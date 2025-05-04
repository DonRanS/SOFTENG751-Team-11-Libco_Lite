// Description: A simple echo server using libco-lite and co_socket
// This server accepts TCP connections and echoes back any messages received.
// It uses coroutines to handle multiple clients concurrently without blocking the main thread.
// For Linux and macOS platforms only.
// 2025-05-05: Initial version, not tested yet
#include "../include/libco-lite.h"
#include "../include/socket.h"
#include <iostream>
#include <string>
#include <vector>

// Struct to pass to client handler coroutine
struct ClientContext {
    int fd;
    CoroutineScheduler* scheduler;
    struct sockaddr_in addr;
};

// Client handler coroutine
uintptr_t handle_client(void* arg) {
    ClientContext* ctx = static_cast<ClientContext*>(arg);
    int fd = ctx->fd;
    CoroutineScheduler* scheduler = ctx->scheduler;
    
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(ctx->addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    int client_port = ntohs(ctx->addr.sin_port);
    
    std::cout << "New client connected: " << client_ip << ":" << client_port << std::endl;
    
    // Send welcome message
    const char* welcome = "Welcome to echo server!\nType 'exit' to close connection.\n";
    co_socket::co_send(*scheduler, fd, welcome, strlen(welcome), 0);
    
    // Echo loop
    char buffer[1024];
    while (true) {
        // Receive data
        ssize_t n = co_socket::co_recv(*scheduler, fd, buffer, sizeof(buffer) - 1, 0);
        
        if (n <= 0) {
            // Connection closed or error
            break;
        }
        
        // Null-terminate and process the message
        buffer[n] = '\0';
        
        // Check for exit command
        if (strncmp(buffer, "exit", 4) == 0) {
            const char* goodbye = "Goodbye!\n";
            co_socket::co_send(*scheduler, fd, goodbye, strlen(goodbye), 0);
            break;
        }
        
        // Echo back
        co_socket::co_send(*scheduler, fd, buffer, n, 0);
    }
    
    std::cout << "Client disconnected: " << client_ip << ":" << client_port << std::endl;
    
    // Clean up
    close(fd);
    delete ctx;
    
    return 0;
}

// Server coroutine
uintptr_t server_routine(void* arg) {
    CoroutineScheduler* scheduler = static_cast<CoroutineScheduler*>(arg);
    const int PORT = 8080;
    
    // Create server socket
    int server_fd = co_socket::create_server_socket(NULL, PORT);
    if (server_fd < 0) {
        std::cerr << "Failed to create server socket: " << strerror(errno) << std::endl;
        return 1;
    }
    
    // Listen for connections
    if (listen(server_fd, SOMAXCONN) < 0) {
        std::cerr << "Failed to listen: " << strerror(errno) << std::endl;
        close(server_fd);
        return 1;
    }
    
    std::cout << "Echo server listening on port " << PORT << std::endl;
    
    // Accept connections in an infinite loop
    while (true) {
        ClientContext* ctx = new ClientContext();
        ctx->scheduler = scheduler;
        socklen_t addr_len = sizeof(ctx->addr);
        
        // Accept new connection
        int client_fd = co_socket::co_accept(*scheduler, server_fd, 
                                           (struct sockaddr*)&ctx->addr, &addr_len);
        
        if (client_fd < 0) {
            std::cerr << "Failed to accept: " << strerror(errno) << std::endl;
            delete ctx;
            continue;
        }
        
        // Store client socket
        ctx->fd = client_fd;
        
        // Create a new coroutine to handle this client
        scheduler->CreateCoroutine(handle_client, ctx);
        
        // Clean up abandoned coroutines (exit status)
        scheduler->CleanupAbandonedCoroutines();
    }
    
    // We'll never reach here, but for completeness
    close(server_fd);
    return 0;
}

int main() {
    // Create a scheduler with 64KB stack per coroutine
    CoroutineScheduler scheduler(64 * 1024);
    
    // Create the server coroutine
    int server_id = scheduler.CreateCoroutine(server_routine, &scheduler);
    
    // Main loop: keep resuming the server coroutine
    while (scheduler.IsCoroutineAlive(server_id)) {
        scheduler.ResumeCoroutine(server_id, 0);
        
        // Periodically clean up abandoned coroutines
        scheduler.CleanupAbandonedCoroutines();
    }
    
    return 0;
}