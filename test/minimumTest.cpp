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

void sleeper(void* arg) {
    int timeout = *(int*)arg;
    printf(">>> coroutine will sleep for %d ms...\n", timeout);
    sleep_for(timeout);
    printf(">>> coroutine is waking up!\n");
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

    printf("--- Test 4: sleep_for(3000 ms) ---\n");
    int timeout = 3000;
    hndl_t sleep_h = go(sleeper, &timeout);
    assert(sleep_h >= 0);
    while (is_coroutine_alive(sleep_h)) {
        resume(sleep_h, 0);
    }
    hclose(sleep_h);
    printf("Test 4 completed successfully\n\n");

    printf("--- Test 5: Non-blocking Channel ---\n");
    hndl_t chv5[2];
    assert(chmake(chv5) == 0);

    const char* nbmsg = "NB test";
    errno = 0;
    int rc = chsend_nb(chv5[0], nbmsg, strlen(nbmsg)+1);
    if (rc < 0 && errno == EAGAIN) {
        printf("  non-blocking send correctly returned EAGAIN\n");
    } else {
        printf("  non-blocking send unexpected rc=%d errno=%d\n", rc, errno);
    }

    char nbbuf[32] = {0};
    rc = chrecv_nb(chv5[1], nbbuf, sizeof(nbbuf));
    if (rc < 0 && errno == EAGAIN) {
        printf("  non-blocking recv correctly returned EAGAIN\n");
    } else {
        printf("  non-blocking recv unexpected rc=%d errno=%d\n", rc, errno);
    }

    // cleanup
    hclose(chv5[0]);
    hclose(chv5[1]);
    printf("Test 5 completed successfully\n\n");

    printf("--- Test 6: Channel Duplication ---\n");
    hndl_t chv6[2];
    assert(chmake(chv6) == 0);
    // duplikasi handle send & recv
    hndl_t dup_s = chdup(chv6[0]);
    hndl_t dup_r = chdup(chv6[1]);
    assert(dup_s > 0 && dup_r > 0);
    
    // pastikan kedua handle merujuk ke channel yang sama
    assert(channel_equal(chv6[0], dup_s));
    assert(channel_equal(chv6[1], dup_r));
    printf("  channel_equal(chv6[0], dup_s) == true\n");
    printf("  channel_equal(chv6[1], dup_r) == true\n");
    
    // kirim via handle asli, terima via duplikat
    const char* dup_msg = "duplication!";
    assert(chsend(chv6[0], dup_msg, strlen(dup_msg)+1, -1) == 0);
    char dup_buf[64] = {0};
    assert(chrecv(dup_r, dup_buf, sizeof(dup_buf), -1) == 0);
    assert(strcmp(dup_buf, dup_msg) == 0);
    printf("  duplication test passed\n");

    // cleanup: tutup semua handle
    hclose(dup_s);
    hclose(dup_r);
    hclose(chv6[0]);
    hclose(chv6[1]);
    printf("Test 6 completed successfully\n\n");

    printf("=== All tests completed successfully ===\n");
    return 0;
}