// libco-lite.cpp
// refactored--- libdll-like API

#include "../include/libco-lite.h"
#include <map>
#include <cstdlib>
#include <assert.h>
#include <cstring>
#include <vector>
#include <ucontext.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <iostream>
#include <mutex>
#include <memory>
#include <errno.h>

// unique identifier，like libdill---dill_unique_id marco
static const int cr_type_placeholder = 0;
static const void *cr_type = &cr_type_placeholder;

static const int bundle_type_placeholder = 0;
static const void *bundle_type_id = &bundle_type_placeholder;

static const int chan_type_placeholder = 0;
static const void *chan_type = &chan_type_placeholder;

// coro status 
enum {
    CORO_READY = 0,     // coro-created ready to run
    CORO_RUNNING = 1,   // coro-running
    CORO_SUSPENDED = 2, // coro-suspended
    CORO_EXITED = 3     // coro-exited
};

// coro structure - libdll-like. it has a virtual function table
struct coroutine_s {
    struct hvfs hvfs;       // vfs
    void* arg;              // coro argument
    int status;             // coro status
    ucontext_t ctx;         // coro context
    int id;                 // coro id (handle)
    uintptr_t yield_val;    // coro-yield value
    void (*fn)(void*);      // coro function
    bool has_own_stack;     // coro stackful or stackless
    size_t stack_size;      // stack size
    char *stack;            // stack pointer
};

// Bundle structure - libdll-like
struct bundle_s {
    struct hvfs hvfs;              // Virtual function table
    std::vector<hndl_t> coroutines; // Coroutines in this bundle
    bool done;                      // Whether finished
};

// Channel structure   - libdll-like channel mechanism
struct channel_s{
    struct hvfs hvfs;              // vfs
    std::vector<struct waiting_clause*> senders;   //  waiting to send
    std::vector<struct waiting_clause*> receivers; //  waiting to receive
    bool done;                      // is channel closed
    void* buf;                      // channel buffer
    size_t bufsize;                 // channel buffer size
    size_t itemsize;                // element size
    size_t items;                   // element count
    size_t first;                   // the first element index
    bool mem;                       // external memory is used?
};

// clause structure - for channel operation
struct waiting_clause {
    int id;                  // clause id
    hndl_t coro;             // coro handle
 //   hndl_t ch;               // channel handle
    void *val;               // data pointer
    size_t len;              // data length 
    int op;                  // operation tyoe：CHSEND or CHRECV
};

// global context - like libdill---dill_global_ctx
struct global_ctx {
    std::map<hndl_t, struct hvfs*> handles;  // handle to vfs mapping
    std::map<hndl_t, coroutine_s*> coroutines; // handle to coroutine mapping
    hndl_t next_handle;                      // next handle id(available)
    hndl_t running_coro;                     // running coroutine id
    ucontext_t main_ctx;                     // main context
   // std::vector<struct waiting_clause*> waiting_clauses; // waiting clauses
    std::mutex mutex;                        // mutex for critical section
    
    // init
    global_ctx() : next_handle(1), running_coro(-1) { }
};

static global_ctx& get_ctx() {
    static global_ctx instance;
    return instance;
}

// virtual function-- coroutine
static void *coroutine_query(struct hvfs *vfs, const void *type) {
    if (type == cr_type) return vfs;
    errno = ENOTSUP;
    return NULL;
}

static void coroutine_close(struct hvfs *vfs) {
    coroutine_s *cr = (coroutine_s*)vfs;
    if (cr->has_own_stack && cr->stack) {
        free(cr->stack);
    }
    free(cr);
}

// virtual function - Bundle
static void *bundle_query(struct hvfs *vfs, const void *type) {
    if (type == bundle_type_id)
        return vfs;
    
    errno = ENOTSUP;
    return NULL;
}

static void bundle_close(struct hvfs *vfs) {
    bundle_s *b = (bundle_s*)vfs;
    // close all sub-coroutines(u know, nested... if parent coro is closed, all sub coroutines are closed)
    for (hndl_t h : b->coroutines) {
        hclose(h);
    }
    free(b);
}

// virtual function - Channel
static void *channel_query(struct hvfs *vfs, const void *type) {
    if (type == chan_type) return vfs;
    errno = ENOTSUP;
    return NULL;
}

