// Basic test for the coroutine library. Example of creating and running two coroutines that yield and resume each other.
// This test is designed to be simple and straightforward, focusing on the basic functionality of the coroutine library.

#include "../src/libco-lite.h"
#include <iostream>

using namespace std;

// Global scheduler pointer
CoroutineScheduler* sched = nullptr;

// Example coroutine function 1
uintptr_t func1(void* arg)
{
    uintptr_t ret;
    cout << "Function 1 started with arg: " << (intptr_t)arg << endl;
    
    // Yield and pass a message back to main
    ret = sched->Yield((uintptr_t)"Function 1 first yield");
    cout << "Function 1 resumed with: " << (const char*)ret << endl;
    
    // Yield again
    ret = sched->Yield((uintptr_t)"Function 1 second yield");
    cout << "Function 1 resumed with: " << (const char*)ret << endl;
    
    return (uintptr_t)"Function 1 completed";
}

// Example coroutine function 2
uintptr_t func2(void* arg)
{
    cout << "Function 2 started with arg: " << (intptr_t)arg << endl;
    
    // Yield and pass a message back to main
    const char* ret = (const char*)sched->Yield((uintptr_t)"Function 2 first yield");
    cout << "Function 2 resumed with: " << ret << endl;
    
    return (uintptr_t)"Function 2 completed";
}

// Main function
int main()
{
    // Create the scheduler
    sched = new CoroutineScheduler();
    
    cout << "Coroutine scheduler initialized" << endl;
    
    // Create two coroutines
    int co1 = sched->CreateCoroutine(func1, (void*)100);
    int co2 = sched->CreateCoroutine(func2, (void*)200);
    
    cout << "Created coroutine IDs: " << co1 << " and " << co2 << endl;
    
    // Run until all coroutines complete
    bool allDone = false;
    while (!allDone) {
        allDone = true;
        
        // Resume coroutine 1 if still active
        if (sched->IsCoroutineAlive(co1)) {
            allDone = false;
            const char* result = (const char*)sched->ResumeCoroutine(co1, (uintptr_t)"Resume message to function 1");
            cout << "Main received from coroutine 1: " << result << endl;
        }
        
        // Resume coroutine 2 if still active
        if (sched->IsCoroutineAlive(co2)) {
            allDone = false;
            const char* result = (const char*)sched->ResumeCoroutine(co2, (uintptr_t)"Resume message to function 2");
            cout << "Main received from coroutine 2: " << result << endl;
        }
    }
    
    cout << "All coroutines completed" << endl;
    
    // Clean up
    delete sched;
    
    return 0;
}
