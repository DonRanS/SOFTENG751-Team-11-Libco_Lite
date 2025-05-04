#ifndef H_COROUTINE_H_
#define H_COROUTINE_H_

#include <stdint.h>
#include <map>
class CoroutineScheduler 
{
    public:

        typedef uintptr_t (*CoFunc)(void* arg);

        enum Status
        {
            CORO_READY,        CORO_RUNNING,
            CORO_SUSPENDED,
            CORO_EXITED
        };

    public:
        explicit CoroutineScheduler(int stacksize = 1024);
        ~CoroutineScheduler();

        int DestroyCoroutine(int id);
        int CreateCoroutine(CoFunc func, void* arg);

        uintptr_t ResumeCoroutine(int id, uintptr_t y = 0);

        bool IsCoroutineAlive(int id) const;
        uintptr_t Yield(uintptr_t y = 0);
        int GetRunningCoroutine() const;
        void CleanupAbandonedCoroutines();
int GetCoroutineCount() const;
    private:
        CoroutineScheduler(const CoroutineScheduler&);
        CoroutineScheduler& operator=(const CoroutineScheduler&);
    private:

        class SchedulerImpl;
        SchedulerImpl* impl_;
};

#endif