static void channel_close(struct hvfs *vfs) {
    channel_s *ch = (channel_s*)vfs;
    // release all waiting clauses
    for (auto waiter : ch->senders) {
        free(waiter);
    }
    for (auto waiter : ch->receivers) {
        free(waiter);
    }
    
    // free channel buffer
    if (ch->buf && !ch->mem) {
        free(ch->buf);
    }
    
    //free(ch); // DO not double free. segfault. You are trying to free something that is already freed.
}

// create a new handle for coroutine, bundle or channel
static hndl_t hmake(struct hvfs *vfs) {
    global_ctx &ctx = get_ctx();
    std::lock_guard<std::mutex> lock(ctx.mutex);
    
    hndl_t h = ctx.next_handle++;
    ctx.handles[h] = vfs;
    return h;
}

// get handle info a query function
void *hquery(hndl_t h, const void *type) {
    global_ctx &ctx = get_ctx();
    std::lock_guard<std::mutex> lock(ctx.mutex);
    
    auto it = ctx.handles.find(h);
    if (it == ctx.handles.end()) {
        errno = EBADF;
        return NULL;
    }
    
    return it->second->query(it->second, type);
}

// close a handle for coroutine, bundle or channel
int hclose(hndl_t h) {
    global_ctx &ctx = get_ctx();
    std::lock_guard<std::mutex> lock(ctx.mutex);
    
    auto it = ctx.handles.find(h);
    if (it == ctx.handles.end()) {
        errno = EBADF;
        return -1;
    }
    
    // close the handle
    it->second->close(it->second);
    
    // erase the handle from the map( handle table)
    ctx.handles.erase(it);
    
    // erase the coroutine from the coroutine map
    ctx.coroutines.erase(h);
    
    return 0;
}

// xfer handle ownership
// xfer ownership of handle to another handle
hndl_t hown(hndl_t h) {
    global_ctx &ctx = get_ctx();
    std::lock_guard<std::mutex> lock(ctx.mutex);
    
    auto it = ctx.handles.find(h);
    if (it == ctx.handles.end()) {
        errno = EBADF;
        return -1;
    }
    
    // get vfs pointer
    struct hvfs *vfs = it->second;
    
    // remove the old handle table 
    ctx.handles.erase(it);
    // remove the old coroutine from the coroutine map
    // create a new handle
    hndl_t new_h = ctx.next_handle++;
    ctx.handles[new_h] = vfs;
    
    // if the handle is a coroutine, update the coroutine map   
    auto coro_it = ctx.coroutines.find(h);
    if (coro_it != ctx.coroutines.end()) {
        coroutine_s *cr = coro_it->second;
        ctx.coroutines.erase(coro_it);
        ctx.coroutines[new_h] = cr;
        cr->id = new_h;
    }
    
    return new_h;
}

// coroutine entry point (basically, coroutines are bunch of functions with ton of entry points)
// like fiber in golang
static void coroutine_entry_point() {
    global_ctx &ctx = get_ctx();
    coroutine_s *cr = ctx.coroutines[ctx.running_coro];
    
    // execute the coroutine function
    cr->fn(cr->arg);
    
    // coro function finished
    cr->status = CORO_EXITED;
    
    // switch back to main context
    swapcontext(&cr->ctx, &ctx.main_ctx);
}

// create a new coroutine
hndl_t go(void (*fn)(void*), void *arg) {
    return go_mem(fn, arg, NULL, 0);
}

