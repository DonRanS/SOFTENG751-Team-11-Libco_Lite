#ifndef H_COROUTINE_H_
#define H_COROUTINE_H_

#include <stdint.h>
#include <sys/types.h>    
#include <map>
#include <cstddef>
#include <cstdlib>


// Error codes
#define CO_EINVAL EINVAL
#define CO_ENOMEM ENOMEM
#define CO_ETIMEDOUT ETIMEDOUT
#define CO_ECANCELED ECANCELED
#define CO_EBUSY EBUSY
#define CO_EPIPE EPIPE

// Handle type definition
typedef int hndl_t;

// Declare VFS interface
struct hvfs {
    void *(*query)(struct hvfs *vfs, const void *type);
    void (*close)(struct hvfs *vfs);
};

// Forward declarations
struct bundle_s;
struct coroutine_s;
struct channel_s;

// Type aliases, use _type suffix to avoid naming conflicts with functions
typedef struct bundle_s bundle_struct;
typedef struct coroutine_s coroutine_type;
typedef struct channel_s channel_type;

// Coroutine creation and management
hndl_t go(void (*fn)(void*), void *arg);
hndl_t go_mem(void (*fn)(void*), void *arg, void *stack, size_t stack_size);

// Coroutine control functions
uintptr_t resume(hndl_t h, uintptr_t val = 0);
uintptr_t yield(uintptr_t val = 0);
bool is_coroutine_alive(hndl_t h);
hndl_t get_running_coroutine();

// Bundle related functions - renamed to create_bundle to avoid conflict
hndl_t create_bundle();
hndl_t create_bundle_mem(void *mem);
hndl_t bundle_go(hndl_t bundle_h, void (*fn)(void*), void *arg);
hndl_t bundle_go_mem(hndl_t bundle_h, void (*fn)(void*), void *arg, void *stack, size_t stack_size);
int bundle_wait(hndl_t bundle_h, int64_t deadline);

// Handle management
void *hquery(hndl_t h, const void *type);
int hclose(hndl_t h);
hndl_t hown(hndl_t h);

// Basic synchronization primitives
int msleep(int64_t deadline);
int64_t now();
int sleep_for(int64_t timeout_ms);

// Channel operations
int chmake(hndl_t chv[2]);
int chmake_mem(void *mem, hndl_t chv[2]);
int chsend(hndl_t ch, const void *val, size_t len, int64_t deadline);
int chrecv(hndl_t ch, void *val, size_t len, int64_t deadline);
int chdone(hndl_t ch);
int chbuffer(hndl_t ch, size_t slots, size_t itemsize);
int chcapacity(hndl_t ch);
int chsize(hndl_t ch);     

// Cancel a coroutine (cooperative): any blocking call in that coro will fail with ECANCELED
int cancel(hndl_t h);
// Query inside a coroutine whether it has been canceled
bool is_canceled();

// return 1 if two channel handles refer to the same channel object
int channel_equal(hndl_t a, hndl_t b);

// Duplicate a channel handle (multi-producer/multi-consumer)
hndl_t chdup(hndl_t ch);

// Non-blocking channel operations (deadline = 0)
int chsend_nb(hndl_t ch, const void *val, size_t len);
int chrecv_nb(hndl_t ch, void *val, size_t len);


// Choose operation
struct chclause {
    int op;        // CHSEND or CHRECV
    hndl_t ch;     // Channel handle
    void *val;     // Value pointer
    size_t len;    // Value length
};

// Choose operation constants
#define CHSEND 1
#define CHRECV 2

int choose(struct chclause *clauses, int nclauses, int64_t deadline);

// File descriptor waiting
int fdin(int fd, int64_t deadline);
int fdout(int fd, int64_t deadline);
int fdclean(int fd);

#endif