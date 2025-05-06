// This scheduler uses epoll to manage file descriptors and coroutines. It allows for efficient I/O operations in a coroutine-based environment.
// Linux and BSD-based systems only
// Created on 2025-05-06 14:00:00

// Tested on 2025-05-06 15:45:00
// change: yield timing squence
// event registration and unregistration
// switched from system socket to coroutine socket
#ifndef SCHEDULER_EPOLL_H
#define SCHEDULER_EPOLL_H

#include "./libco-lite.h"
#include "./co_socket.h"
#include <sys/epoll.h>
#include <map>
#include <vector>
#include <utility>

class EpollScheduler {
public:
    EpollScheduler();
    ~EpollScheduler();

    // Register a file descriptor with epoll and associate it with a coroutine
    void RegisterEvent(int fd, uint32_t events, int coro_id, CoroutineScheduler* scheduler);

    // Unregister a file descriptor
    void UnregisterEvent(int fd);

    // Find and resume coroutine based on fd and event
    bool ResumeCoroutine(int fd, uint32_t events);

    // Poll for events and resume coroutines
    void Poll(int timeout_ms = -1);

    // Helper functions for common operations
    void WaitForRead(int fd, int coro_id, CoroutineScheduler* scheduler);
    void WaitForWrite(int fd, int coro_id, CoroutineScheduler* scheduler);

private:
    int epoll_fd_;
    std::map<int, std::vector<std::pair<uint32_t, std::pair<int, CoroutineScheduler*>>>> fd_to_coro_;
};

namespace co_socket_epoll {

// Epoll-based socket accept - integrates with your existing co_socket
int co_accept(CoroutineScheduler& scheduler, EpollScheduler& epoll_scheduler, 
              int sockfd, struct sockaddr* addr, socklen_t* addrlen);

// Epoll-based socket receive
ssize_t co_recv(CoroutineScheduler& scheduler, EpollScheduler& epoll_scheduler,
                int sockfd, void* buf, size_t len, int flags);

// Epoll-based socket send
ssize_t co_send(CoroutineScheduler& scheduler, EpollScheduler& epoll_scheduler,
                int sockfd, const void* buf, size_t len, int flags);

// Epoll-based socket connect
bool co_connect(CoroutineScheduler& scheduler, EpollScheduler& epoll_scheduler,
                int sockfd, const struct sockaddr* addr, socklen_t addrlen);

} // namespace co_socket_epoll

#endif // SCHEDULER_EPOLL_H