// use provided memory to create a new coroutine
hndl_t go_mem(void (*fn)(void*), void *arg, void *stack, size_t stack_size) {
    if (!fn) {
        errno = EINVAL;
        return -1;
    }
    
    // stack size cfg
    if (!stack && stack_size == 0) {
        stack_size = 64 * 1024; // 64KB for each coroutine by default
    }
    
    // allocate coroutine structure
    coroutine_s *cr = (coroutine_s*)malloc(sizeof(coroutine_s));
    if (!cr) {
        errno = ENOMEM;
        return -1;
    }
    
    // set virtual function table
    cr->hvfs.query = coroutine_query;
    cr->hvfs.close = coroutine_close;
    
    // initialize coroutine- status
    cr->fn = fn;
    cr->arg = arg;
    cr->status = CORO_READY;
    cr->yield_val = 0;
    
    // stack allocation and initialization and set and options and...
    // if stack is provided, use it. bunch of stack definitions and ops
    if (stack) {
        // existing stack, just use it. dont create a new one
        // new stack is not new car... old as good as new
        cr->stack = (char*)stack;
        cr->stack_size = stack_size;
        cr->has_own_stack = false;
    } else {
        // if no stack is provided, allocate a new one
        // we want to get stackful coroutine. right?
        cr->stack = (char*)malloc(stack_size);
        if (!cr->stack) {
            free(cr);
            errno = ENOMEM;
            return -1;
        }
        cr->stack_size = stack_size;
        cr->has_own_stack = true;
    }
    
    // context initialization
    getcontext(&cr->ctx);
    cr->ctx.uc_stack.ss_sp = cr->stack;
    cr->ctx.uc_stack.ss_size = cr->stack_size;
    cr->ctx.uc_link = &get_ctx().main_ctx;
    makecontext(&cr->ctx, (void(*)())coroutine_entry_point, 0);
    
    global_ctx &ctx = get_ctx();
    std::lock_guard<std::mutex> lock(ctx.mutex);
    hndl_t h = ctx.next_handle++;
    ctx.handles[h] = &cr->hvfs;
    cr->id = h;
    ctx.coroutines[h] = cr;
    
    return h;
}

hndl_t create_bundle() {
    return create_bundle_mem(NULL);
}

// use existing memory to create a new bundle
// somewhat questionable, but we need to do it... 
// 
hndl_t create_bundle_mem(void *mem) {
    bundle_s *b;
    bool is_mem = false;
    
    if (mem) {
b = static_cast<bundle_s*>(mem);
        is_mem = true;
    } else {
b = static_cast<bundle_s*>(malloc(sizeof(bundle_s)));
        if (!b) {
            errno = ENOMEM;
            return -1;
        }
    }
    
    // Set virtual function table
    b->hvfs.query = bundle_query;
    b->hvfs.close = bundle_close;
    
    // Initialize bundle
    b->done = false;
    b->coroutines.clear();
    
    // Create handle
    hndl_t h = hmake(&b->hvfs);
    if (h < 0) {
        if (!is_mem) free(b);
        return -1;
    }
    
    return h;
}

// add coroutine to bundle
hndl_t bundle_go(hndl_t bundle_h, void (*fn)(void*), void *arg) {
    return bundle_go_mem(bundle_h, fn, arg, NULL, 0);
}

// add coroutine to bundle with provided memory 
hndl_t bundle_go_mem(hndl_t bundle_h, void (*fn)(void*), void *arg, void *stack, size_t stack_size) {
    // get boundle
    bundle_s *b = (bundle_s*)hquery(bundle_h, bundle_type_id);
    if (!b) {
        return -1;  // error number set by hquery. so just return -1
    }
    
    // 创建协程
    hndl_t cr_h = go_mem(fn, arg, stack, stack_size);
    if (cr_h < 0) {
        return -1;  // error number set by go_mem return -1 by default
    }
    
    // add coroutine to bundle
    global_ctx &ctx = get_ctx();
    std::lock_guard<std::mutex> lock(ctx.mutex);
    b->coroutines.push_back(cr_h);
    
    return cr_h;
}

// wait for all coroutines in the bundle to finish
int bundle_wait(hndl_t bundle_h, int64_t deadline) {
    // get bundle
    bundle_s *b = (bundle_struct*)hquery(bundle_h, bundle_type_id);
    if (!b) {
        return -1;  // errno已由hquery设置
    }
    
    global_ctx &ctx = get_ctx();
    
    while (true) {
        bool all_done = true;
        
        // check if all coroutines are done
        {
            std::lock_guard<std::mutex> lock(ctx.mutex);
            for (auto it = b->coroutines.begin(); it != b->coroutines.end();) {
                hndl_t coro_h = *it;
                auto coro_it = ctx.coroutines.find(coro_h);
                
                if (coro_it == ctx.coroutines.end()) {
                    // if coroutine not found, remove it from the bundle
                    it = b->coroutines.erase(it);
                } else {
                    coroutine_s *cr = coro_it->second;
                    if (cr->status != CORO_EXITED) {
                        all_done = false;
                    }
                    ++it;
                }
            }
        }
        
        // if all coroutines are done, mark the bundle as done
        if (all_done) {
            b->done = true;
            return 0;
        }
        
        // expire time check
        if (deadline >= 0 && now() >= deadline) {
            errno = ETIMEDOUT;
            return -1;
        }
        
        // yield if in coroutine
        if (ctx.running_coro >= 0) {
            yield();
        } else {
            // short sleep if not in coroutine. avoid busy wait or 100% CPU usage
            usleep(1000);  // 1ms
        }
    }
}

