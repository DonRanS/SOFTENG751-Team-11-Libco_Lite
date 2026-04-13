# Project Team 11 - Libco Lite
## Overview
This project is part of the **Libco Lite** initiative, developed by **Team 11**. It aims to provide lightweight, efficient, and modular coroutine solutions for Linux-based systems.

Following **libdll** (https://sustrik.github.io/libdill/documentation.html) design philosophy and its API, this high-performance coroutine library offers:

- **High-Performance Coroutines**: Stackful coroutines with optimized context switching
- **Go-Style Channels**: Type-safe communication between coroutines with buffering support  
- **Bundle Management**: Group and coordinate multiple coroutines (structured concurrency)
- **Advanced Optimizations**: Stack pooling, lock-free operations, direct context switching
- **Resource Management**: Handle-based system with automatic cleanup
- **Cancellation Support**: Cooperative cancellation mechanism

This project is totally irrelevant to THE **libco** library, much more primitive.
## Development iterations
[**Gen 1**](https://github.com/SE751-2025/project-team-11/tree/Gen1-libco-lite-ConceptV%26V): Conception Verfication. Just a try, not following **libdill** API, just for a tease. 

[**Gen 2**](https://github.com/SE751-2025/project-team-11/tree/Gen2-libco-lite-API-compactible): API adaption, follow design philosophy of **libdill**. Support structured concurrency.

[**Gen 3**](https://github.com/SE751-2025/project-team-11/tree/Gen3-libco-lite-deliverables): The default branch. Performance and stability optimization.
| **Component** | **Generation 1** | **Generation 2** | **Generation 3** |
|---------------|------------------|------------------|------------------|
| `Handle Lookup` | Basic map structure | `std::map` | `std::unordered_map` |
| `Stack Management` | Individual `malloc/free` | Individual `malloc/free` | `StackPool` reuse |
| `Synchronization` | Basic locking | Global mutexes | Reader–writer locks |
| `State Coordination` | Mutex-based | Mutex-based | `std::atomic` operations |
| `Context Switching` | Main context hub | Main context hub | Direct peer-to-peer supported |
| `Current Coroutine Access` | Map lookup | Map lookup | Thread-local cache |
| `Memory Layout` | Standard allocation | Standard allocation | 64-byte cache alignment |
| `Handle Generation` | Sequential counter | Sequential counter | Atomic batch counter |
| `Structured Concurrency` | Not implemented | Bundle abstractions | Bundle abstractions |
| `API Compatibility` | Custom interface | libdill compatible | libdill compatible |

 
## Performance Highlights

- **Stack Pooling**: Reuses 8KB stacks instead of allocating 64KB per coroutine
- **Direct Switching**: Bypasses main context for coroutine-to-coroutine switches (`yield_to()`)
- **Lock-Free Design**: Atomic operations with thread-local caching
- **O(1) Lookups**: Unordered maps instead of O(log n) data structures
- **Cache-Aligned Structures**: 64-byte aligned coroutine data for optimal performance

## Quick Start

### Basic Coroutine Example

```cpp
#include "libco-lite.h"
#include <iostream>

void worker(void* arg) {
    int* id = (int*)arg;
    std::cout << "Coroutine " << *id << " starting\n";
    
    for (int i = 0; i < 3; i++) {
        std::cout << "Coroutine " << *id << " iteration " << i << "\n";
        yield(); // Yield control back to caller
    }
    
    std::cout << "Coroutine " << *id << " finished\n";
}

int main() {
    int id1 = 1, id2 = 2;
    
    // Create coroutines
    hndl_t coro1 = go(worker, &id1);
    hndl_t coro2 = go(worker, &id2);
    
    // Resume them alternately
    while (is_coroutine_alive(coro1) || is_coroutine_alive(coro2)) {
        if (is_coroutine_alive(coro1)) resume(coro1);
        if (is_coroutine_alive(coro2)) resume(coro2);
    }
    
    // Cleanup
    hclose(coro1);
    hclose(coro2);
    return 0;
}
```

### Channel Communication (libdill-style)

```cpp
void producer(void* arg) {
    hndl_t* ch = (hndl_t*)arg;
    
    for (int i = 0; i < 10; i++) {
        chsend(ch[0], &i, sizeof(i), -1);
        std::cout << "Produced: " << i << std::endl;
    }
}

void consumer(void* arg) {
    hndl_t* ch = (hndl_t*)arg;
    int data;
    
    while (chrecv(ch[1], &data, sizeof(data), -1) == 0) {
        std::cout << "Consumed: " << data << std::endl;
    }
}

int main() {
    hndl_t ch[2];
    chmake(ch);
    chbuffer(ch[0], 5, sizeof(int)); // 5-item buffer
    
    hndl_t prod = go(producer, ch);
    hndl_t cons = go(consumer, ch);
    
    // Let them run
    while (is_coroutine_alive(prod) || is_coroutine_alive(cons)) {
        if (is_coroutine_alive(prod)) resume(prod);
        if (is_coroutine_alive(cons)) resume(cons);
    }
    
    hclose(prod);
    hclose(cons);
    hclose(ch[0]);
    hclose(ch[1]);
    return 0;
}
```

### Bundle Management (Structured Concurrency)

```cpp
void worker_func(void* arg) {
    int* id = (int*)arg;
    std::cout << "Worker " << *id << " executing\n";
    // Do work here
}

int main() {
    // Create bundle for structured concurrency
    hndl_t bundle = create_bundle();
    
    // Add coroutines to bundle
    int id1 = 1, id2 = 2, id3 = 3;
    bundle_go(bundle, worker_func, &id1);
    bundle_go(bundle, worker_func, &id2);
    bundle_go(bundle, worker_func, &id3);
    
    // Wait for all coroutines in bundle to complete
    bundle_wait(bundle, -1); // Block until all done
    
    hclose(bundle);
    return 0;
}
```

## API Reference

### Coroutine Management
```cpp
// Create coroutines
hndl_t go(void (*fn)(void*), void *arg);
hndl_t go_mem(void (*fn)(void*), void *arg, void *stack, size_t stack_size);

// Control execution
uintptr_t resume(hndl_t h, uintptr_t val = 0);
uintptr_t yield(uintptr_t val = 0);
uintptr_t yield_to(hndl_t target_coro, uintptr_t val = 0); // Direct switching

// Status checking
bool is_coroutine_alive(hndl_t h);
hndl_t get_running_coroutine();
```

### Channels (libdill-style)
```cpp
// Create channel pair
hndl_t chv[2];
chmake(chv); // chv[0] = sender, chv[1] = receiver

// Buffered channels
chbuffer(chv[0], 10, sizeof(int)); // 10-slot buffer for integers

// Send/receive
int data = 42;
chsend(chv[0], &data, sizeof(data), -1); // Blocking send
chrecv(chv[1], &data, sizeof(data), -1); // Blocking receive

// Non-blocking operations
chsend_nb(chv[0], &data, sizeof(data)); // Returns immediately
chrecv_nb(chv[1], &data, sizeof(data)); // Returns immediately
```

### Bundles (Structured Concurrency)
```cpp
// Create bundle
hndl_t bundle = create_bundle();

// Add coroutines to bundle
hndl_t coro1 = bundle_go(bundle, worker_func, &arg1);
hndl_t coro2 = bundle_go(bundle, worker_func, &arg2);

// Wait for all coroutines in bundle to complete
bundle_wait(bundle, -1); // Block until all done

hclose(bundle);
```

### Timing and Sleep
```cpp
// Get current time in milliseconds
int64_t current_time = now();

// Sleep for duration
sleep_for(1000); // Sleep for 1 second

// Sleep until deadline
int64_t deadline = now() + 5000;
msleep(deadline); // Sleep until deadline
```

### Cancellation
```cpp
// Cancel a coroutine
cancel(coro_handle);

// Check if current coroutine is canceled (inside coroutine)
if (is_canceled()) {
    // Cleanup and exit
    return;
}
```

### Function test

```bash
# Our minimumTest covers all basic functions of this library
xmake run minimumTest
```

## Performance Benchmarks

The library includes echo server comparisons:

```bash
# Build and run coroutine echo server
xmake run echoServer

# Build and run threaded echo server  
xmake run threadedServer

# Run benchmark client 
# 500 connections, 100 msgs per connection, 1024 Bytes per msg
xmake run simpleBench -c 500 -n 100 -s 1024
```

## Installation
1. Clone the repository:
    ```bash
    git clone https://github.com/SE751-2025/project-team-11.git
    ```
2. Navigate to the project directory:
    ```bash
    cd libco-lite-linux/project-team-11
    ```
3. Build with xmake:
    ```bash
    xmake
    ```
4. Run tests:
    ```bash
    xmake run minimumTest
    ```

## Build Configuration

### Available Targets

- `minimumTest` - Basic functionality test
- `echoServer` - Coroutine-based TCP echo server
- `threadedServer` - Thread-based comparison server
- `simpleBench` - Benchmark client
- `ctxbenchmark` - Context switching performance test

### Build Options

```bash
# Debug build
xmake config -m debug
xmake build

# Release build (optimized)
xmake config -m release  
xmake build
```

## Advanced Features

### Direct Context Switching

```cpp
// Instead of: yield() -> main -> resume(target)
// Use direct switching: current -> target
uintptr_t result = yield_to(target_coroutine, data);
```

### Custom Stack Management

```cpp
// Use custom stack
void* custom_stack = malloc(16384); // 16KB stack
hndl_t coro = go_mem(worker_func, arg, custom_stack, 16384);
```

### Choose Operation (Select-like)

```cpp
struct chclause clauses[] = {
    {CHRECV, ch1, &data1, sizeof(data1)},
    {CHRECV, ch2, &data2, sizeof(data2)},
    {CHSEND, ch3, &data3, sizeof(data3)}
};

int ready = choose(clauses, 3, deadline);
if (ready >= 0) {
    // clauses[ready] completed
}
```

## Error Handling

The library uses errno-style error reporting following libdill conventions:

```cpp
if (chsend(ch, &data, sizeof(data), 0) < 0) {
    switch (errno) {
        case EAGAIN:   // Would block (non-blocking mode)
        case ETIMEDOUT: // Deadline exceeded  
        case EPIPE:    // Channel closed
        case ECANCELED: // Operation canceled
        // Handle error
    }
}
```

## Thread Safety

- **Handle operations**: Thread-safe with internal synchronization
- **Channel operations**: Fully thread-safe, designed for multi-producer/consumer
- **Coroutine execution**: Single-threaded within each coroutine context
- **Performance counters**: Atomic operations for lock-free access



## License
This project is licensed under the [MIT License](LICENSE).

**TL;DR: Use this library however you want - commercial, personal, modify it, redistribute it. Just keep the copyright notice.**
