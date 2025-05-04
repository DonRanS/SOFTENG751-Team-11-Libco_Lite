// co_socket.h  
// 2025-05-05: Initial version, not tested yet

// Coroutine-friendly socket operations
// This file provides coroutine-friendly socket operations for TCP connections.
// It includes functions for creating sockets, connecting, accepting, sending, and receiving data in a non-blocking manner.
// For Linux and macOS platforms only.
#ifndef CO_SOCKET_H
#define CO_SOCKET_H

#include "../include/libco-lite.h"
#include "../include/socket.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

namespace co_socket {

// Set socket to non-blocking mode
inline int set_nonblocking(int sockfd) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(sockfd, F_SETFL, flags | O_NONBLOCKING);
}

// Create a TCP socket
inline int create_tcp_socket() {
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
inline bool co_connect(CoroutineScheduler& scheduler, int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
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
inline int co_accept(CoroutineScheduler& scheduler, int sockfd, struct sockaddr* addr, socklen_t* addrlen) {
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
inline ssize_t co_send(CoroutineScheduler& scheduler, int sockfd, const void* buf, size_t len, int flags) {
    size_t total_sent = 0;
    const char* buffer = static_cast<const char*>(buf);
    
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
inline ssize_t co_recv(CoroutineScheduler& scheduler, int sockfd, void* buf, size_t len, int flags) {
    while (true) {
        ssize_t received = recv(sockfd, buf, len, flags | MSG_DONTWAIT);
        
        if (received >= 0) {
            // Data received (or connection closed with 0 bytes)
            return received;
        }
        
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            // Real error occurred
            return -1;
        }
        
        // Would block, yield to other coroutines
        scheduler.Yield(0);
    }
}

// Helper to create and bind a server socket
inline int create_server_socket(const char* ip, int port) {
    int sockfd = create_tcp_socket();
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

#endif // CO_SOCKET_H