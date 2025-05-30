#include "libco-lite.h"
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <cstring>
#include <errno.h>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <signal.h>

#define TEST_PORT 8080
#define BUFFER_SIZE 4096        //  depends on your needs, but 4KB is a good default
#define MAX_EVENTS 1024         // batch size for epoll events
#define SOCKET_BUFFER_SIZE 524288 // 512KB socket buffers
#define MAX_ACCEPT_BATCH 16     // Accept more connections per batch
#define MAX_READ_BATCH 8        // More reads before yielding
#define POLL_BATCH_SIZE 100     // Process events in larger batches

// OPTIMIZATION: Reduce polling frequency for high connection counts
// Adjust these values based on your system's performance
#define MIN_POLL_INTERVAL_US 500   // Minimum 1ms between polls
#define MAX_POLL_INTERVAL_US 10000  // Maximum 10ms between polls

// Signal handling for graceful shutdown
// Graceful~
volatile bool shutdown_requested = false;

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        shutdown_requested = true;
        std::cout << "\nShutdown requested..." << std::endl;
        exit(0);
    }
}

// Global epoll for all I/O
int epoll_fd = -1;
std::unordered_map<int, hndl_t> fd_to_coro;
std::unordered_map<hndl_t, int> coro_to_fd;

// OPTIMIZATION: Use sets for faster lookups and deduplication
std::unordered_set<hndl_t> ready_coroutines_set;
std::vector<hndl_t> ready_coroutines_vec;
std::vector<int> cleanup_fds;

// OPTIMIZATION: Round-robin scheduler to avoid starvation
std::vector<hndl_t> all_client_coroutines;
size_t round_robin_index = 0;

// Performance tracking
static int total_connections = 0;
static long long total_bytes = 0;
static auto last_poll_time = std::chrono::steady_clock::now();

// OPTIMIZATION: Ultra-fast socket setup
inline void optimize_socket(int fd) {
    // Disable Nagle's algorithm
    int nodelay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
    
    // Very large socket buffers for high-throughput scenarios
    int buf_size = SOCKET_BUFFER_SIZE;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
    
    // Enable keepalive with aggressive settings
    int keepalive = 1;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
    
    int keepidle = 30;   // Start keepalive after 30 seconds
    int keepintvl = 5;   // 5 second intervals
    int keepcnt = 3;     // 3 probes before declaring dead
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));
}

void handle_client(void* arg) {
    hndl_t my_handle = get_running_coroutine();
    int client_fd = coro_to_fd[my_handle];
    char buffer[BUFFER_SIZE];
    
    // OPTIMIZATION: Track yields to reduce frequency
    int yields_since_work = 0;
    const int max_yields_before_work = 3;
    
    while (!shutdown_requested) {
        bool did_work = false;
        int consecutive_reads = 0;
        
        // OPTIMIZATION: More aggressive read batching
        while (consecutive_reads < MAX_READ_BATCH) {
            ssize_t bytes_read = recv(client_fd, buffer, BUFFER_SIZE - 1, MSG_DONTWAIT);
            
            if (bytes_read > 0) {
                did_work = true;
                consecutive_reads++;
                total_bytes += bytes_read;
                yields_since_work = 0;
                
                // OPTIMIZATION: Optimized send with minimal syscalls
                const char* send_ptr = buffer;
                ssize_t remaining = bytes_read;
                
                while (remaining > 0) {
                    ssize_t sent = send(client_fd, send_ptr, remaining, 
                                      MSG_DONTWAIT | MSG_NOSIGNAL);
                    if (sent < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // Only yield if we can't send immediately
                            yield();
                            continue;
                        } else {
                            goto disconnect;
                        }
                    }
                    send_ptr += sent;
                    remaining -= sent;
                }
                
                // Continue reading if buffer was full (likely more data)
                if (bytes_read == BUFFER_SIZE - 1) {
                    continue;
                }
                
                // If small read, probably no more data immediately available
                if (bytes_read < BUFFER_SIZE / 4) {
                    break;
                }
                
            } else if (bytes_read == 0) {
                goto disconnect;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break; // No more data
                } else {
                    // i know goto is harmful, but this is a performance-critical path
                    goto disconnect;
                }
            }
        }
        
        // OPTIMIZATION: Smart yielding - yield less when busy, more when idle
        if (did_work) {
            yields_since_work = 0;
            // Only yield occasionally when actively processing
            if (consecutive_reads >= MAX_READ_BATCH) {
                yield();
            }
        } else {
            yields_since_work++;
            // Always yield when no work, but track frequency
            yield();
            
            // If we've yielded many times without work, this connection is idle
            if (yields_since_work > max_yields_before_work) {
                yields_since_work = 0;
            }
        }
    }
    
