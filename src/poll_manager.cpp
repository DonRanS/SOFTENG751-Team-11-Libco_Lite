#include "../include/libco-lite.h"
#ifdef EPOLL
#include <sys/epoll.h>
#include <unordered_map>
#include <mutex>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <iostream>
#include "../include/socket_wrapper.h"
#include <vector>
std::atomic<bool> g_poller_running(false);

// Global poll manager instance
static PollManager* g_poll_manager = nullptr;

PollManager& get_poll_manager() {
    if (!g_poll_manager) {
        g_poll_manager = new PollManager();
    }
    return *g_poll_manager;
}

PollManager::PollManager() {
    // Create epoll instance
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        std::cerr << "Failed to create epoll instance: " << strerror(errno) << std::endl;
    }
}

PollManager::~PollManager() {
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
}

int PollManager::add(int fd, int events, hndl_t coroutine) {
    printf("---> PollManager::add called with fd=%d, events=%d, coroutine=%d\n", fd, events, coroutine);
    printf("---> PollManager::add FDW_IN=%d, FDW_OUT=%d, FDW_ERR=%d\n", FDW_IN, FDW_OUT, FDW_ERR);
    
    if (fd < 0 || epoll_fd_ < 0) {
        printf("---> PollManager::add invalid fd or epoll_fd_\n");
        errno = EINVAL;
        return -1;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // convert our event flags to epoll flags
    uint32_t epoll_events = 0;
    if (events & FDW_IN) {
        epoll_events |= EPOLLIN;
        printf("---> PollManager::add adding EPOLLIN\n");
    }
    if (events & FDW_OUT) {
        epoll_events |= EPOLLOUT;
        printf("---> PollManager::add adding EPOLLOUT\n");
    }
    if (events & FDW_ERR) {
        epoll_events |= EPOLLERR;
        printf("---> PollManager::add adding EPOLLERR\n");
    }
    
    // each event should be EPOLLONESHOT. once
    epoll_events |= EPOLLONESHOT;
    printf("---> PollManager::add adding EPOLLONESHOT = %u\n", EPOLLONESHOT);
    
    printf("---> PollManager::add converted events=%d to epoll_events=%u (0x%x)\n", 
           events, epoll_events, epoll_events);
    
    printf("---> PollManager::add final epoll_events=%u, includes EPOLLONESHOT: %s\n", 
           epoll_events, (epoll_events & EPOLLONESHOT) ? "YES" : "NO");
    
    // e poll register the fd with epoll
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev)); 
    ev.events = epoll_events;
    ev.data.fd = fd;
    
    int rc = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev);
    if (rc < 0) {
        printf("---> PollManager::add epoll_ctl ADD failed: %s\n", strerror(errno));
        if (errno == EEXIST) {
            // already exists, try to modify
            printf("---> PollManager::add file descriptor already exists, trying MOD\n");
            rc = epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
            if (rc < 0) {
                printf("---> PollManager::add epoll_ctl MOD failed: %s\n", strerror(errno));
                return -1;
            }
        } else {
            return -1;
        }
    }
    
    // store the coroutine handle associated with the fd
    fd_to_coro_[fd] = coroutine;
    printf("---> PollManager::add registered fd=%d with coroutine=%d, fd_to_coro_.size()=%zu\n", 
           fd, coroutine, fd_to_coro_.size());
    
    return 0;
}

int PollManager::mod(int fd, int events, hndl_t coroutine) {
    if (fd < 0 || epoll_fd_ < 0) {
        errno = EINVAL;
        return -1;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Convert our event flags to epoll flags
    uint32_t epoll_events = 0;
   if (events & FDW_IN) {
        epoll_events |= EPOLLIN;
        printf("---> PollManager::add adding EPOLLIN\n");
    }
    if (events & FDW_OUT) {
        epoll_events |= EPOLLOUT;
        printf("---> PollManager::add adding EPOLLOUT\n");
    }
    if (events & FDW_ERR) {
        epoll_events |= EPOLLERR;
        printf("---> PollManager::add adding EPOLLERR\n");
    }
    
    // Set EPOLLONESHOT to ensure only one notification per ready event
 epoll_events |= EPOLLONESHOT;
    printf("---> PollManager::add adding EPOLLONESHOT = %u\n", EPOLLONESHOT);
    printf("---> PollManager::add final epoll_events=%u (0x%x), includes EPOLLONESHOT: %s\n", 
           epoll_events, epoll_events, (epoll_events & EPOLLONESHOT) ? "YES" : "NO");    
    // Modify in epoll
    struct epoll_event ev;
    ev.events = epoll_events;
    ev.data.fd = fd;
    
    int rc = epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
    if (rc < 0) {
        return -1;
    }
    
    // Update the coroutine handle
    fd_to_coro_[fd] = coroutine;
    
    return 0;
}

int PollManager::del(int fd) {
    if (fd < 0 || epoll_fd_ < 0) {
        errno = EINVAL;
        return -1;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Remove from epoll
    struct epoll_event ev; // Dummy, older kernels require non-NULL
    int rc = epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, &ev);
    if (rc < 0 && errno != ENOENT && errno != EBADF) {
        return -1;
    }
    
    // Remove from our map
    fd_to_coro_.erase(fd);
    
    return 0;
}

int PollManager::poll(int timeout_ms) {
    static int poll_count = 0;
    poll_count++;
    printf("---> PollManager::poll called, count=%d\n", poll_count);
    
    if (epoll_fd_ < 0) {
        return 0; 
    }
    
    const int MAX_EVENTS = 64;
    struct epoll_event events[MAX_EVENTS];
    
    // wait for events
    int n = epoll_wait(epoll_fd_, events, MAX_EVENTS, timeout_ms);
    if (n > 0 || poll_count % 100 == 0) {
        printf("---> PollManager::poll epoll_wait returned %d events\n", n);
    }
    
    if (n < 0) {
        if (errno == EINTR) {
            return 0; 
        }
        return -1;
    }
    
    // collect coroutines to resume
    std::vector<hndl_t> coroutines_to_resume;
    
    {
        // use mutex to protect access to fd_to_coro_
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            auto it = fd_to_coro_.find(fd);
            
            if (it != fd_to_coro_.end()) {
                // record the coroutine to resume
                hndl_t coro = it->second;
                printf("---> PollManager::poll removing fd=%d with coroutine=%d from tracking\n", 
                       fd, coro);
                coroutines_to_resume.push_back(coro);
                fd_to_coro_.erase(it);
            }
        }
    } // unlock mutex
    
    // resume coroutines after unlocking
    for (hndl_t coro : coroutines_to_resume) {
        if (is_coroutine_alive(coro)) {
            printf("---> PollManager::poll resuming coroutine=%d\n", coro);
            resume(coro, 0);
        }
    }
    
    return n;
}

