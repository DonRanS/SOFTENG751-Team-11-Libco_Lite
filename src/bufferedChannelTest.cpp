// bufferedChannelTest.cpp

#include "../include/libco-lite.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

int main() {
    printf("=== Buffered Channel Test ===\n");
    hndl_t ch[2];
    assert(chmake(ch) == 0);
    // siapkan buffer 3 slot × 16 byte
    assert(chbuffer(ch[0], 3, 16) == 0);
    assert(chcapacity(ch[0]) == 3);
    assert(chsize(ch[0]) == 0);

    // tulis 3 pesan
    for(int i = 1; i <= 3; i++) {
        char msg[16];
        snprintf(msg, sizeof(msg), "msg%d", i);
        assert(chsend(ch[0], msg, strlen(msg) + 1, -1) == 0);
        assert(chsize(ch[0]) == i);
    }

    // baca kembali, seharusnya msg1, msg2, msg3
    char buf[16];
    for(int i = 1; i <= 3; i++) {
        assert(chrecv(ch[1], buf, sizeof(buf), -1) == 0);
        printf("  got '%s'\n", buf);
        assert(chsize(ch[0]) == 3 - i);
    }

    hclose(ch[0]);
    hclose(ch[1]);
    printf("OK\n");
    return 0;
}
