// This scheduler uses epoll to manage file descriptors and coroutines. It allows for efficient I/O operations in a coroutine-based environment.
// Linux and BSD-based systems only
// Created on 2025-05-06 14:00:00

// Tested on 2025-05-06 15:20:00
// change: yield timing squence
// event registration and unregistration
// switched from system socket to coroutine socket
// scheduler_epoll.h - Improved version

// 2025-05-08 00:55:00 refactored the code to use a hash set to track existing IDs
// Performance improvements and bug fixes. 
// refactored, event-driven. 
#pragma once

#include <sys/epoll.h>
#include <map>
#include <stdint.h>
#include <sys/socket.h>  // For socklen_t
#include "../include/libco-lite.h"

class EpollScheduler {
public:
    EpollScheduler();
    ~EpollScheduler();
    
    void RegisterEvent(int fd, uint32_t events, int coro_id, CoroutineScheduler* scheduler);
    void UnregisterEvent(int fd);
    bool ResumeCoroutine(int fd, uint32_t events);
    void Poll(int timeout_ms);
    
    void WaitForRead(int fd, int coro_id, CoroutineScheduler* scheduler);
    void WaitForWrite(int fd, int coro_id, CoroutineScheduler* scheduler);
    
private:
    int epoll_fd_ = -1;
    
    // Simpler data structure to store event data
    struct EventData {
        int coro_id;
        CoroutineScheduler* scheduler;
        uint32_t events;
    };
    
    std::map<int, EventData> fd_to_data_;
};

namespace co_socket_epoll {
    int co_accept(CoroutineScheduler& scheduler, EpollScheduler& epoll_scheduler, 
                 int sockfd, struct sockaddr* addr, socklen_t* addrlen);
    
    ssize_t co_recv(CoroutineScheduler& scheduler, EpollScheduler& epoll_scheduler,
                   int sockfd, void* buf, size_t len, int flags);
    
    ssize_t co_send(CoroutineScheduler& scheduler, EpollScheduler& epoll_scheduler,
                   int sockfd, const void* buf, size_t len, int flags);
    
    bool co_connect(CoroutineScheduler& scheduler, EpollScheduler& epoll_scheduler,
                   int sockfd, const struct sockaddr* addr, socklen_t addrlen);
}