// resume a coroutine
uintptr_t resume(hndl_t h, uintptr_t val) {
    global_ctx &ctx = get_ctx();
    
    // get coroutine
    // check if the handle is valid
    coroutine_s *cr;
    {
        std::lock_guard<std::mutex> lock(ctx.mutex);
        auto it = ctx.coroutines.find(h);
        if (it == ctx.coroutines.end()) {
            errno = EBADF;
            return (uintptr_t)-1;
        }
        cr = it->second;
    }
    
    // check coroutine status
    if (cr->status == CORO_RUNNING) {
        errno = EBUSY;
        return (uintptr_t)-1;
    }
    if (cr->status == CORO_EXITED) {
        errno = EINVAL;
        return (uintptr_t)-1;
    }
    
    // need a in come value. set income value
    cr->yield_val = val;
    
    // save current context
    hndl_t prev_coro = ctx.running_coro;
    
    // set the current coroutine as need to resume
    ctx.running_coro = h;
    cr->status = CORO_RUNNING;
    
    // context switch
    if (prev_coro >= 0) {
        coroutine_s *prev_cr = ctx.coroutines[prev_coro];
        prev_cr->status = CORO_SUSPENDED;
        swapcontext(&prev_cr->ctx, &cr->ctx);
    } else {
        swapcontext(&ctx.main_ctx, &cr->ctx);
    }
    
    // resume the coroutine
    ctx.running_coro = prev_coro;
    
    // if the coroutine has exited, we need to clean up. free resources
    if (cr->status == CORO_EXITED) {
        // Do not free the coroutine immediately, as it may be used in the bundle
        // freeed by hclose
    }
    
    // return the value from the coroutine. yield_val 
    return cr->yield_val;
}

// yield the current coroutine. give CPU time to other coroutines
uintptr_t yield(uintptr_t val) {
    global_ctx &ctx = get_ctx();
    
    // must be invoked in a coroutine
    if (ctx.running_coro < 0) {
        errno = EPERM;
        return (uintptr_t)-1;
    }
    
    // get the current coroutine
    coroutine_s *cr = ctx.coroutines[ctx.running_coro];
    
    // set return value
    // set the return value for the coroutine
    cr->yield_val = val;
    
    // switch back to the main context or caller context
    cr->status = CORO_SUSPENDED;
    swapcontext(&cr->ctx, &ctx.main_ctx);
    
    // return the value from the coroutine( for resume)
    return cr->yield_val;
}

// get system time in milliseconds  
int64_t now() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// sleep for a specified time in milliseconds
int msleep(int64_t deadline) {
    if (deadline < 0) {
        errno = EINVAL;
        return -1;
    }
    
    int64_t now_ms = now();
    if (now_ms >= deadline) {
        return 0;
    }
    
    // if in coroutine, yield until deadline. non-blocking
    global_ctx &ctx = get_ctx();
    if (ctx.running_coro >= 0) {
        while (now() < deadline) {
            yield();
        }
        return 0;
    }
    
    // or just sleep for the remaining time. block it!
    usleep((deadline - now_ms) * 1000);
    return 0;
}

int sleep_for(int64_t timeout_ms) {
    int64_t deadline = now() + timeout_ms;
    return msleep(deadline);
}

// channel creation
int chmake(hndl_t chv[2]) {
    return chmake_mem(NULL, chv);
}

