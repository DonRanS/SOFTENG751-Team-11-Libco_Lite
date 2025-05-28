
#include "../include/libco-lite.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

const int SWITCH_COUNT = 50000000; 
uint64_t now_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

static int g_counter = 0;
static hndl_t g_coro1 = -1;
static hndl_t g_coro2 = -1;
static uint64_t g_start_time = 0;
static bool g_force_exit = false;

void ping_coroutine(void* arg) {
    // printf("Ping coroutine started\n");
    
    while (g_counter < SWITCH_COUNT && !g_force_exit) {
        g_counter++;
        resume(g_coro2,0);
        //g_counter = g_counter + 2;
        
        // if (g_counter % 100000 == 0) {
        //     // printf("Completed switches: %d\n", g_counter);
        // }
        
        // if (is_coroutine_alive(g_coro2)) {
        //     resume(g_coro2, 0);
        // } else {
        //     // printf("Ping: Pong coroutine is not alive, exiting\n");
        //     break;
        // }
    }
    
    // printf("Ping coroutine finished, counter=%d\n", g_counter);
    
    // uint64_t end_time = now_us();
    // uint64_t elapsed_us = end_time - g_start_time;
    
    // double seconds = elapsed_us / 1000000.0;
    // double switches_per_sec = g_counter / seconds;
    // double ns_per_switch = (elapsed_us * 1000.0) / g_counter;
    
    // printf("\nContext Switch Test Results:\n");
    // printf("Total context switches: %d\n", g_counter);
    // printf("Total time: %.3f seconds\n", seconds);
    // printf("Context switches per second: %.2f\n", switches_per_sec);
    // printf("Time per context switch: %.2f nanoseconds\n", ns_per_switch);
}

void pong_coroutine(void* arg) {
    // printf("Pong coroutine started\n");
    
    while (g_counter < SWITCH_COUNT && !g_force_exit) {
        resume(g_coro1,0);
        // if (is_coroutine_alive(g_coro1)) {
        //     resume(g_coro1, 0);
        // } else {
        //     // printf("Pong: Ping coroutine is not alive, exiting\n");
        //     break;
        // }
    }
    
    // printf("Pong coroutine finished, counter=%d\n", g_counter);
}

int main() {
    printf("=== Minimal Context Switch Test ===\n");
    printf("Testing %d cooperative context switches\n", SWITCH_COUNT);
 
    g_counter = 0;
    g_force_exit = false;

    g_coro1 = go(ping_coroutine, NULL);
    if (g_coro1 < 0) {
        printf("Failed to create ping coroutine\n");
        return 1;
    }
    
    g_coro2 = go(pong_coroutine, NULL);
    if (g_coro2 < 0) {
        printf("Failed to create pong coroutine\n");
        hclose(g_coro1);
        return 1;
    }
    
    printf("Created ping coroutine (handle %d) and pong coroutine (handle %d)\n", 
           g_coro1, g_coro2);
    
    g_start_time = now_us();
    
    // 
    printf("Starting context switch test...\n");
    resume(g_coro1, 0);
    
    int prev_counter = 0;
    int stall_count = 0;
    
    while (g_counter < SWITCH_COUNT) {
        usleep(100000); // 100ms
        
        // // stuck check
        // if (g_counter == prev_counter) {
        //     stall_count++;
        //     printf("WARNING: No progress for %d checks, counter=%d\n", 
        //            stall_count, g_counter);
            
        //     if (stall_count % 5 == 0) {
        //         printf("Attempting to resume ping coroutine...\n");
        //         if (is_coroutine_alive(g_coro1)) {
        //             resume(g_coro1, 0);
        //         }
                
        //         printf("Attempting to resume pong coroutine...\n");
        //         if (is_coroutine_alive(g_coro2)) {
        //             resume(g_coro2, 0);
        //         }
        //     }
            
        //     if (stall_count > 30) {
        //         printf("Stalled for too long, forcing exit\n");
        //         g_force_exit = true;
        //         break;
        //     }
        // } else {
        //     stall_count = 0;
        //     prev_counter = g_counter;
        // }
    }
    
    printf("Main: Test completed or timed out, waiting for cleanup...\n");
    sleep(1);
    
    printf("Main: Closing coroutine handles\n");
    if (is_coroutine_alive(g_coro1)) {
        hclose(g_coro1);
    }
    if (is_coroutine_alive(g_coro2)) {
        hclose(g_coro2);
    }
    
   // if (g_force_exit) {
        uint64_t end_time = now_us();
        uint64_t elapsed_us = end_time - g_start_time;

        int g_counter_total = g_counter * 2;
        
        double seconds = elapsed_us / 1000000.0;
        double switches_per_sec = g_counter_total  / seconds;
        double ns_per_switch = (elapsed_us * 1000.0) / g_counter_total;
        
        printf("\nContext Switch Test Results (from main):\n");
        printf("Total context switches: %d\n", g_counter);
        printf("Total time: %.3f seconds\n", seconds);
        printf("Context switches per second: %.2f\n", switches_per_sec);
        printf("Time per context switch: %.2f nanoseconds\n", ns_per_switch);
   // }
    
    printf("Main: Test completed\n");
    return 0;
}