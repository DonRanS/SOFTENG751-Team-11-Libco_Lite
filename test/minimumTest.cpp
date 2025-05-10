// minimumTest.cpp - Simplified test program

#include "../include/libco-lite.h"
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <cerrno>
#include <stdlib.h>
#include <iostream>
#include <memory>
#include <cstring>
#include <cstdio>
#include <vector>
// Global variable for testing
static int test_value = 0;

// Simple coroutine function
void simple_coroutine(void* arg) {
    int* value = (int*)arg;
    printf("Coroutine started with value: %d\n", *value);
    
    // Increment value
    (*value)++;
    
    // Yield CPU
    printf("Coroutine yielding with value: %d\n", *value);
    yield();
    
    // Continue after resume
    (*value)++;
    printf("Coroutine finished with value: %d\n", *value);
}

// Nested function call
void nested_function(void* arg) {
    printf("Nested function - before yield\n");
    yield();
    printf("Nested function - after yield\n");
}

// Nested coroutine
void nested_coroutine(void* arg) {
    printf("Nested coroutine started\n");
    
    // Call another function, test yield in deep call
    nested_function(NULL);
    
    printf("Nested coroutine finished\n");
}

void channel_test(void* arg) {
    printf("Channel test started\n");
    
    // Create channel
    hndl_t chv[2];
    int rc = chmake(chv);
    assert(rc == 0);
    printf("Channel created: handles %d, %d\n", chv[0], chv[1]);
    
    // Test sending and receiving in the same coroutine
    const char* msg = "Hello through channel";
    printf("Sending message: '%s'\n", msg);
    
    rc = chsend(chv[0], msg, strlen(msg) + 1, -1);
    if (rc < 0) {
        printf("Failed to send: error %d\n", errno);
    } else {
        printf("Message sent\n");
        
        // Receive message
        char buffer[64] = {0};
        rc = chrecv(chv[1], buffer, sizeof(buffer), -1);
        if (rc < 0) {
            printf("Failed to receive: error %d\n", errno);
        } else {
            printf("Received message: '%s'\n", buffer);
            assert(strcmp(buffer, msg) == 0);
        }
    }
    
    // Close channel - add debug information
    printf("Closing channel handle %d\n", chv[0]);
    hclose(chv[0]);
    printf("Closing channel handle %d\n", chv[1]);
    hclose(chv[1]);
    
    printf("Channel test finished\n");
}

int main() {
    printf("=== Minimal Test for libco-lite ===\n\n");
    
    printf("--- Test 1: Simple Coroutine ---\n");
    test_value = 10;
    hndl_t coro_h = go(simple_coroutine, &test_value);
    assert(coro_h >= 0);
    
    // Resume coroutine
    printf("Main: Resuming coroutine first time\n");
    resume(coro_h, 0);
    
    // Check if value increased
    printf("Main: Value after first resume: %d\n", test_value);
    assert(test_value == 11);
    
    // Resume coroutine again
    printf("Main: Resuming coroutine second time\n");
    resume(coro_h, 0);
    
    // Check if value increased again
    printf("Main: Value after second resume: %d\n", test_value);
    assert(test_value == 12);
    
    // Close coroutine handle
    hclose(coro_h);
    printf("Test 1 completed successfully\n\n");
    
    printf("--- Test 2: Nested Coroutine ---\n");
    hndl_t nested_h = go(nested_coroutine, NULL);
    assert(nested_h >= 0);
    
    // Resume coroutine
    printf("Main: Resuming nested coroutine first time\n");
    resume(nested_h, 0);
    
    // Resume coroutine again
    printf("Main: Resuming nested coroutine second time\n");
    resume(nested_h, 0);
    
    // Close coroutine handle
    hclose(nested_h);
    printf("Test 2 completed successfully\n\n");
    
   printf("--- Test 3: Channel Communication ---\n");
hndl_t channel_h = go(channel_test, NULL);
assert(channel_h >= 0);
printf("Channel test coroutine created with handle: %d\n", channel_h);

// Loop to resume coroutine until complete
while (is_coroutine_alive(channel_h)) {
    printf("Resuming channel test coroutine\n");
    resume(channel_h, 0);
    printf("Channel test coroutine resumed\n");
}

// Close coroutine handle
printf("Closing channel test coroutine handle\n");
hclose(channel_h);
printf("Test 3 completed successfully\n\n");
    printf("=== All tests completed successfully ===\n");
    return 0;
}