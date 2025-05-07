// Impl of EpollScheduler and coroutine socket functions
// Linux and BSD-based systems only
// Created on 2025-05-06 14:00:00
// Tested on 2025-05-06 15:20:00
// scheduler_epoll.cpp

// change: yield timing squence
//event registration and unregistration
// 2025-05-06 15:20:00

// replaced system socket with coroutine socket
// refactoring: 2025-05-06 15:50:00 
// corrected registration and unregistration of events
// revised main loop and error handling. Now it accepts multiple clients.
// And echoes back when connection is established.
//
// 2025-05-06 16:30:00 Passed test on Ubuntu 22.04 LTS
// Optimized version of epoll server for handling massive connections

// 2025-05-08 00:55:00 refactored the code to use a hash set to track existing IDs


#include "../include/libco-lite.h"
#include "../include/co_socket.h"
#include "../include/scheduler_epoll.h"
#include <iostream>
#include <string.h>

#include <chrono>

#include <unistd.h>
#include <vector>
#include <algorithm>
#include <unordered_set>
#include <memory>
#include <atomic>

#define LISTENING_PORT 17952
#define SAFEWORD "excommunicado"


// Uncomment to enable verbose logging
// #define VERBOSE_LOGGING 

// Connection stats for monitoring
std::atomic<int> active_connections{0};
std::atomic<uint64_t> total_bytes_received{0};
std::atomic<uint64_t> total_bytes_sent{0};

uintptr_t echo_client(void* arg) {
    auto params = static_cast<std::tuple<CoroutineScheduler*, EpollScheduler*, int>*>(arg);
    CoroutineScheduler* scheduler = std::get<0>(*params);
    EpollScheduler* epoll_scheduler = std::get<1>(*params);
    int client_fd = std::get<2>(*params);
    
    // Track connection
    active_connections++;
    
    #ifdef VERBOSE_LOGGING
        std::cout << "Starting epoll echo handler for client fd " << client_fd << std::endl;
    #endif
    
    // Send welcome message
    const char* welcome = "Welcome to epoll echo test!\nSend " SAFEWORD " to close connection.\n";
    ssize_t sent = co_socket_epoll::co_send(*scheduler, *epoll_scheduler, client_fd, welcome, strlen(welcome), 0);
    if (sent > 0) {
        total_bytes_sent += sent;
    }
    
    // Allocate buffer on the heap instead of stack
    std::unique_ptr<char[]> buffer(new char[8192]);
    
    // Echo loop
    while (true) {
        ssize_t n = co_socket_epoll::co_recv(*scheduler, *epoll_scheduler, client_fd, buffer.get(), 8191, 0);
        
        if (n <= 0) {
          #ifdef VERBOSE_LOGGING
                std::cout << "Connection closed: fd " << client_fd << std::endl;
            #endif
            break;
        }
        
        total_bytes_received += n;
        buffer[n] = '\0';
        
        if (strncmp(buffer.get(), SAFEWORD, strlen(SAFEWORD)) == 0) {
            const char* goodbye = "Your membership hereby revoked. Farewell!\n";
            sent = co_socket_epoll::co_send(*scheduler, *epoll_scheduler, client_fd, goodbye, strlen(goodbye), 0);
            if (sent > 0) {
                total_bytes_sent += sent;
            }
            break;
        }
        
        // Echo back
        sent = co_socket_epoll::co_send(*scheduler, *epoll_scheduler, client_fd, buffer.get(), n, 0);
        if (sent > 0) {
            total_bytes_sent += sent;
        }
    }
    
    close(client_fd);
    delete params;
    
    // Clean up connection tracking
    active_connections--;
    return 0;
}

// Wait for and accept connections
uintptr_t accept_connections(void* arg) {
    auto params = static_cast<std::pair<CoroutineScheduler*, EpollScheduler*>*>(arg);
    CoroutineScheduler* scheduler = params->first;
    EpollScheduler* epoll_scheduler = params->second;
    const int PORT = LISTENING_PORT;
    
    // Create server socket with increased backlog
    int server_fd = co_socket::create_server_socket(NULL, PORT);
    if (server_fd < 0) {
        std::cerr << "Failed to create server socket: " << strerror(errno) << std::endl;
        return 1;
    }
    
    // Increase listen backlog to handle more connections
    if (listen(server_fd, 65535) < 0) {
        std::cerr << "Failed to listen: " << strerror(errno) << std::endl;
        close(server_fd);
        return 1;
    }
    
    std::cout << "Epoll echo server listening on port " << PORT << std::endl;
    
    // Connection acceptance loop
    while (true) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        int client_fd = co_socket_epoll::co_accept(*scheduler, *epoll_scheduler, server_fd, 
                                                 (struct sockaddr*)&client_addr, &addr_len);
        
        if (client_fd < 0) {
            std::cerr << "Accept failed: " << strerror(errno) << std::endl;
            scheduler->Yield(0);  // Yield to prevent busy-waiting
            continue;
        }
        
        // Reduce socket buffer sizes to conserve memory
        int buffer_size = 16384;  // 16KB buffer size
        setsockopt(client_fd, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size));
        setsockopt(client_fd, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size));
        
        // Only log connection details in verbose mode
        #ifdef VERBOSE_LOGGING
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
            int client_port = ntohs(client_addr.sin_port);
            
            std::cout << "New client connected: " << client_ip << ":" << client_port 
                    << " (fd: " << client_fd << ")" << std::endl;
        #endif
        
        // Create parameters for the echo client coroutine
        auto* client_params = new std::tuple<CoroutineScheduler*, EpollScheduler*, int>(
            scheduler, epoll_scheduler, client_fd);
        
        // Start a new coroutine to handle this client
        int coro_id = scheduler->CreateCoroutine(echo_client, client_params);
        
        // Yield to give the new coroutine a chance to run
        scheduler->Yield(0);
        
        // Print connection stats periodically (every 100 connections)
        static int connection_count = 0;
        if (++connection_count % 100 == 0) {
            std::cout << "Stats: Active Connections: " << active_connections 
                     << ", Total Received: " << (total_bytes_received / (1024*1024)) << " MB"
                     << ", Total Sent: " << (total_bytes_sent / (1024*1024)) << " MB" 
                     << std::endl;
        }
    }
    
    close(server_fd);
    delete params;
    return 0;
}

