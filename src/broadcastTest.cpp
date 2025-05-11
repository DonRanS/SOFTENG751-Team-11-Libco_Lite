// broadcastTest.cpp
// Minimal repro for chdone() broadcast + cancellation

#include "../include/libco-lite.h"
#include <stdio.h>
#include <assert.h>

// This coroutine will block in chrecv(), then should be canceled
void broadcast_coroutine(void* arg) {
    hndl_t* ch = (hndl_t*)arg;
    char buf[16];
    printf("Broadcast coro: menunggu chrecv()...\n");
    int rc = chrecv(ch[1], buf, sizeof(buf), -1);
    if (rc < 0 && is_canceled()) {
        printf("Broadcast coro: dibatalkan oleh chdone()\n");
    } else if (rc < 0) {
        printf("Broadcast coro: chrecv error %d\n", errno);
    } else {
        printf("Broadcast coro: unexpected recv '%s'\n", buf);
    }
}

int main() {
    printf("=== broadcastTest ===\n");

    hndl_t chv[2];
    assert(chmake(chv) == 0);
    printf("  handles are send=%d, recv=%d\n", chv[0], chv[1]);

    // 1) spawn + first resume → enters chrecv() and blocks
    hndl_t h = go(broadcast_coroutine, chv);
    assert(h >= 0);
    resume(h, 0);

    // 2) broadcast shutdown → should cancel the blocked coroutine
    printf("Main: calling chdone()\n");
    assert(chdone(chv[1]) == 0);

    // 3) wake it one more time so it can observe is_canceled()
    if (is_coroutine_alive(h)) {
        resume(h, 0);
    }

    // cleanup
    hclose(h);
    hclose(chv[0]);
    hclose(chv[1]);

    printf("=== broadcastTest completed ===\n");
    return 0;
}
