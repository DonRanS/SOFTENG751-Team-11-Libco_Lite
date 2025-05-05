
// WARNING: This code is designed for Linux and Mac OS X(probably... I have no MAC device) only.
// It uses ucontext.h which is not available on Windows.
// Windows users should use other coroutine libraries like boost::coroutine or libco.
//TODO : add windows support?
//TODO: use epoll on linux and kqueue on mac os x to implement coroutine scheduler, windows can use IOCP
//Coroutine is designed for IO multiplexing, not for CPU multiplexing. So it is not a thread library. It is a user level thread library.
// All coroutines are running in the same thread. No thread-safe issues here.   


//Corutine is desined for IO intensive applications. So, next phase is to use epoll to maximize the performance of coroutine scheduler.
//We need to write a coroutine scheduler which can use epoll to multiplex IO events.
#include "../include/libco-lite.h"

#include <map>
#include <stdlib.h>
#include <assert.h>
#include <cstring>

#ifdef __linux__
// Include ucontext.h on Linux
#include <ucontext.h>
#elif defined(__APPLE__)
// On macOS, we need to define _XOPEN_SOURCE to use ucontext.h
#define _XOPEN_SOURCE
#include <ucontext.h>
// Alternative option: use sys/ucontext.h instead
// #include <sys/ucontext.h>
#else
#error "This code is designed for Linux and Mac OS X only. Other platforms are not supported."
#endif


using namespace std;

struct coroutine
{
    void* arg;

    int status;
    ucontext_t cxt;
    uintptr_t yield;
    CoroutineScheduler::CoFunc func;

    char stack[0];
};

class CoroutineScheduler::SchedulerImpl
{
    public:

        explicit SchedulerImpl(int stacksize);
        ~SchedulerImpl();

        int CreateCoroutine(CoroutineScheduler::CoFunc func, void* arg);
        int DestroyCoroutine(int id);

        uintptr_t Yield(uintptr_t);
        uintptr_t ResumeCoroutine(int id, uintptr_t);

        bool IsCoroutineAlive(int id) const;
	int GetRunningCoroutine() const;
	int GetCoroutineCount() const;
	void CleanupAbandonedCoroutines(); 

    private:

        SchedulerImpl(const SchedulerImpl&);
        SchedulerImpl& operator=(const SchedulerImpl&);

        static void Schedule(void* arg);

    private:

        int index_;
        int running_;
        const int stacksize_;

        ucontext_t mainContext_;
        map<int, coroutine*> id2routine_;
       // coroutine** routine_;		// unused
};

int CoroutineScheduler::SchedulerImpl::GetRunningCoroutine() const
{
    return running_;
}

int CoroutineScheduler::SchedulerImpl::GetCoroutineCount() const
{
    return id2routine_.size();
}
int CoroutineScheduler::GetRunningCoroutine() const
{
    return impl_->GetRunningCoroutine();
}

int CoroutineScheduler::GetCoroutineCount() const
{
    return impl_->GetCoroutineCount();
}

CoroutineScheduler::SchedulerImpl::SchedulerImpl(int stacksize)
    :index_(0), running_(-1), stacksize_(stacksize)
{
}

CoroutineScheduler::SchedulerImpl::~SchedulerImpl()
{
    map<int, coroutine*>::iterator it = id2routine_.begin();

    while (it != id2routine_.end())
    {
        if (it->second) 
        {
            free(it->second);
            it->second = NULL; // Avoid dangling pointer
        }
      //  it++; // Add iterator increment 
         ++it; // Avoid dangling pointer
    }
}

bool CoroutineScheduler::SchedulerImpl::IsCoroutineAlive(int id) const
{
    map<int, coroutine*>::const_iterator it = id2routine_.find(id);
    if (it == id2routine_.end()) return false;

    return it->second;
}

int CoroutineScheduler::SchedulerImpl::DestroyCoroutine(int id)
{
    coroutine* cor = id2routine_[id];
    if (!cor) return -1;

    free(cor);
    id2routine_.erase(id);
    return id;
}

// static function
void CoroutineScheduler::SchedulerImpl::Schedule(void* arg)
{
    assert(arg);
    SchedulerImpl* sched = (SchedulerImpl*) arg;

    int running = sched->running_;

    coroutine* cor = sched->id2routine_[running];
    assert(cor);

    cor->yield = cor->func(cor->arg);

    sched->running_ = -1;
    cor->status = CORO_EXITED;
}