// channel creation with provided memory
int chmake_mem(void *mem, hndl_t chv[2]) {
    if (!chv) {
        errno = EINVAL;
        return -1;
    }
    
    // allocate channel structure
    channel_s *ch;
    bool is_mem = false;
    
    if (mem) {
        ch = (channel_s*)mem;
        is_mem = true;
    } else {
        ch = (channel_s*)malloc(sizeof(channel_s));
        if (!ch) {
            errno = ENOMEM;
            return -1;
        }
    }
    
    // set virtual function table
    ch->hvfs.query = channel_query;
    ch->hvfs.close = channel_close;
    
    // ch init
    ch->done = false;
    ch->buf = NULL;
    ch->bufsize = 0;
    ch->itemsize = 0;
    ch->items = 0;
    ch->first = 0;
    ch->mem = is_mem;
    ch->senders.clear();
    ch->receivers.clear();
    
    // one for send, one for receive
    // handles for channel
    hndl_t h1 = hmake(&ch->hvfs);
    if (h1 < 0) {
        if (!is_mem) free(ch);
        return -1;
    }
    
    hndl_t h2 = hmake(&ch->hvfs);
    if (h2 < 0) {
        hclose(h1);
        return -1;
    }
    
    chv[0] = h1;
    chv[1] = h2;
    
    return 0;
}

// sendd data to channel
int chsend(hndl_t ch, const void *val, size_t len, int64_t deadline) {
    // get channel
    channel_s *c = (channel_s*)hquery(ch, chan_type);
    if (!c) {
        return -1;  // errno已由hquery设置
    }    
    // check channel status. closed or not
    if (c->done) {
        errno = EPIPE;
        return -1;
    }
    
    // validate parameters
    if (!val && len > 0) {
        errno = EINVAL;
        return -1;
    }
    
    global_ctx &ctx = get_ctx();
    
    // match with receiver... 
    // check if there are any receivers waiting
    if (!c->receivers.empty()) {
        waiting_clause *rx = c->receivers.front();
        c->receivers.erase(c->receivers.begin());
        
        // check message size
        if (rx->len < len) {
            // error: receiver buffer too small
            errno = EMSGSIZE;
            free(rx);
            return -1;
        }
        
        // cpy data to receiver buffer
        if (rx->val && val) {
            memcpy(rx->val, val, len);
        }
        
        // resume the receiver coroutine
        resume(rx->coro, rx->id);
        free(rx);
        return 0;
    }
    
    // if buffer is used and not full, write to buffer
    if (c->buf && c->items < c->bufsize / c->itemsize) {
        // calc nex t writable position
        size_t pos = (c->first + c->items) % (c->bufsize / c->itemsize);
        
        // cpy data to receiver buffer
        if (val) {
            memcpy((char*)c->buf + pos * c->itemsize, val, len);
        }
        
        // element count increment ++
        c->items++;
        return 0;
    }
    
    // if blckable and in coroutine
    if (deadline != 0 && ctx.running_coro >= 0) {
        // create waiting clause
        waiting_clause *tx = (waiting_clause*)malloc(sizeof(waiting_clause));
        if (!tx) {
            errno = ENOMEM;
            return -1;
        }
        
        // 
        // initialize waiting clause
        tx->id = 0;  // succ ssend id
        tx->coro = ctx.running_coro;
        tx->val = (void*)val;  // data ptr
        tx->len = len;
        tx->op = CHSEND;
        
        // add to senders queue
        c->senders.push_back(tx);
        
        // yield CPU, wait for matching receiver
        uintptr_t result = yield();
        
        // check result after yield( after resume)
        if (result == (uintptr_t)-1) {
            // error: send failed   
            errno = EPIPE;
            return -1;
        }
        
        return 0;
    }
    
    // non-blocking send, but channel is full
    errno = ETIMEDOUT;
    return -1;
}

