// Impl of EpollScheduler and coroutine socket functions
// Linux and BSD-based systems only
// Created on 2025-05-06 14:00:00
// Tested on 2025-05-06 15:20:00
// Improved EpollScheduler implementation
// Linux and BSD-based systems only

// Improved EpollScheduler implementation
// Linux and BSD-based systems only

// 2025-05-08 00:55:00 refactored the code to use a hash set to track existing IDs

#include "../include/scheduler_epoll.h"
#include "../include/co_socket.h"  // Add this to get access to set_nonblocking
#include <unistd.h>
#include <iostream>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>  // For socklen_t, accept, and other socket functions
#include <sys/types.h>   // For additional type definitions
#include <fcntl.h>       // For fcntl function and flags

EpollScheduler::EpollScheduler() {
    // Create epoll instance
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }
}

EpollScheduler::~EpollScheduler() {
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
    }
}

void EpollScheduler::RegisterEvent(int fd, uint32_t events, int coro_id, CoroutineScheduler* scheduler) {
    struct epoll_event ev;
    ev.events = events;  // No edge-triggered mode by default
    ev.data.fd = fd;

    // Store coroutine info with the fd
    EventData event_data = {coro_id, scheduler, events};

    auto it = fd_to_data_.find(fd);
    if (it == fd_to_data_.end()) {
        // New fd registration
        fd_to_data_[fd] = event_data;
        
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
            perror("epoll_ctl: EPOLL_CTL_ADD");
            exit(EXIT_FAILURE);
        }
    } else {
        // Update existing fd with new event mask
        it->second = event_data;
        
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) == -1) {
            perror("epoll_ctl: EPOLL_CTL_MOD");
            exit(EXIT_FAILURE);
        }
    }
}

void EpollScheduler::UnregisterEvent(int fd) {
    auto it = fd_to_data_.find(fd);
    
    if (it == fd_to_data_.end()) {
        // Already unregistered, nothing to do
        return;
    }

    // Remove from epoll
    struct epoll_event ev;  // Dummy event for older kernels
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, &ev) == -1) {
        perror("epoll_ctl: EPOLL_CTL_DEL");
    }

    // Remove from map
    fd_to_data_.erase(it);
}

bool EpollScheduler::ResumeCoroutine(int fd, uint32_t events) {
    auto it = fd_to_data_.find(fd);
    if (it == fd_to_data_.end()) {
        return false;
    }

    // Check if the requested event matches our interest
    if (it->second.events & events) {
        CoroutineScheduler* scheduler = it->second.scheduler;
        int coro_id = it->second.coro_id;
        
        if (scheduler->IsCoroutineAlive(coro_id)) {
            scheduler->ResumeCoroutine(coro_id);
            return true;
        }
    }
    
    return false;
}

void EpollScheduler::Poll(int timeout_ms) {
    const int MAX_EVENTS = 8192;
    struct epoll_event events[MAX_EVENTS];
    
    int nfds = epoll_wait(epoll_fd_, events, MAX_EVENTS, timeout_ms);
    if (nfds < 0) {
        if (errno != EINTR) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }
        return;
    }
    
    for (int i = 0; i < nfds; i++) {
        int fd = events[i].data.fd;
        uint32_t revents = events[i].events;
        
        ResumeCoroutine(fd, revents);
    }
}

void EpollScheduler::WaitForRead(int fd, int coro_id, CoroutineScheduler* scheduler) {
    RegisterEvent(fd, EPOLLIN, coro_id, scheduler);
}

void EpollScheduler::WaitForWrite(int fd, int coro_id, CoroutineScheduler* scheduler) {
    RegisterEvent(fd, EPOLLOUT, coro_id, scheduler);
}

namespace co_socket_epoll {

int co_accept(CoroutineScheduler& scheduler, EpollScheduler& epoll_scheduler, 
              int sockfd, struct sockaddr* addr, socklen_t* addrlen) {
    while (true) {
        int client_fd = accept(sockfd, addr, addrlen);
        
        if (client_fd >= 0) {
            // Successfully accepted a connection
            int flags = fcntl(client_fd, F_GETFL, 0);
            fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
            return client_fd;
        }
        
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            // Real error occurred
            return -1;
        }
        
        // Register for read events and yield
        int current_coro = scheduler.GetRunningCoroutine();
        epoll_scheduler.WaitForRead(sockfd, current_coro, &scheduler);
        scheduler.Yield();
    }
}

ssize_t co_recv(CoroutineScheduler& scheduler, EpollScheduler& epoll_scheduler,
                int sockfd, void* buf, size_t len, int flags) {
    while (true) {
        ssize_t received = recv(sockfd, buf, len, flags | MSG_DONTWAIT);
        
        if (received >= 0) {
            // Data received or connection closed
            return received;
        }
        
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            // Real error occurred
            return -1;
        }
        
        // Register for read events and yield
        int current_coro = scheduler.GetRunningCoroutine();
        epoll_scheduler.WaitForRead(sockfd, current_coro, &scheduler);
        scheduler.Yield();
    }
}

ssize_t co_send(CoroutineScheduler& scheduler, EpollScheduler& epoll_scheduler,
                int sockfd, const void* buf, size_t len, int flags) {
    size_t total_sent = 0;
    const char* buffer = static_cast<const char*>(buf);
    
    while (total_sent < len) {
        ssize_t sent = send(sockfd, buffer + total_sent, len - total_sent, flags | MSG_DONTWAIT);
        
        if (sent > 0) {
            total_sent += sent;
        } else if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Register for write events and yield
                int current_coro = scheduler.GetRunningCoroutine();
                epoll_scheduler.WaitForWrite(sockfd, current_coro, &scheduler);
                scheduler.Yield();
            } else {
                // Real error occurred
                return -1;
            }
        }
    }
    
    return total_sent;
}

bool co_connect(CoroutineScheduler& scheduler, EpollScheduler& epoll_scheduler,
                int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
    // Make sure the socket is non-blocking
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    
    int ret = connect(sockfd, addr, addrlen);
    
    if (ret == 0) {
        // Connected immediately
        return true;
    }
    
    if (errno != EINPROGRESS) {
        // Real error occurred
        return false;
    }
    
    // Register for write events and yield
    int current_coro = scheduler.GetRunningCoroutine();
    epoll_scheduler.WaitForWrite(sockfd, current_coro, &scheduler);
    scheduler.Yield();
    
    // Check if connection was successful
    int error = 0;
    socklen_t len = sizeof(error);
    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error) {
        // Connection failed
        epoll_scheduler.UnregisterEvent(sockfd);
        return false;
    }
    
    return true;
}

} // namespace co_socket_epoll