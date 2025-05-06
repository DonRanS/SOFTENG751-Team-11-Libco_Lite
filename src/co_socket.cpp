// 2025-05-05 02:21:00 Initial version, not tested yet

// Coroutine-friendly socket operations
// This file provides coroutine-friendly socket operations for TCP connections.
// It includes functions for creating sockets, connecting, accepting, sending, and receiving data in a non-blocking manner.
// For Linux and macOS platforms only.

//  2025-05-05 21:21:00 Tested. Functions are working as expected.

// 2025-05-06 16:00:00 Added preprocessor directives for verbose output and debugging.
#include "../include/co_socket.h"
#include <sys/select.h>  // For select() and fd_set
#include <iostream>

// Uncomment the following line to enable verbose output for debugging
//# define VERBOSE


namespace co_socket {

// Set socket to non-blocking mode
int set_nonblocking(int sockfd) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
}

// Create a TCP socket
int create_tcp_socket() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return -1;
    
    // Set to non-blocking
    if (set_nonblocking(sockfd) < 0) {
        close(sockfd);
        return -1;
    }
    
    // Enable address reuse
    int reuse = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        close(sockfd);
        return -1;
    }
    
    return sockfd;
}

// Coroutine-friendly connect
bool co_connect(CoroutineScheduler& scheduler, int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
    // Attempt non-blocking connect
    int ret = connect(sockfd, addr, addrlen);
    
    if (ret == 0) {
        // Connected immediately
        return true;
    }
    
    if (errno != EINPROGRESS) {
        // Real error occurred
        return false;
    }
    
    // Connect is in progress, we need to wait
    while (true) {
        fd_set wset, eset;
        FD_ZERO(&wset);
        FD_ZERO(&eset);
        FD_SET(sockfd, &wset);
        FD_SET(sockfd, &eset);
        
        // Check if socket is writable (zero timeout for non-blocking check)
        struct timeval tv = {0, 0};
        ret = select(sockfd + 1, NULL, &wset, &eset, &tv);
        
        if (ret > 0) {
            // Socket became writable or has an error
            if (FD_ISSET(sockfd, &eset)) {
                // Error occurred
                return false;
            }
            
            if (FD_ISSET(sockfd, &wset)) {
                // Socket is writable, check for connection error
                int error = 0;
                socklen_t len = sizeof(error);
                if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error) {
                    // Connection failed
                    return false;
                }
                return true;  // Connected successfully
            }
        } else if (ret < 0 && errno != EINTR) {
            // Select error
            return false;
        }
        
        // Not ready yet, yield to other coroutines
        scheduler.Yield(0);
    }
}

// Coroutine-friendly accept
int co_accept(CoroutineScheduler& scheduler, int sockfd, struct sockaddr* addr, socklen_t* addrlen) {
    while (true) {
        int client_fd = accept(sockfd, addr, addrlen);
        
        if (client_fd >= 0) {
            // Successfully accepted a connection
            set_nonblocking(client_fd);
            return client_fd;
        }
        
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            // Real error occurred
            return -1;
        }
        
        // No connections available, yield and try again later
        scheduler.Yield(0);
    }
}

// Coroutine-friendly send
ssize_t co_send(CoroutineScheduler& scheduler, int sockfd, const void* buf, size_t len, int flags) {
    size_t total_sent = 0;
    const char* buffer = static_cast<const char*>(buf);
  //  std::cout << "Sending/Receiving data, size: " << len << std::endl;

    while (total_sent < len) {
        ssize_t sent = send(sockfd, buffer + total_sent, len - total_sent, flags | MSG_DONTWAIT);
        
        if (sent > 0) {
            total_sent += sent;
        } else if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Would block, yield to other coroutines
                scheduler.Yield(0);
            } else {
                // Real error occurred
                return -1;
            }
        }
    }
    
    return total_sent;
}

// Coroutine-friendly recv
ssize_t co_recv(CoroutineScheduler& scheduler, int sockfd, void* buf, size_t len, int flags) {
   #ifdef VERBOSE
    std::cout << "co_recv: Starting to receive data on fd " << sockfd << std::endl;
    #endif
    while (true) {
        // Try to receive data
        ssize_t received = recv(sockfd, buf, len, flags | MSG_DONTWAIT);
        
        if (received > 0) {
            // Successfully received data
            #ifdef VERBOSE

            std::cout << "co_recv: Received " << received << " bytes on fd " << sockfd << std::endl;
            #endif

            return received;
        } 
        
        if (received == 0) {
            #ifdef VERBOSE

            std::cout << "co_recv: Connection closed by peer on fd " << sockfd << std::endl;
            #endif
            return 0;
        }
        
        // Error occurred
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            // Real error, not just "would block"
            std::cerr << "co_recv: Error on fd " << sockfd << ": " << strerror(errno) << std::endl;
            return -1;
        }
        
        // Would block - use select to wait for readiness
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        
        // Check if data is available without blocking
        struct timeval tv = {0, 0};
        int ready = select(sockfd + 1, &readfds, NULL, NULL, &tv);
        
        if (ready > 0) {
            // Socket is ready, try receiving again immediately
            #ifdef VERBOSE
            std::cout << "co_recv: Socket ready, re-trying" << std::endl;
            #endif
            continue;
        }
        
        // Socket not ready, yield and try again later
       // std::cout << "co_recv: Socket would block, yielding coroutine" << std::endl;
        scheduler.Yield(0);
    }
}

// Helper to create and bind a server socket
int create_server_socket(const char* ip, int port) {
    // Change this line:
    // int sockfd = create_tcp_socket();
    // To this:
    int sockfd = co_socket::create_tcp_socket();
    
    if (sockfd < 0) return -1;
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (ip) {
        if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
            close(sockfd);
            return -1;
        }
    } else {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    
    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        return -1;
    }
    
    return sockfd;
}

} // namespace co_socket