int main() {
    // Allocate much more memory for coroutines to handle massive connections
    // With 8KB per coroutine, we can handle up to 65535 connections with 512MB
    constexpr size_t STACK_SIZE_PER_COROUTINE = 8 * 1024;  // 8KB per coroutine
    constexpr size_t MAX_CONNECTIONS = 65535;
    constexpr size_t TOTAL_MEMORY = STACK_SIZE_PER_COROUTINE * MAX_CONNECTIONS;
    
    CoroutineScheduler scheduler(TOTAL_MEMORY);
    EpollScheduler epoll_scheduler;
    
    // Create context ptr for the acceptor coroutine
    auto* client_ctx = new std::pair<CoroutineScheduler*, EpollScheduler*>(&scheduler, &epoll_scheduler);
    
    // Create the acceptor coroutine
    int acceptor_id = scheduler.CreateCoroutine(accept_connections, client_ctx);
    
    std::cout << "Main: Starting coroutine scheduler with epoll" << std::endl;
    std::cout << "Allocated " << (TOTAL_MEMORY / (1024 * 1024)) << " MB for coroutines" << std::endl;
    std::cout << "Maximum connections supported: " << MAX_CONNECTIONS << std::endl;
    
    // Track ALL coroutines efficiently
    std::vector<int> coroutines;
    coroutines.reserve(1024);  // Pre-allocate for better performance
    coroutines.push_back(acceptor_id);
    
    // Status reporting variables
    auto last_status_time = std::chrono::steady_clock::now();
    
    // Main loop
    while (!coroutines.empty()) {
        // Get current count to avoid modifying while iterating
        int current_size = coroutines.size();
        
        // Resume each coroutine
        for (int i = 0; i < current_size; i++) {
            int id = coroutines[i];
            if (scheduler.IsCoroutineAlive(id)) {
                scheduler.ResumeCoroutine(id);
            }
        }
        
        // Process epoll events - this will resume waiting coroutines
        epoll_scheduler.Poll(1);  // 1ms timeout
        
        // Clean up finished coroutines
        for (auto it = coroutines.begin(); it != coroutines.end();) {
            if (!scheduler.IsCoroutineAlive(*it)) {
                it = coroutines.erase(it);
            } else {
                ++it;
            }
        }
        
        // Check for new coroutines - using optimized method
        int total_coroutines = scheduler.GetCoroutineCount();
        if (total_coroutines > coroutines.size()) {
            // Use a more efficient approach with hash set
            std::unordered_set<int> existing_ids(coroutines.begin(), coroutines.end());
            
            // Find maximum ID currently tracked
            int max_id = 0;
            if (!coroutines.empty()) {
                max_id = *std::max_element(coroutines.begin(), coroutines.end()) + 1;
            }
            
            // First check low IDs (more likely to have new coroutines)
            for (int i = 0; i < max_id && coroutines.size() < total_coroutines; i++) {
                if (scheduler.IsCoroutineAlive(i) && 
                    existing_ids.find(i) == existing_ids.end()) {
                    coroutines.push_back(i);
                    existing_ids.insert(i);
                }
            }
            
            // Then check higher IDs if we still haven't found all coroutines
            for (int i = max_id; coroutines.size() < total_coroutines; i++) {
                if (scheduler.IsCoroutineAlive(i) && 
                    existing_ids.find(i) == existing_ids.end()) {
                    coroutines.push_back(i);
                    existing_ids.insert(i);
                }
            }
        }
        
        // Print status every 10 seconds
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            current_time - last_status_time).count();
        
        if (elapsed >= 10) {
            std::cout << "Status: Active Connections: " << active_connections 
                     << ", Coroutines: " << coroutines.size()
                     << ", Data sent: " << (total_bytes_sent / (1024*1024)) << " MB"
                     << ", Data received: " << (total_bytes_received / (1024*1024)) << " MB"
                     << std::endl;
            last_status_time = current_time;
        }
    }
    
    std::cout << "All coroutines have finished. Exiting." << std::endl;
    return 0;
}