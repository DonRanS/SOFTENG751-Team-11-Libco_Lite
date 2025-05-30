// deprecated: This file is deprecated and will be removed in future versions.
// incompatible: This file is incompatible with the current version of libco-lite.(API changes)

// please use simpleBench.cpp instead.
#include "../include/libco-lite.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <atomic>
#include <vector>

const int NUM_COROUTINES = 10;
const int ITERATIONS_PER_CORO = 1000;

std::atomic<int> g_total_counts{0};
std::atomic<bool> g_continue_test{true};

uint64_t now_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}


void work_coroutine(void* arg) {
    int id = *(int*)arg;
    printf("Coroutine %d started\n", id);
    
    int local_count = 0;
    
    while (local_count < ITERATIONS_PER_CORO && g_continue_test) {
        local_count++;
        g_total_counts++;
        
        if (local_count % 100 == 0) {
            printf("Coroutine %d: completed %d iterations\n", id, local_count);
        }
        
        yield();
    }
    
    printf("Coroutine %d completed %d iterations\n", id, local_count);
    free(arg);
}

int main() {
    printf("=== Minimal Coroutine Benchmark ===\n");
    
    uint64_t start_time = now_ms();
    std::vector<hndl_t> handles;
    
    for (int i = 0; i < NUM_COROUTINES; i++) {
        int* id = (int*)malloc(sizeof(int));
        *id = i;
        
        hndl_t h = go(work_coroutine, id);
        if (h < 0) {
            printf("Failed to create coroutine %d\n", i);
            continue;
        }
        
        handles.push_back(h);
        printf("Created coroutine %d with handle %d\n", i, h);
        
        resume(h, 0);
    }
    
    printf("All coroutines created, starting execution loop\n");
    
    int iterations = 0;
    while (iterations < 1000) {  
        iterations++;
        
        // 
        if (iterations % 10 == 0) {
            uint64_t elapsed = now_ms() - start_time;
            printf("[%lu ms] Iteration %d: Total counts: %d\n", 
                   elapsed, iterations, g_total_counts.load());
        }
        
        // if all iterations are completed, exit
        if (g_total_counts >= NUM_COROUTINES * ITERATIONS_PER_CORO) {
            printf("All iterations completed\n");
            break;
        }
        
        bool all_completed = true;
        for (hndl_t h : handles) {
            if (is_coroutine_alive(h)) {
                all_completed = false;
                resume(h, 0);
            }
        }
        
        if (all_completed) {
            printf("All coroutines completed\n");
            break;
        }
        
        usleep(10000);  // 10ms
    }
    
    uint64_t end_time = now_ms();
    double elapsed_sec = (end_time - start_time) / 1000.0;
    int total_counts = g_total_counts.load();
    
    printf("\nBenchmark Results:\n");
    printf("Total time: %.3f seconds\n", elapsed_sec);
    printf("Total iterations: %d\n", total_counts);
    printf("Iterations per second: %.2f\n", total_counts / elapsed_sec);
    printf("Iterations per coroutine per second: %.2f\n", 
           (total_counts / elapsed_sec) / NUM_COROUTINES);
    
    g_continue_test = false;

    for (hndl_t h : handles) {
        if (is_coroutine_alive(h)) {
            printf("Closing coroutine handle %d\n", h);
            hclose(h);
        }
    }
    
    printf("Benchmark complete\n");
    return 0;
}