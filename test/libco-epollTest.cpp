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
#include "../include/libco-lite.h"
#include "../include/co_socket.h"
#include "../include/scheduler_epoll.h"
#include <iostream>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <algorithm>

#define LISTENING_PORT 17951
#define SAFEWORD "excommunicado"

uintptr_t echo_client(void* arg) {
    auto params = static_cast<std::tuple<CoroutineScheduler*, EpollScheduler*, int>*>(arg);
    CoroutineScheduler* scheduler = std::get<0>(*params);
    EpollScheduler* epoll_scheduler = std::get<1>(*params);
    int client_fd = std::get<2>(*params);
    
    std::cout << "Starting epoll echo handler for client fd " << client_fd << std::endl;
    
    // Send welcome message
    const char* welcome = "Welcome to epoll echo test!\nSend " SAFEWORD " to close connection.\n";
    ssize_t sent = co_socket_epoll::co_send(*scheduler, *epoll_scheduler, client_fd, welcome, strlen(welcome), 0);
    std::cout << "Sent welcome message, result: " << sent << std::endl;
    
    // Echo loop
    char buffer[1024];
    while (true) {
        std::cout << "Waiting to receive data (epoll)..." << std::endl;
        ssize_t n = co_socket_epoll::co_recv(*scheduler, *epoll_scheduler, client_fd, buffer, sizeof(buffer) - 1, 0);
        std::cout << "co_recv_epoll returned: " << n << std::endl;
        
        if (n <= 0) {
            std::cout << "Connection closed or error: " << (n < 0 ? strerror(errno) : "client disconnected") << std::endl;
            break;
        }
        
        buffer[n] = '\0';
        std::cout << "Received: " << buffer << std::endl;
        
        if (strncmp(buffer, SAFEWORD, strlen(SAFEWORD)) == 0) {
            const char* goodbye = "Your membership hereby revoked. Farewell!\n";
            co_socket_epoll::co_send(*scheduler, *epoll_scheduler, client_fd, goodbye, strlen(goodbye), 0);
            break;
        }
        
        // Echo back
        sent = co_socket_epoll::co_send(*scheduler, *epoll_scheduler, client_fd, buffer, n, 0);
        std::cout << "Echo sent, result: " << sent << std::endl;
    }
    
    std::cout << "Closing client fd " << client_fd << std::endl;
    close(client_fd);
    delete params;
    return 0;
}

// Wait for and accept connections
uintptr_t accept_connections(void* arg) {
    auto params = static_cast<std::pair<CoroutineScheduler*, EpollScheduler*>*>(arg);
    CoroutineScheduler* scheduler = params->first;
    EpollScheduler* epoll_scheduler = params->second;
    const int PORT = LISTENING_PORT;
    
    int server_fd = co_socket::create_server_socket(NULL, PORT);
    if (server_fd < 0) {
        std::cerr << "Failed to create server socket: " << strerror(errno) << std::endl;
        return 1;
    }
    
    if (listen(server_fd, SOMAXCONN) < 0) {
        std::cerr << "Failed to listen: " << strerror(errno) << std::endl;
        close(server_fd);
        return 1;
    }
    
    std::cout << "Epoll echo server listening on port " << PORT << std::endl;
    
    while (true) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        std::cout << "Waiting for client connection (epoll)..." << std::endl;
        int client_fd = co_socket_epoll::co_accept(*scheduler, *epoll_scheduler, server_fd, 
                                                 (struct sockaddr*)&client_addr, &addr_len);
        
        if (client_fd < 0) {
            std::cerr << "Accept failed: " << strerror(errno) << std::endl;
            scheduler->Yield(0);  // Yield to prevent busy-waiting
            continue;
        }
        
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        int client_port = ntohs(client_addr.sin_port);
        
        std::cout << "New client connected: " << client_ip << ":" << client_port 
                  << " (fd: " << client_fd << ")" << std::endl;
        
        // Create parameters for the echo client coroutine
        auto* client_params = new std::tuple<CoroutineScheduler*, EpollScheduler*, int>(
            scheduler, epoll_scheduler, client_fd);
        
        // Start a new coroutine to handle this client
        int coro_id = scheduler->CreateCoroutine(echo_client, client_params);
        std::cout << "Created client handler coroutine with ID: " << coro_id << std::endl;
        
        // Yield to give the new coroutine a chance to run
        scheduler->Yield(0);
    }
    
    close(server_fd);
    delete params;
    return 0;
}

int main() {
    // Create coroutine scheduler with 64KB stack per coroutine
    // Increase this value if you need more stack space for your coroutines
    CoroutineScheduler scheduler(64 * 1024);
    EpollScheduler epoll_scheduler;
    
    // Create context ptr for the acceptor coroutine
    auto* client_ctx = new std::pair<CoroutineScheduler*, EpollScheduler*>(&scheduler, &epoll_scheduler);
    
    // Create the acceptor coroutine
    int acceptor_id = scheduler.CreateCoroutine(accept_connections, client_ctx);
    
    std::cout << "Main: Starting coroutine scheduler with epoll" << std::endl;
    
    // Track ALL coroutines - this is critical!
    std::vector<int> coroutines;
    coroutines.push_back(acceptor_id);
    
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
        epoll_scheduler.Poll(1);  // 1ms timeout (small timeout to avoid blocking too long)
        
        // Clean up finished coroutines
        for (auto it = coroutines.begin(); it != coroutines.end();) {
            if (!scheduler.IsCoroutineAlive(*it)) {
                it = coroutines.erase(it);
            } else {
                ++it;
            }
        }
        
        // Check for new coroutines
        int total_coroutines = scheduler.GetCoroutineCount();
        
        // If we have more coroutines than we're tracking, add them
        if (total_coroutines > coroutines.size()) {
            // Simple but inefficient way to find new coroutines
            for (int i = 0; i < 1000; i++) {  // Arbitrary limit
                if (scheduler.IsCoroutineAlive(i) && 
                    std::find(coroutines.begin(), coroutines.end(), i) == coroutines.end()) {
                    coroutines.push_back(i);
                }
            }
        }
        
        // Prevent 100% CPU usage
        usleep(1000);  // 1ms sleep
    }
    
    std::cout << "All coroutines have finished. Exiting." << std::endl;
    return 0;
}