disconnect:
    cleanup_fds.push_back(client_fd);
}

// OPTIMIZATION: Batch cleanup with efficient removal
void process_cleanup() {
    for (int client_fd : cleanup_fds) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
        
        auto it = fd_to_coro.find(client_fd);
        if (it != fd_to_coro.end()) {
            hndl_t coro = it->second;
            
            // Remove from all data structures
            fd_to_coro.erase(it);
            coro_to_fd.erase(coro);
            ready_coroutines_set.erase(coro);
            
            // Remove from round-robin list
            auto rr_it = std::find(all_client_coroutines.begin(), 
                                  all_client_coroutines.end(), coro);
            if (rr_it != all_client_coroutines.end()) {
                all_client_coroutines.erase(rr_it);
                // Adjust round-robin index if needed
                if (round_robin_index >= all_client_coroutines.size()) {
                    round_robin_index = 0;
                }
            }
        }
        
        close(client_fd);
    }
    cleanup_fds.clear();
}

void server_coroutine(void* arg) {
    int server_fd = *(int*)arg;
    
    while (true) {
        int accepts_this_round = 0;
        
        // OPTIMIZATION: More aggressive batch accepting
        while (accepts_this_round < MAX_ACCEPT_BATCH) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
            
            if (client_fd > 0) {
                accepts_this_round++;
                
                int flags = fcntl(client_fd, F_GETFL, 0);
                if (flags >= 0) {
                    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
                }
                
                optimize_socket(client_fd);
                
                hndl_t client_handler = go(handle_client, nullptr);
                if (client_handler < 0) {
                    close(client_fd);
                    continue;
                }
                
                fd_to_coro[client_fd] = client_handler;
                coro_to_fd[client_handler] = client_fd;
                
                total_connections++;
                
                // Add to round-robin scheduler
                all_client_coroutines.push_back(client_handler);
                
                std::cout << "Accepted connection " << total_connections 
                          << " from " << inet_ntoa(client_addr.sin_addr) 
                          << " (fd " << client_fd << ")" << std::endl;
                
                struct epoll_event ev;
                ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
                ev.data.fd = client_fd;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);
                
            } else {
                break;
            }
        }
        
        yield();
    }
}

