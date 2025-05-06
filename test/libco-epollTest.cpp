// This is a simple echo server using libco-lite and epoll.
// created on 2025-05-06 14:00:00
// a basic echo server using coroutines and epoll for I/O multiplexing
#include "../include/libco-lite.h"
#include "../include/socket.h"
#include "../include/scheduler_epoll.h"
#include <iostream>
#include <string>
#include <signal.h>
#include <atomic>

// Global flag for graceful shutdown
std::atomic<bool> running(true);

// Signal handler
void signal_handler(int sig) {
    std::cout << "Received signal " << sig << ", shutting down...\n";
    running = false;
}

// Simple echo client coroutine
uintptr_t echo_client(void* arg) {
    struct {
        int fd;
        CoroutineScheduler* scheduler;
        EpollScheduler* epoll_scheduler;
    } *ctx = static_cast<decltype(ctx)>(arg);
    
    int fd = ctx->fd;
    CoroutineScheduler* scheduler = ctx->scheduler;
    EpollScheduler* epoll_scheduler = ctx->epoll_scheduler;
    
    // Buffer for data exchange
    char buffer[1024];
    
    // Receive and process data
    while (running) {
        // Clear buffer
        memset(buffer, 0, sizeof(buffer));
        
        // Receive data using epoll
        ssize_t n = co_socket_epoll::co_recv(*scheduler, *epoll_scheduler, fd, buffer, sizeof(buffer) - 1, 0);
        
        if (n <= 0) {
            // Connection closed or error
            std::cout << "Client disconnected\n";
            break;
        }
        
        buffer[n] = '\0';
        std::cout << "Received: " << buffer << std::endl;
        
        // Echo data back
        std::string response = "Echo: ";
        response += buffer;
        
        co_socket_epoll::co_send(*scheduler, *epoll_scheduler, fd, response.c_str(), response.size(), 0);
    }
    
    // Clean up
    close(fd);
    delete ctx;
    return 0;
}

// Server coroutine
uintptr_t server_routine(void* arg) {
    struct {
        CoroutineScheduler* scheduler;
        EpollScheduler* epoll_scheduler;
    } *ctx = static_cast<decltype(ctx)>(arg);
    
    CoroutineScheduler* scheduler = ctx->scheduler;
    EpollScheduler* epoll_scheduler = ctx->epoll_scheduler;
    
    const int PORT = 8080;
    
    // Create server socket
    int server_fd = co_socket::create_server_socket(NULL, PORT);
    if (server_fd < 0) {
        std::cerr << "Failed to create server socket\n";
        return 1;
    }
    
    // Listen for connections
    if (listen(server_fd, SOMAXCONN) < 0) {
        std::cerr << "Failed to listen\n";
        close(server_fd);
        return 1;
    }
    
    std::cout << "Echo server listening on port " << PORT << std::endl;
    std::cout << "Test with: telnet localhost 8080 or nc localhost 8080\n";
    
    // Accept loop
    while (running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        // Accept connection using epoll
        int client_fd = co_socket_epoll::co_accept(*scheduler, *epoll_scheduler, 
                                                  server_fd, (struct sockaddr*)&client_addr, &addr_len);
        
        if (client_fd < 0) {
            if (!running) break;
            std::cerr << "Accept failed\n";
            continue;
        }
        
        // Print client info
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        std::cout << "New connection from " << client_ip << ":" << ntohs(client_addr.sin_port) << std::endl;
        
        // Create context for the client handler
        auto* client_ctx = new struct {
            int fd;
            CoroutineScheduler* scheduler;
            EpollScheduler* epoll_scheduler;
        };
        
        client_ctx->fd = client_fd;
        client_ctx->scheduler = scheduler;
        client_ctx->epoll_scheduler = epoll_scheduler;
        
        // Create coroutine to handle this client
        scheduler->CreateCoroutine(echo_client, client_ctx);
        
        // Clean up abandoned coroutines
        scheduler->CleanupAbandonedCoroutines();
    }
    
    // Clean up
    close(server_fd);
    delete ctx;
    return 0;
}

int main() {
    // Register signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        // Create coroutine scheduler with 64KB stack per coroutine
        CoroutineScheduler scheduler(64 * 1024);
        
        // Create epoll scheduler
        EpollScheduler epoll_scheduler;
        
        // Context for server coroutine
        auto* ctx = new struct {
            CoroutineScheduler* scheduler;
            EpollScheduler* epoll_scheduler;
        };
        
        ctx->scheduler = &scheduler;
        ctx->epoll_scheduler = &epoll_scheduler;
        
        // Create server coroutine
        int server_id = scheduler.CreateCoroutine(server_routine, ctx);
        
        std::cout << "Coroutine echo server started\n";
        std::cout << "Press Ctrl+C to exit\n";
        
        // Main loop
        while (running && scheduler.IsCoroutineAlive(server_id)) {
            // Resume server coroutine to accept connections
            scheduler.ResumeCoroutine(server_id);
            
            // Process epoll events - this will resume any waiting coroutines
            epoll_scheduler.Poll(100);  // 100ms timeout
            
            // Clean up any abandoned coroutines
            scheduler.CleanupAbandonedCoroutines();
        }
        
        std::cout << "Server stopped\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}