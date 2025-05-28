#ifndef SOCKET_WRAPPERS_H
#define SOCKET_WRAPPERS_H
#include "../include/libco-lite.h"
#ifdef EPOLL
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>  // For int64_t
#include <atomic>
#include <fcntl.h>
// Make a socket non-blocking
int set_nonblocking(int fd);

// Connect with timeout
int socket_connect(int fd, const struct sockaddr *addr, socklen_t addrlen, int64_t deadline);

// Accept with timeout
int socket_accept(int fd, struct sockaddr *addr, socklen_t *addrlen, int64_t deadline);

// Send with timeout
ssize_t socket_send(int fd, const void *buf, size_t len, int64_t deadline);

// Receive with timeout
ssize_t socket_recv(int fd, void *buf, size_t len, int64_t deadline);

#endif // EPOLL
#endif // SOCKET_WRAPPERS_H