// recv from channel    
int chrecv(hndl_t ch, void *val, size_t len, int64_t deadline) {
    // get channel
    channel_s *c = (channel_s*)hquery(ch, chan_type);
    if (!c) {
        return -1;  // errno已由hquery设置
    }
    
    // validate channel status
    if (!val && len > 0) {
        errno = EINVAL;
        return -1;
    }
    
    global_ctx &ctx = get_ctx();
    
    // check buf
    if (c->items > 0) {
        // RD buf
        if (val) {
            memcpy(val, (char*)c->buf + c->first * c->itemsize, 
                  len < c->itemsize ? len : c->itemsize);
        }
        // up buf status
        c->first = (c->first + 1) % (c->bufsize / c->itemsize);
        c->items--; // Consume!!!!!! Devour!!!
        
        // if there are waiting senders, notify them. process them
        if (!c->senders.empty()) {
            waiting_clause *tx = c->senders.front();
            c->senders.erase(c->senders.begin());
            
            // sav data to buffer
            if (tx->val) {
                size_t pos = (c->first + c->items) % (c->bufsize / c->itemsize);
                memcpy((char*)c->buf + pos * c->itemsize, tx->val, tx->len);
            }
            
            c->items++;
            
            // all done. resume the sender coroutine
            resume(tx->coro, tx->id);
            free(tx);
        }
        
        return 0;
    }
    
    // a sender is waiting?
    if (!c->senders.empty()) {
        waiting_clause *tx = c->senders.front();
        c->senders.erase(c->senders.begin());
        
        // check message size
        if (len < tx->len) {
            // channel buffer too small... yeah... like before, chk before consume
            errno = EMSGSIZE;
            free(tx);
            return -1;
        }
        
        if (val && tx->val) {
            memcpy(val, tx->val, tx->len);
        }

        resume(tx->coro, tx->id);
        free(tx);
        return 0;
    }
    
    if (c->done) {
        errno = EPIPE;
        return -1;
    }
    
    // if blockable and in coroutine
    if (deadline != 0 && ctx.running_coro >= 0) {
        // waiting clause created!
        waiting_clause *rx = (waiting_clause*)malloc(sizeof(waiting_clause));
        if (!rx) {
            errno = ENOMEM;
            return -1;
        }
        
        
        rx->id = 0;  // success recv id
        rx->coro = ctx.running_coro;
        rx->val = val;
        rx->len = len;
        rx->op = CHRECV;
        
        // add to receivers queue
        c->receivers.push_back(rx);
        
        // yield CPU, wait for matching sender
        uintptr_t result = yield();
        
        // chk after yield
        if (result == (uintptr_t)-1) {
            // err no. recv failed
            errno = EPIPE;
            return -1;
        }
        
        return 0;
    }
    
    // err no. non-blocking recv, but channel is empty
    // seems no such err no. so.. comment it out
    /*if (c->done) {
        errno = EPIPE;
        return -1;
    }*/
    errno = ETIMEDOUT;
    return -1;
}

int chsend_nb(hndl_t ch, const void *val, size_t len) {
    int rc = chsend(ch, val, len, 0);
    if (rc < 0 && (errno == ETIMEDOUT || errno == EBADF)) {
        errno = EAGAIN;
    }
    return rc;
}

// libco-lite.cpp

int chrecv_nb(hndl_t ch, void *val, size_t len) {
    int rc = chrecv(ch, val, len, 0);
    // if we got timeout *or* bad-handle, turn it into EAGAIN
    if (rc < 0 && (errno == ETIMEDOUT || errno == EBADF)) {
        errno = EAGAIN;
    }
    return rc;
}

hndl_t chdup(hndl_t ch) {
    // query untuk memastikan ch valid
    channel_s *c = (channel_s*)hquery(ch, chan_type);
    if (!c) {
        // errno sudah di-set oleh hquery menjadi EBADF
        return -1;
    }
    // buat handle baru yang menunjuk ke hvfs yang sama
    return hmake(&c->hvfs);
}

int channel_equal(hndl_t a, hndl_t b) {
    void *pa = hquery(a, chan_type);
    void *pb = hquery(b, chan_type);
    if (!pa || !pb) {
        errno = EBADF;
        return 0;
    }
    return pa == pb;
}

// channel done
int chdone(hndl_t ch) {
    // get
    channel_s *c = (channel_s*)hquery(ch, chan_type);
    if (!c) {
        return -1;  // 
    }
    
    if (c->done) {
        errno = EPIPE;
        return -1;
    }
    
    // 
    c->done = true;
    
    // notify all senders and receivers
    for (auto tx : c->senders) {
        resume(tx->coro, (uintptr_t)-1);  // err mark--tx
        free(tx);
    }
    c->senders.clear();
    
    for (auto rx : c->receivers) {
        resume(rx->coro, (uintptr_t)-1);  // err mark--rx
        free(rx);
    }
    c->receivers.clear();
    
    return 0;
}