int main() {
    std::cout << "Starting Coroutine Echo Server" << std::endl;
    
    // Set up signal handlers for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // OPTIMIZATION: Pre-allocate for high connection counts
    ready_coroutines_vec.reserve(MAX_EVENTS);
    cleanup_fds.reserve(MAX_EVENTS);
    all_client_coroutines.reserve(5000);
    
    epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0) {
        std::cerr << "Failed to create epoll: " << strerror(errno) << std::endl;
        return 1;
    }
    
    int server_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
        return 1;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    
    int flags = fcntl(server_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        std::cerr << "Failed to set non-blocking: " << strerror(errno) << std::endl;
        close(server_fd);
        return 1;
    }
    
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
    
    // Very large backlog for high connection rates
    if (listen(server_fd, 2048) < 0) {
        std::cerr << "Failed to listen: " << strerror(errno) << std::endl;
        close(server_fd);
        return 1;
    }
    
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);
    
    std::cout << "Server listening on port " << TEST_PORT << std::endl;
    
    hndl_t server_coro = go(server_coroutine, &server_fd);
    if (server_coro < 0) {
        std::cerr << "Failed to create server coroutine" << std::endl;
        close(server_fd);
        return 1;
    }
    
    fd_to_coro[server_fd] = server_coro;
    coro_to_fd[server_coro] = server_fd;
    
    std::cout << "Starting scheduler..." << std::endl;
    std::cout << "Server ready!" << TEST_PORT << std::endl;
    
    // HIGH-SCALE SCHEDULER WITH REDUCED POLLING OVERHEAD
    struct epoll_event events[MAX_EVENTS];
    int poll_cycles_since_events = 0;
    int round_robin_cycles = 0;
    
    while (true) {
        auto current_time = std::chrono::steady_clock::now();
        auto time_since_last_poll = std::chrono::duration_cast<std::chrono::microseconds>(
            current_time - last_poll_time).count();
        
        // OPTIMIZATION: Adaptive polling - reduce frequency under high load
        bool should_poll = false;
        int timeout_ms = 0;
        
        if (poll_cycles_since_events < 5) {
            // Recent activity - poll aggressively
            should_poll = (time_since_last_poll >= MIN_POLL_INTERVAL_US);
            timeout_ms = 0;
        } else if (poll_cycles_since_events < 50) {
            // Moderate activity - poll less frequently
            should_poll = (time_since_last_poll >= MIN_POLL_INTERVAL_US * 2);
            timeout_ms = 1;
        } else {
            // Low activity - poll infrequently
            should_poll = (time_since_last_poll >= MAX_POLL_INTERVAL_US);
            timeout_ms = 5;
        }
        
        int num_events = 0;
        if (should_poll) {
            num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, timeout_ms);
            last_poll_time = current_time;
            
            if (num_events > 0) {
                poll_cycles_since_events = 0;
                
                // OPTIMIZATION: Efficient event processing
                ready_coroutines_set.clear();
                
                for (int i = 0; i < num_events; i++) {
                    int fd = events[i].data.fd;
                    
                    if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                        if (fd != server_fd) {
                            cleanup_fds.push_back(fd);
                            continue;
                        }
                    }
                    
                    auto it = fd_to_coro.find(fd);
                    if (it != fd_to_coro.end()) {
                        ready_coroutines_set.insert(it->second);
                    }
                }
                
                // Convert set to vector for iteration
                ready_coroutines_vec.clear();
                ready_coroutines_vec.reserve(ready_coroutines_set.size());
                for (hndl_t coro : ready_coroutines_set) {
                    ready_coroutines_vec.push_back(coro);
                }
                
                // Resume ready coroutines
                for (hndl_t coro : ready_coroutines_vec) {
                    if (is_coroutine_alive(coro)) {
                        resume(coro);
                    }
                }
            } else {
                poll_cycles_since_events++;
            }
        } else {
            poll_cycles_since_events++;
        }
        
        // OPTIMIZATION: Round-robin scheduler for fairness under high load
        round_robin_cycles++;
        if (round_robin_cycles >= 10 && !all_client_coroutines.empty()) {
            // Give some CPU time to connections that might not have events
            int rr_batch_size = std::min(50, (int)all_client_coroutines.size());
            
            for (int i = 0; i < rr_batch_size; i++) {
                if (round_robin_index >= all_client_coroutines.size()) {
                    round_robin_index = 0;
                }
                
                if (round_robin_index < all_client_coroutines.size()) {
                    hndl_t coro = all_client_coroutines[round_robin_index];
                    if (is_coroutine_alive(coro)) {
                        resume(coro);
                    }
                    round_robin_index++;
                }
            }
            round_robin_cycles = 0;
        }
        
        // Resume server occasionally
        static int server_resume_counter = 0;
        server_resume_counter++;
        if (server_resume_counter >= 20 && is_coroutine_alive(server_coro)) {
            resume(server_coro);
            server_resume_counter = 0;
        }
        
        // Batch cleanup
        static int cleanup_counter = 0;
        cleanup_counter++;
        if (cleanup_counter >= 100 || !cleanup_fds.empty()) {
            process_cleanup();
            cleanup_counter = 0;
        }
        
        // Time-based progress report every 10 seconds
        static auto last_stats_time = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(current_time - last_stats_time).count() >= 10) {
            std::cout << "Stats: " << all_client_coroutines.size() << " active connections, " 
                      << total_connections << " total, " << total_bytes << " bytes" << std::endl;
            last_stats_time = current_time;
        }
    }
    
    std::cout << "Shutting down..." << std::endl;
    process_cleanup();
    
    for (auto& pair : coro_to_fd) {
        hndl_t coro = pair.first;
        int fd = pair.second;
        if (is_coroutine_alive(coro)) {
            close(fd);
            hclose(coro);
        }
    }
    
    close(epoll_fd);
    std::cout << "Final: " << total_connections << " connections, " 
              << total_bytes << " bytes processed" << std::endl;
    return 0;
}