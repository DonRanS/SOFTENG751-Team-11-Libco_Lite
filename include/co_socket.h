// 2025-05-05: Initial version, not tested yet
// 2025-05-06: Tested. Functions are working as expected.
// 2025-05-06 16:00:00 Passed test on Ubuntu 22.04 LTS; Incoporated epoll into the test

#include <sys/socket.h>
#include "./libco-lite.h"
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

namespace co_socket {

// Set socket to non-blocking mode
int set_nonblocking(int sockfd);

// Create a TCP socket
int create_tcp_socket();

// Coroutine-friendly connect
bool co_connect(CoroutineScheduler& scheduler, int sockfd, const struct sockaddr* addr, socklen_t addrlen);

// Coroutine-friendly accept
int co_accept(CoroutineScheduler& scheduler, int sockfd, struct sockaddr* addr, socklen_t* addrlen);

// Coroutine-friendly send
ssize_t co_send(CoroutineScheduler& scheduler, int sockfd, const void* buf, size_t len, int flags);

// Coroutine-friendly recv
ssize_t co_recv(CoroutineScheduler& scheduler, int sockfd, void* buf, size_t len, int flags);

// Helper to create and bind a server socket
int create_server_socket(const char* ip, int port);

} // namespace co_socket

