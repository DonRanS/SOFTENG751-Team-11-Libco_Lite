// Description: A simple echo server using libco-lite and co_socket
// This server accepts TCP connections and echoes back any messages received.
// It uses coroutines to handle multiple clients concurrently without blocking the main thread.
// For Linux and macOS platforms only.
// 2025-05-05 02:21:00 Initial version, not tested yet
// 2025-05-05 21:24:00 Tested. Functions are working as expected.

// 2025-05-07 13:30:00 abandon linear search for new coroutines, replced with a hash set to track existing IDs

//2025-05-08 00:55:00 minor refactoring, added comments and improved readability
#include "../include/libco-lite.h"
#include "../include/co_socket.h"
#include <iostream>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <algorithm>
#include <unordered_set>

//#define VERBOSE_LOGGING  // Uncomment to enable verbose logging


#define LISTENING_PORT 17951
#define SAFEWORD "excommunicado"

uintptr_t echo_client(void* arg) {
    auto params = static_cast<std::pair<CoroutineScheduler*, int>*>(arg);
    CoroutineScheduler* scheduler = params->first;
    int client_fd = params->second;
    #ifdef VERBOSE_LOGGING
    std::cout << "Starting echo handler for client fd " << client_fd << std::endl;
    #endif
    // Send welcome message
    const char* welcome = "Welcome to echo test!\nSend " SAFEWORD " to close connection.\n";
    ssize_t sent = co_socket::co_send(*scheduler, client_fd, welcome, strlen(welcome), 0);
    #ifdef VERBOSE_LOGGING
    std::cout << "Sent welcome message, result: " << sent << std::endl;
    #endif
    // Echo loop
    char buffer[1024];
    while (true) {
        #ifdef VERBOSE_LOGGING
        std::cout << "Waiting to receive data..." << std::endl;
        #endif
        ssize_t n = co_socket::co_recv(*scheduler, client_fd, buffer, sizeof(buffer) - 1, 0);
        #ifdef VERBOSE_LOGGING
        std::cout << "co_recv returned: " << n << std::endl;
        #endif
        if (n <= 0) {
            std::cout << "Connection closed or error: " << (n < 0 ? strerror(errno) : "client disconnected") << std::endl;
            break;
        }
        
        buffer[n] = '\0';
        #ifdef VERBOSE_LOGGING
        std::cout << "Received: " << buffer << std::endl;
        #endif
        if (strncmp(buffer, SAFEWORD, strlen(SAFEWORD)) == 0) {
            const char* goodbye = "Your membership hereby revoked. Farewell!\n";
            co_socket::co_send(*scheduler, client_fd, goodbye, strlen(goodbye), 0);
            break;
        }
        
        // Echo
        sent = co_socket::co_send(*scheduler, client_fd, buffer, n, 0);
        #ifdef VERBOSE_LOGGING
        std::cout << "Echo sent, result: " << sent << std::endl;
        #endif
    }
    
    std::cout << "Closing client fd " << client_fd << std::endl;
    close(client_fd);
    delete params;
    return 0;
}

/*uintptr_t echo_client(void* arg) {

    auto params = static_cast<std::tuple<CoroutineScheduler*, EpollScheduler*, int>*>(arg);
    CoroutineScheduler* scheduler = std::get<0>(*params);
    EpollScheduler* epoll_scheduler = std::get<1>(*params);
    int client_fd = std::get<2>(*params);
    
    // Send welcome message
    const char* welcome = "Welcome to epoll echo test!\nSend " SAFEWORD " to close connection.\n";
    ssize_t sent = co_socket_epoll::co_send(*scheduler, *epoll_scheduler, client_fd, welcome, strlen(welcome), 0);
    
    // Allocate buffer on the heap instead of stack
    std::unique_ptr<char[]> buffer(new char[102400]);
    
    // Echo loop
    while (true) {
        ssize_t n = co_socket_epoll::co_recv(*scheduler, *epoll_scheduler, client_fd, buffer.get(), 102399, 0);
        
        if (n <= 0) {
            break;
        }
        
        buffer[n] = '\0';
        
        if (strncmp(buffer.get(), SAFEWORD, strlen(SAFEWORD)) == 0) {
            const char* goodbye = "Your membership hereby revoked. Farewell!\n";
            co_socket_epoll::co_send(*scheduler, *epoll_scheduler, client_fd, goodbye, strlen(goodbye), 0);
            break;
        }
        
        // Echo back
        sent = co_socket_epoll::co_send(*scheduler, *epoll_scheduler, client_fd, buffer.get(), n, 0);
    }
    
    close(client_fd);
    delete params;
    return 0;
}
*/
// Wait for and accept connections
uintptr_t accept_connections(void* arg) {
    CoroutineScheduler* scheduler = static_cast<CoroutineScheduler*>(arg);
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
    
    std::cout << "Echo server listening on port " << PORT << std::endl;
    
    while (true) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        #ifdef VERBOSE_LOGGING
        std::cout << "Waiting for client connection..." << std::endl;
        #endif
        int client_fd = co_socket::co_accept(*scheduler, server_fd, (struct sockaddr*)&client_addr, &addr_len);
        
        if (client_fd < 0) {
            std::cerr << "Accept failed: " << strerror(errno) << std::endl;
            scheduler->Yield(0);  // Yield to prevent busy-waiting
            continue;
        }
        
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        int client_port = ntohs(client_addr.sin_port);
        #ifdef VERBOSE_LOGGING
        std::cout << "New client connected: " << client_ip << ":" << client_port 
                  << " (fd: " << client_fd << ")" << std::endl;
        #endif
        // resolved issues: auto* to initialize a sturct is not allowed in C++11...
        // original: auto* client_ctx = new struct{......}
        auto* client_ctx = new std::pair<CoroutineScheduler*, int>(scheduler, client_fd);
        
        // Start a new coroutine to handle this client
        int coro_id = scheduler->CreateCoroutine(echo_client, client_ctx);
        #ifdef VERBOSE_LOGGING
        std::cout << "Created client handler coroutine with ID: " << coro_id << std::endl;
        #endif
        // Yield to give the new coroutine a chance to run
        scheduler->Yield(0);
    }
    
    close(server_fd);
    return 0;
}

int main() {
    CoroutineScheduler scheduler(1024 * 1024);
    
    // Create the acceptor coroutine
    int acceptor_id = scheduler.CreateCoroutine(accept_connections, &scheduler);
    
    std::cout << "Main: Starting coroutine scheduler" << std::endl;
    
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
        
        // Clean up finished coroutines
        for (auto it = coroutines.begin(); it != coroutines.end();) {
            if (!scheduler.IsCoroutineAlive(*it)) {
                it = coroutines.erase(it);
            } else {
                ++it;
            }
        }
        /*
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
        */
       //
       // Check for new coroutines
       // Use a more efficient approach to find new coroutines
       // a O(1) hash set to track existing IDs
int total_coroutines = scheduler.GetCoroutineCount();
if (total_coroutines > coroutines.size()) {
    // Use a more efficient approach
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
        // Prevent 100% CPU usage
        //usleep(1000);  // 1ms sleep
    }
    
    std::cout << "All coroutines have finished. Exiting." << std::endl;
    return 0;
}