// select from multiple channels
// ops in this function. pretty similar to the previous. so, comments are omitted.. cliche
int choose(struct chclause *clauses, int nclauses, int64_t deadline) {
   if (!clauses || nclauses <= 0) {
       errno = EINVAL;
       return -1;
   }
   
   global_ctx &ctx = get_ctx();
   
   // mmust be in coroutine
   // check if the current coroutine is valid
   if (ctx.running_coro < 0) {
       errno = EPERM;
       return -1;
   }
   
   // check all clauses. looking for immediate runnable clauses
   for (int i = 0; i < nclauses; i++) {
       struct chclause *cl = &clauses[i];
       
       channel_s *c = (channel_s*)hquery(cl->ch, chan_type);
       if (!c) {
           // invalid channel handle
           errno = EBADF;
           return -1;
       }
       
       if (cl->len > 0 && !cl->val) {
           errno = EINVAL;
           return i;
       }
       
       if (cl->op == CHSEND) {
       
           if (c->done) {
               errno = EPIPE;
               return i;
           }
           
           if (!c->receivers.empty()) {
               waiting_clause *rx = c->receivers.front();
               c->receivers.erase(c->receivers.begin());
               
               if (rx->len < cl->len) {
                   errno = EMSGSIZE;
                   free(rx);
                   return i;
               }
               
               if (rx->val && cl->val) {
                   memcpy(rx->val, cl->val, cl->len);
               }
               
               resume(rx->coro, rx->id);
               free(rx);
               
               errno = 0;
               return i;
           }
           
           if (c->buf && c->items < c->bufsize / c->itemsize) {
               size_t pos = (c->first + c->items) % (c->bufsize / c->itemsize);
               
               if (cl->val) {
                   memcpy((char*)c->buf + pos * c->itemsize, cl->val, cl->len);
               }
               
               // increase element count
               c->items++;
               
               errno = 0;
               return i;
           }
           // like explained before... so no comments. comments here are cliche...
       } else if (cl->op == CHRECV) {
         
           if (c->items > 0) {
               // read from buffer
               if (cl->val) {
                   memcpy(cl->val, (char*)c->buf + c->first * c->itemsize, 
                          cl->len < c->itemsize ? cl->len : c->itemsize);
               }
               
               // update buffer status
               c->first = (c->first + 1) % (c->bufsize / c->itemsize);
               c->items--;
               
               if (!c->senders.empty()) {
                   waiting_clause *tx = c->senders.front();
                   c->senders.erase(c->senders.begin());
                   
                   if (tx->val) {
                       size_t pos = (c->first + c->items) % (c->bufsize / c->itemsize);
                       memcpy((char*)c->buf + pos * c->itemsize, tx->val, tx->len);
                   }
                   
                   c->items++;
                   
                   resume(tx->coro, tx->id);
                   free(tx);
               }
               
               errno = 0;
               return i;
           }
           
           
           if (!c->senders.empty()) {
               waiting_clause *tx = c->senders.front();
               c->senders.erase(c->senders.begin());
                if (cl->len < tx->len) {
                   errno = EMSGSIZE;
                   free(tx);
                   return i;
              }
               
               if (cl->val && tx->val) {
                   memcpy(cl->val, tx->val, tx->len);
               }
               
               resume(tx->coro, tx->id);
               free(tx);
               
               errno = 0;
               return i;
           }
           
           if (c->done) {
               errno = EPIPE;
               return i;
           }
       } else {
           errno = EINVAL;
           return i;
       }
   }
   
   // if no clauses are immediately runnable, and not blockable
   if (deadline == 0) {
       errno = ETIMEDOUT;
       return -1;
   }
   
   // create waiting clauses for all clauses
   std::vector<waiting_clause*> waiters;
   for (int i = 0; i < nclauses; i++) {
       struct chclause *cl = &clauses[i];
       
       // get channel
       channel_s *c = (channel_s*)hquery(cl->ch, chan_type);
       if (!c) continue;  // already checked before
       
       waiting_clause *wc = (waiting_clause*)malloc(sizeof(waiting_clause));
       if (!wc) {
           for (auto w : waiters) {
               free(w);
           }
           errno = ENOMEM;
           return -1;
       }
       
       // waiting clause initialization
      // wc->id = -1;  // no need to se t a default value
       wc->id = i;  // clauses[i].id
       wc->coro = ctx.running_coro;
       wc->val = cl->val;
       wc->len = cl->len;
       wc->op = cl->op;
       
       // add to queue
       if (cl->op == CHSEND) {
           c->senders.push_back(wc);
       } else {
           c->receivers.push_back(wc);
       }
       
       waiters.push_back(wc);
   }
   
   // time expire check
   int64_t timeout = -1;
   if (deadline >= 0) {
       timeout = deadline - now();
       if (timeout < 0) timeout = 0;
   }
   
   // yield CPU and wait for a clause to be triggered
   int64_t start_time = now();
   uintptr_t result;
   
   if (timeout > 0) {
       result = yield();
       
       if (now() - start_time >= timeout && result == (uintptr_t)-1) {
           for (int i = 0; i < nclauses; i++) {
               struct chclause *cl = &clauses[i];
               channel_s *c = (channel_s*)hquery(cl->ch, chan_type);
               if (!c) continue;
                              if (cl->op == CHSEND) {
                   for (auto it = c->senders.begin(); it != c->senders.end();) {
                       if ((*it)->coro == ctx.running_coro) {
                           free(*it);
                           it = c->senders.erase(it);
                       } else {
                           ++it;
                       }
                   }
               } else {
                   for (auto it = c->receivers.begin(); it != c->receivers.end();) {
                       if ((*it)->coro == ctx.running_coro) {
                           free(*it);
                           it = c->receivers.erase(it);
                       } else {
                           ++it;
                       }
                   }
               }
           }
           
           errno = ETIMEDOUT;
           return -1;
       }
   } else {
       result = yield();
   }
   
   if (result >= 0 && result < (uintptr_t)nclauses) {
       return (int)result;
   }
   
   // if we reach here, it means the coroutine was resumed without a valid clause
   // clean up waiting clauses
   errno = EPIPE;
   return -1;
}

