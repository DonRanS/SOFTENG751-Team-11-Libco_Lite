#include "../include/libco-lite.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

// Timer utilities
double get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000.0) + (tv.tv_usec / 1000.0);
}

// Simple coroutine function that yields N times
uintptr_t yield_n_times(void* arg) {
    int n = *((int*)arg);
    CoroutineScheduler* scheduler = (CoroutineScheduler*)(((void**)arg)[1]);
    
    for (int i = 0; i < n; i++) {
        scheduler->Yield(i);
    }
    
    return n;
}

// Benchmark for creating and running N coroutines that each yield M times
void benchmark_create_yield(int num_coroutines, int yields_per_coroutine, int stack_size) {
    printf("Benchmark: %d coroutines, %d yields each, %d stack size\n", 
           num_coroutines, yields_per_coroutine, stack_size);
    
    // Create scheduler
    double start_time = get_time_ms();
    CoroutineScheduler scheduler(stack_size);
    double scheduler_creation_time = get_time_ms() - start_time;
    
    printf("  Scheduler creation time: %.3f ms\n", scheduler_creation_time);
    
    // Create coroutines
    int* coroutine_ids = (int*)malloc(num_coroutines * sizeof(int));
    void** args = (void**)malloc(num_coroutines * sizeof(void*));
    
    start_time = get_time_ms();
    for (int i = 0; i < num_coroutines; i++) {
        int* count = (int*)malloc(sizeof(int));
        *count = yields_per_coroutine;
        void** arg_pair = (void**)malloc(2 * sizeof(void*));
        arg_pair[0] = count;
        arg_pair[1] = &scheduler;
        args[i] = arg_pair;
        
        coroutine_ids[i] = scheduler.CreateCoroutine(yield_n_times, arg_pair);
    }
    double coroutine_creation_time = get_time_ms() - start_time;
    
    printf("  Coroutine creation time: %.3f ms (%.3f ms per coroutine)\n", 
           coroutine_creation_time, coroutine_creation_time / num_coroutines);
    
    // Run coroutines and time the resume/yield cycle
    start_time = get_time_ms();
    int total_resumes = 0;
    
    bool all_done = false;
    while (!all_done) {
        all_done = true;
        for (int i = 0; i < num_coroutines; i++) {
            if (scheduler.IsCoroutineAlive(coroutine_ids[i])) {
                scheduler.ResumeCoroutine(coroutine_ids[i]);
                total_resumes++;
                all_done = false;
            }
        }
    }
    
    double execution_time = get_time_ms() - start_time;
    double operations_per_sec = (total_resumes * 1000.0) / execution_time;
    
    printf("  Execution time: %.3f ms\n", execution_time);
    printf("  Total operations: %d resumes/yields\n", total_resumes);
    printf("  Operations per second: %.2f resume/yield cycles per second\n", operations_per_sec);
    
    // Clean up
    for (int i = 0; i < num_coroutines; i++) {
        free(((void**)args[i])[0]); // Free the count
        free(args[i]);              // Free the arg pair
    }
    free(args);
    free(coroutine_ids);
}

// Benchmark for context switching overhead
void benchmark_context_switching(int num_switches, int stack_size) {
    printf("Benchmark: Context switching overhead with %d switches, %d stack size\n", 
           num_switches, stack_size);
    
    CoroutineScheduler scheduler(stack_size);
    
    // Coroutine that just yields back a value
    int* bounce_count = (int*)malloc(sizeof(int));
    *bounce_count = num_switches;
    void** arg_pair = (void**)malloc(2 * sizeof(void*));
    arg_pair[0] = bounce_count;
    arg_pair[1] = &scheduler;
    
    int coroutine_id = scheduler.CreateCoroutine(yield_n_times, arg_pair);
    
    double start_time = get_time_ms();
    
    for (int i = 0; i < num_switches; i++) {
        scheduler.ResumeCoroutine(coroutine_id, i);
    }
    
    double execution_time = get_time_ms() - start_time;
    double switches_per_sec = (num_switches * 1000.0) / execution_time;
    double time_per_switch = execution_time / num_switches;
    
    printf("  Total time: %.3f ms\n", execution_time);
    printf("  Context switches per second: %.2f\n", switches_per_sec);
    printf("  Time per context switch: %.3f ms\n", time_per_switch);
    
    free(arg_pair[0]);
    free(arg_pair);
}

// Benchmark memory usage
void benchmark_memory_usage(int num_coroutines, int stack_size) {
    printf("Benchmark: Memory usage with %d coroutines, %d stack size\n", 
           num_coroutines, stack_size);
    
    CoroutineScheduler scheduler(stack_size);
    int* coroutine_ids = (int*)malloc(num_coroutines * sizeof(int));
    void** args = (void**)malloc(num_coroutines * sizeof(void*));
    
    // First measure baseline memory usage
    // Note: In a real benchmark, you would use platform-specific APIs to measure memory usage
    // This is just a placeholder for illustration
    printf("  Note: This benchmark requires platform-specific memory measurement tools\n");
    
    for (int i = 0; i < num_coroutines; i++) {
        int* count = (int*)malloc(sizeof(int));
        *count = 1; // Just one yield
        void** arg_pair = (void**)malloc(2 * sizeof(void*));
        arg_pair[0] = count;
        arg_pair[1] = &scheduler;
        args[i] = arg_pair;
        
        coroutine_ids[i] = scheduler.CreateCoroutine(yield_n_times, arg_pair);
    }
    
    printf("  Each coroutine should use approximately %d bytes for its stack\n", stack_size);
    printf("  Total expected stack memory: %d bytes\n", num_coroutines * stack_size);
    printf("  Plus overhead for coroutine structures\n");
    
    // Clean up
    for (int i = 0; i < num_coroutines; i++) {
        free(((void**)args[i])[0]);
        free(args[i]);
    }
    free(args);
    free(coroutine_ids);
}

int main() {
    printf("Coroutine Scheduler Benchmark\n");
    printf("============================\n\n");
    
    // Test different stack sizes
    int stack_sizes[] = {1024, 4096, 16384};
    for (int i = 0; i < 3; i++) {
        benchmark_create_yield(100, 10, stack_sizes[i]);
        printf("\n");
    }
    
    // Context switching overhead
    benchmark_context_switching(10000, 4096);
    printf("\n");
    
    // Memory usage
    benchmark_memory_usage(1000, 4096);
    printf("\n");
    
    return 0;
}