uintptr_t CoroutineScheduler::SchedulerImpl::ResumeCoroutine(int id, uintptr_t y)
{
    // potential memory leak here, if coroutine is not destroyed after exit
    auto it = id2routine_.find(id);
    if (it == id2routine_.end()) return 0;
    coroutine * cor = it->second;
    // END of potential memory leak change.

  //  coroutine* cor = id2routine_[id];
    if (cor == NULL || cor->status == CORO_RUNNING) return 0;

    cor->yield = y;
    switch (cor->status)
    {
        case CORO_READY:
            {
                getcontext(&cor->cxt);

                cor->status = CORO_RUNNING;
                cor->cxt.uc_stack.ss_sp = cor->stack;
                cor->cxt.uc_stack.ss_size = stacksize_;
                // successor context. jump to mainContext_ when coroutine exits.
              //  cor->cxt.uc_stack.ss_flags = 0; // reset flags
                cor->cxt.uc_link = &mainContext_;

                running_ = id;
                // setup coroutine context
                makecontext(&cor->cxt, (void (*)())Schedule, 1, this);
                swapcontext(&mainContext_, &cor->cxt);
            }
            break;
        case CORO_SUSPENDED:
            {
                running_ = id;
                cor->status = CORO_RUNNING;
                swapcontext(&mainContext_, &cor->cxt);  // context switch to coroutine
            }
            break;
        default:
            assert(0);
    }

    uintptr_t ret = cor->yield;
    cor->yield = 0;

    if (running_ == -1 && cor->status == CORO_EXITED) {
    DestroyCoroutine(id);
    } 
    // shouldnt rearm if coroutine is exited
    //else if (cor->status == CORO_EXITED) {
    //    cor->status = CORO_READY;
    //}
    return ret;
}

uintptr_t CoroutineScheduler::SchedulerImpl::Yield(uintptr_t y)
{
    if (running_ < 0) return 0;

    int cur = running_;
    running_ = -1;

    coroutine* cor = id2routine_[cur];

    cor->yield = y;
    cor->status = CORO_SUSPENDED;

    swapcontext(&cor->cxt, &mainContext_);

    uintptr_t ret = cor->yield;
    cor->yield = 0;
    return ret;
}
int CoroutineScheduler::SchedulerImpl::CreateCoroutine(CoroutineScheduler::CoFunc func, void* arg)
{
    #ifdef __apple__
    // On macOS, we need to define _XOPEN_SOURCE to use ucontext.h
    coroutine* cor = (coroutine*)aligned_alloc(16, sizeof(coroutine) + stacksize_);
#else
    // On Linux, we can use malloc directly
    coroutine* cor = (coroutine*)malloc(sizeof(coroutine) + stacksize_);
#endif

    if (cor == NULL) return -1;
// allocate memory for coroutine
    // dont trust auto initialization, memset it to 0
    memset(cor, 0, sizeof(coroutine)); // initialize coroutine structure
    // end of change
    cor->arg  = arg;
    cor->func = func;
    cor->yield = 0;
    cor->status = CORO_READY;
  //  assert(cor!= NULL);
    int index = index_++;
    id2routine_[index] = cor;

    return index;
}
// New implementation to clean up abandoned coroutines
void CoroutineScheduler::SchedulerImpl::CleanupAbandonedCoroutines() 
{
    auto it = id2routine_.begin();
    while (it != id2routine_.end()) {
        if (it->second && it->second->status == CORO_EXITED) {
            free(it->second);
            it = id2routine_.erase(it);
        } else {
            ++it;
        }
    }
}



CoroutineScheduler::CoroutineScheduler(int stacksize)
    :impl_(new SchedulerImpl(stacksize))
{
    assert(impl_);
}   

CoroutineScheduler::~CoroutineScheduler()
{
    delete impl_;
}

int CoroutineScheduler::CreateCoroutine(CoFunc func, void* arg)
{
    return impl_->CreateCoroutine(func, arg);
}

int CoroutineScheduler::DestroyCoroutine(int id)
{
    return impl_->DestroyCoroutine(id);
}

// New interface to clean up abandoned coroutines
void CoroutineScheduler::CleanupAbandonedCoroutines()
{
    impl_->CleanupAbandonedCoroutines();
}

uintptr_t CoroutineScheduler::ResumeCoroutine(int id, uintptr_t y)
{
    return impl_->ResumeCoroutine(id, y);
}

uintptr_t CoroutineScheduler::Yield(uintptr_t y)
{
    return impl_->Yield(y);
}

bool CoroutineScheduler::IsCoroutineAlive(int id) const
{
    return impl_->IsCoroutineAlive(id);
}