// wait for file descriptor to be readable
int fdin(int fd, int64_t deadline) {
   if (fd < 0) {
       errno = EBADF;
       return -1;
   }
   
   global_ctx &ctx = get_ctx();
   
   
   if (ctx.running_coro < 0) {
       errno = EPERM;
       return -1;
   }
      fd_set rfds;
   struct timeval tv;
   
   int64_t timeout = deadline >= 0 ? deadline - now() : -1;
   if (timeout < 0 && deadline >= 0) {
       errno = ETIMEDOUT;
       return -1;
   }
   
   while (true) {
       FD_ZERO(&rfds);
       FD_SET(fd, &rfds);
       
       if (timeout >= 0) {
           tv.tv_sec = timeout / 1000;
           tv.tv_usec = (timeout % 1000) * 1000;
       }
       
       int result = select(fd + 1, &rfds, NULL, NULL, timeout >= 0 ? &tv : NULL);
       
       if (result > 0) {
           return 0;
       } else if (result == 0) {
           errno = ETIMEDOUT;
           return -1;
       } else {
           if (errno == EINTR) {
               continue;
           }
           return -1;
       }
   }
}

int fdout(int fd, int64_t deadline) {
   if (fd < 0) {
       errno = EBADF;
       return -1;
   }
   
   global_ctx &ctx = get_ctx();
   
   if (ctx.running_coro < 0) {
       errno = EPERM;
       return -1;
   }
   
   fd_set wfds;
   struct timeval tv;
   
   int64_t timeout = deadline >= 0 ? deadline - now() : -1;
   if (timeout < 0 && deadline >= 0) {
       errno = ETIMEDOUT;
       return -1;
   }
   
   while (true) {
       FD_ZERO(&wfds);
       FD_SET(fd, &wfds);
       
       if (timeout >= 0) {
           tv.tv_sec = timeout / 1000;
           tv.tv_usec = (timeout % 1000) * 1000;
       }
       
       int result = select(fd + 1, NULL, &wfds, NULL, timeout >= 0 ? &tv : NULL);
       
       if (result > 0) {
           return 0;
       } else if (result == 0) {
           errno = ETIMEDOUT;
           return -1;
       } else {
           if (errno == EINTR) {
               // interrupted by signal, continue
               continue;
           }
           return -1;
       }
   }
}

// clean up file descriptor
int fdclean(int fd) {

   // in epoll, we need to clean up the file descriptor
   // not not yet!
   if (fd < 0) {
       errno = EBADF;
       return -1;
   }
   
   return 0;
}

bool is_coroutine_alive(hndl_t h) {
   global_ctx &ctx = get_ctx();
   std::lock_guard<std::mutex> lock(ctx.mutex);
   
   auto it = ctx.coroutines.find(h);
   if (it == ctx.coroutines.end()) {
       return false;
   }
   
   return it->second->status != CORO_EXITED;
}

hndl_t get_running_coroutine() {
   return get_ctx().running_coro;
}