int fdwait_epoll(int fd, int events, int64_t deadline) {
    printf("---> fdwait_epoll called with fd=%d, events=%d\n", fd, events);
    printf("---> fdwait_epoll FDW_IN=%d, FDW_OUT=%d, FDW_ERR=4\n", FDW_IN, FDW_OUT, FDW_ERR);
    printf("---> fdwait_epoll checking for IN: %s\n", (events & FDW_IN) ? "YES" : "NO");
    printf("---> fdwait_epoll checking for OUT: %s\n", (events & FDW_OUT) ? "YES" : "NO");
    printf("---> fdwait_epoll checking for ERR: %s\n", (events & FDW_ERR) ? "YES" : "NO");
    
    // get current coroutine
    hndl_t coro = get_running_coroutine();
    printf("---> fdwait_epoll current coroutine: %d\n", coro);
    
    // ensure in coroutine context
    if (coro < 0) {
        printf("---> fdwait_epoll ERROR: not in coroutine context\n");
        errno = EPERM;
        return -1;
    }
    
    // register the fd with the poll manager
    PollManager& pm = get_poll_manager();
    printf("---> fdwait_epoll got poll manager, is_valid=%d\n", pm.is_valid());
    
    int rc = pm.add(fd, events, coro);
    printf("---> fdwait_epoll pm.add() returned: %d\n", rc);
    
    if (rc < 0) {
        printf("---> fdwait_epoll failed to add fd to poll manager: %s\n", strerror(errno));
        return -1;
    }
    
    // 
    int64_t timeout = -1;
    if (deadline >= 0) {
        timeout = deadline - now();
        printf("---> fdwait_epoll calculated timeout: %ld ms\n", timeout);
        if (timeout < 0) {
            //  timeout
            printf("---> fdwait_epoll already timed out\n");
            pm.del(fd);
            errno = ETIMEDOUT;
            return -1;
        }
    }
    
    printf("---> fdwait_epoll about to yield()\n");
    uintptr_t result = yield();
    printf("---> fdwait_epoll resumed after yield() with result %lu\n", result);
    
    // always clean up
    rc = pm.del(fd);
    printf("---> fdwait_epoll cleaned up fd=%d, result=%d\n", fd, rc);
    
    // timeou chcek
    if (deadline >= 0 && now() >= deadline) {
        printf("---> fdwait_epoll timed out after yield\n");
        errno = ETIMEDOUT;
        return -1;
    }
    
    return 0;
}


//a background thread that does polling 
class PollerThread {
public:
    PollerThread() 
        : running_(false), thread_(nullptr) {}
    
    ~PollerThread() {
        stop();
    }
    
    void start() {
        if (running_) return;
        
        running_ = true;
        thread_ = new std::thread([this]() {
            this->run();
        });
    }
    
    void stop() {
        if (!running_) return;
        
        running_ = false;
        if (thread_) {
            thread_->join();
            delete thread_;
            thread_ = nullptr;
        }
    }
    
private:
    void run() {
           printf("---> PollerThread::run started\n");
    g_poller_running = true;
        PollManager& pm = get_poll_manager();
        printf("Checking if poller thread is running: %s\n", 
           g_poller_running.load() ? "YES" : "NO");
        while (running_) {
            // Poll with timeout
            pm.poll(100); // 100ms timeout
        }
    }
    
    std::atomic<bool> running_;
    std::thread* thread_;
};

// Global poller thread
static PollerThread* g_poller_thread = nullptr;

// Initialize the poller thread
void init_poller_thread() {
    printf("---> init_poller_thread called\n");
    if (!g_poller_thread) {
        printf("---> Creating new poller thread\n");
        g_poller_thread = new PollerThread();
        printf("---> About to start poller thread\n");
        g_poller_thread->start();
        printf("---> Poller thread started\n");
    } else {
        printf("---> Poller thread already exists\n");
    }
}

// This should be called when your program starts
extern "C" {
   void __attribute__((constructor)) libco_init() {
    printf("---> libco_init constructor called\n");
    init_poller_thread();
    printf("---> libco_init constructor completed\n");
}
    
    void __attribute__((destructor)) libco_cleanup() {
        if (g_poller_thread) {
            delete g_poller_thread;
            g_poller_thread = nullptr;
        }
        
        if (g_poll_manager) {
            delete g_poll_manager;
            g_poll_manager = nullptr;
        }
    }
}
#endif // EPOLLs