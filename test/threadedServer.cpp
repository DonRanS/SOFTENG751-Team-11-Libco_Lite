#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <errno.h>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <map>
#include <chrono>

#define TEST_PORT 8080
#define BUFFER_SIZE 4096  // adjust according to your needs. dont hurt yourself with too big buffer
//#define MAX_CONNECTIONS 10000 //legacy code, not used in this version

// Global state 
std::mutex stats_mutex;
std::mutex log_mutex;
std::atomic<int> active_connections{0};
std::atomic<int> total_connections{0};
std::atomic<long long> bytes_processed{0};
std::atomic<bool> shutdown_flag{false};
std::vector<std::thread> client_threads;
// remove if you want. just for stats and debugging. This is a thread-safe logging function
void thread_safe_log(const std::string& message) {
    std::lock_guard<std::mutex> lock(log_mutex);
    std::cout << "[Thread " << std::this_thread::get_id() << "] " << message << std::endl;
}

void update_stats(int bytes) {
    bytes_processed += bytes; // Just atomic increment, no mutex
}

void handle_client_blocking(int client_fd, struct sockaddr_in client_addr) {
    active_connections++;
    total_connections++;
    
    char buffer[BUFFER_SIZE];
    
    while (!shutdown_flag.load()) {
    
        ssize_t bytes_read = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        
        if (bytes_read > 0) {
            update_stats(bytes_read);
                        ssize_t total_sent = 0;
            while (total_sent < bytes_read) {
                ssize_t sent = send(client_fd, buffer + total_sent, 
                                  bytes_read - total_sent, 0);
                
                if (sent < 0) {
                    goto disconnect;
                }
                total_sent += sent;
            }
            
            std::this_thread::yield(); // thread switch hint, demonstrating thread switching is expensive
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            
        } else if (bytes_read == 0) {
            break;  // Client disconnected
        } else {
            break;  // Error
        }
    }
    
disconnect:
    close(client_fd);
    active_connections--;
}
// remove if youwant. just for stats and debugging
void stats_monitor() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        std::lock_guard<std::mutex> lock(stats_mutex);
        thread_safe_log("Stats: Active=" + std::to_string(active_connections.load()) + 
                       " Total=" + std::to_string(total_connections.load()) +
                       " Bytes=" + std::to_string(bytes_processed.load()));
    }
}

int main() {
    std::cout << "Starting Threaded Echo Server..." << std::endl;
    
    // Create server socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
        return 1;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(TEST_PORT);
    
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Failed to bind: " << strerror(errno) << std::endl;
        close(server_fd);
        return 1;
    }
    
    // Smaller backlog than coroutine version
    if (listen(server_fd, 50) < 0) {
        std::cerr << "Failed to listen: " << strerror(errno) << std::endl;
        close(server_fd);
        return 1;
    }
    
    std::cout << "Server listening on port " << TEST_PORT << std::endl;
    
    std::cout << "Starting threaded accept loop..." << std::endl;
    
    int connection_count = 0;
    while (1) { 
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd > 0) {
            try {
                std::thread client_thread(handle_client_blocking, client_fd, client_addr);
                client_thread.detach();
                connection_count++;
                
            } catch (const std::exception& e) {
                close(client_fd);
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        
        if (connection_count % 1000 == 0) {
            std::cout << "Processed " << connection_count << " connections" << std::endl;
        }
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(5)); // wait for close to finish. we are out of the loop, so, wont affect performance
    
    close(server_fd);
    std::cout << "Shutdown complete" << std::endl;
    std::cout << "Total connections processed: " << total_connections.load() << std::endl;
    std::cout << "Total bytes processed: " << bytes_processed.load() << std::endl;
    
    return 0;
}