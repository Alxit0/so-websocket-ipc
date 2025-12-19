# Codebase Q&A - HTTP Multi-Process Server

This document provides detailed answers to common questions about the codebase architecture, design decisions, and implementation details.

---

## Architecture & Design Questions

### 1. Why use a prefork architecture instead of a single process with threads?

**Answer:** The prefork architecture improves reliability and resource isolation. Each worker process has its own address space, preventing memory corruption in one worker from affecting others. It also provides better CPU core utilization through true parallelism (not just concurrency).

**Code Reference:**
- [main.c](src/main.c#L64-L83) - Worker process creation with `fork()`
- [main.c](src/main.c#L1) - Architecture overview comment

### 2. How does SO_REUSEPORT prevent the thundering herd problem?

**Answer:** `SO_REUSEPORT` allows multiple sockets to bind to the same port, with the kernel distributing incoming connections across workers. This prevents all workers from waking up on a single connection (thundering herd).

**Code Reference:**
- [server.c](src/server.c#L37-L39) - `SO_REUSEPORT` socket option configuration

```c
int opt = 1;
setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif
```

### 3. Why are `/health`, `/metrics`, and `/stats` endpoints processed before enqueueing?

**Answer:** Priority endpoints need to be highly available for monitoring, even when the server is under heavy load. By bypassing the queue, they can respond immediately without waiting for worker threads.

**Code Reference:**
- [server.c](src/server.c#L337-L343) - Priority endpoint detection and handling
- [server.c](src/server.c#L148-L164) - `is_priority_endpoint()` function using `MSG_PEEK`

```c
// Check if this is a priority endpoint (metrics, health, stats)
// Priority endpoints bypass the queue and are handled immediately
if (is_priority_endpoint(client_fd)) {
    priority_handled++;
    handle_priority_endpoint(client_fd);
    continue;
}
```

### 4. What's the advantage of having a separate LRU cache per worker instead of shared cache?

**Answer:** Per-worker caches eliminate lock contention and synchronization overhead that would occur with a shared cache. Each worker can access its cache with minimal locking, improving throughput. The tradeoff is less efficient memory usage.

**Code Reference:**
- [server.c](src/server.c#L266-L278) - Per-worker cache initialization
- [file_cache.c](src/file_cache.c#L1) - LRU cache implementation comment

### 5. Why use a bounded circular queue instead of an unbounded linked list?

**Answer:** Bounded queues prevent unbounded memory growth under load and naturally implement backpressure. When the queue is full, new connections receive 503 responses instead of consuming unlimited memory.

**Code Reference:**
- [connection_queue.h](src/connection_queue.h#L8) - `QUEUE_SIZE` definition (100 slots)
- [connection_queue.c](src/connection_queue.c#L1) - Producer-Consumer queue comment

---

## Synchronization Questions

### 6. How do you prevent deadlocks with multiple synchronization primitives?

**Answer:** The code uses a consistent locking order and minimal critical sections. Semaphores control queue access, mutexes protect counters, and rwlocks guard cache. Each synchronization primitive has a single, well-defined purpose, reducing deadlock risk.

**Code Reference:**
- [connection_queue.c](src/connection_queue.c#L20-L45) - Semaphore initialization
- [file_cache.c](src/file_cache.c#L108) - Reader-writer lock initialization
- [stats.c](src/stats.c#L31) - Statistics semaphore

### 7. Why use three semaphores (`empty_slots`, `filled_slots`, `mutex`) for the connection queue?

**Answer:** This is the classic bounded buffer solution:
- `empty_slots`: Counts available slots (producer waits when full)
- `filled_slots`: Counts filled slots (consumer waits when empty)
- `mutex`: Ensures mutual exclusion when updating queue state

**Code Reference:**
- [connection_queue.c](src/connection_queue.c#L20-L45) - Semaphore initialization with comments

```c
// empty_slots: initially QUEUE_SIZE (all slots are empty)
if (sem_init(&queue->empty_slots, 0, QUEUE_SIZE) != 0) {
    log_message("Failed to initialize empty_slots semaphore: %s", strerror(errno));
    return -1;
}

// filled_slots: initially 0 (no slots are filled)
if (sem_init(&queue->filled_slots, 0, 0) != 0) {
    log_message("Failed to initialize filled_slots semaphore: %s", strerror(errno));
    sem_destroy(&queue->empty_slots);
    return -1;
}

// mutex: binary semaphore for mutual exclusion (initialized to 1)
if (sem_init(&queue->mutex, 0, 1) != 0) {
    log_message("Failed to initialize mutex semaphore: %s", strerror(errno));
    sem_destroy(&queue->empty_slots);
    sem_destroy(&queue->filled_slots);
    return -1;
}
```

### 8. How is the shared memory statistics structure protected from race conditions?

**Answer:** A process-shared semaphore protects the statistics structure. The semaphore is initialized with `sem_init(..., 1, 1)` where the first `1` indicates it's shared between processes.

**Code Reference:**
- [stats.c](src/stats.c#L10-L33) - Shared memory initialization with `mmap()` and semaphore
- [stats.c](src/stats.c#L97-L109) - Statistics update with semaphore protection

```c
global_stats = mmap(NULL, sizeof(server_stats_t), 
                   PROT_READ | PROT_WRITE, 
                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
// ...
sem_init(&global_stats->semaphore, 1, 1);  // 1 = shared between processes
```

### 9. What's the locking order when accessing queue, cache, and stats?

**Answer:** Each subsystem is independent with no nested locking:
- **Queue:** Acquire `empty_slots` → `mutex` → `filled_slots`, released in reverse
- **Cache:** Single `pthread_rwlock_t` (read or write lock)
- **Stats:** Single `sem_t` semaphore

No cross-subsystem locks are held simultaneously, preventing deadlock.

**Code Reference:**
- [connection_queue.c](src/connection_queue.c#L52-L79) - Queue enqueue operation
- [file_cache.c](src/file_cache.c#L142-L164) - Cache get operation
- [stats.c](src/stats.c#L97-L120) - Stats update operation

### 10. How does the reader-writer lock improve cache performance?

**Answer:** Reader-writer locks allow multiple threads to read concurrently (cache hits) while ensuring exclusive access for writes (cache updates). Since cache hits are more common than misses, this significantly reduces contention.

**Code Reference:**
- [file_cache.c](src/file_cache.c#L142) - `pthread_rwlock_wrlock()` for LRU update on cache hit
- [file_cache.c](src/file_cache.c#L204) - Write lock for cache put operations
- [file_cache.h](src/file_cache.h#L35) - `pthread_rwlock_t lock` field

---

## Performance Questions

### 11. What happens when the connection queue is full?

**Answer:** The producer (accept loop) uses `connection_queue_try_enqueue()` which returns immediately if the queue is full. A 503 Service Unavailable response is sent to the client with a `Retry-After: 1` header.

**Code Reference:**
- [server.c](src/server.c#L345-L355) - Queue full handling with 503 response
- [server.c](src/server.c#L124-L136) - `send_503_response()` function
- [connection_queue.c](src/connection_queue.c#L84-L113) - `connection_queue_try_enqueue()` with `sem_trywait()`

```c
// Try to enqueue connection (non-blocking)
if (connection_queue_try_enqueue(&conn_queue, client_fd) != 0) {
    // Queue is full - reject with 503
    total_rejected++;
    send_503_response(client_fd);
    
    // Log every 100 rejections to avoid log spam
    if (total_rejected % 100 == 1) {
        log_message("Worker %d: Queue full, rejected %lu connections so far", 
                   worker_id, total_rejected);
    }
}
```

### 12. How does the LRU cache eviction policy work?

**Answer:** The cache maintains a doubly-linked list where the head is the most recently used (MRU) and the tail is the least recently used (LRU). On cache hit, the entry moves to the front. When space is needed, entries are evicted from the tail.

**Code Reference:**
- [file_cache.c](src/file_cache.c#L12-L44) - `move_to_front()` function
- [file_cache.c](src/file_cache.c#L64-L86) - `evict_lru()` function
- [file_cache.c](src/file_cache.c#L154) - Moving entry to front on cache hit

```c
static void evict_lru(file_cache_t* cache) {
    if (!cache->tail) {
        return;
    }
    
    cache_entry_t* lru = cache->tail;
    
    // Remove from list
    if (lru->prev) {
        lru->prev->next = NULL;
    }
    cache->tail = lru->prev;
    
    if (cache->head == lru) {
        cache->head = NULL;
    }
    
    // Update cache statistics
    cache->total_size -= lru->content_size;
    cache->entry_count--;
    
    log_message("Cache: Evicted LRU entry '%s' (%zu bytes)", 
                lru->path, lru->content_size);
    
    // Free memory
    free(lru->content);
    free(lru);
}
```

### 13. What's the time complexity of enqueue/dequeue operations?

**Answer:** Both operations are **O(1)**. The circular buffer uses fixed indices (`head` and `tail`) updated with modulo arithmetic. Semaphores provide O(1) blocking/signaling.

**Code Reference:**
- [connection_queue.c](src/connection_queue.c#L52-L79) - Enqueue with O(1) array indexing
- [connection_queue.c](src/connection_queue.c#L116-L147) - Dequeue with O(1) array indexing

```c
// Add connection to the queue - O(1)
queue->connections[queue->tail] = client_fd;
queue->tail = (queue->tail + 1) % QUEUE_SIZE;
```

### 14. Why limit cached files to 1MB?

**Answer:** The 1MB limit (`MAX_FILE_SIZE`) prevents large files from consuming all cache memory and ensures fair distribution among cached resources. Large files are served efficiently via `sendfile()` anyway.

**Code Reference:**
- [file_cache.h](src/file_cache.h#L10) - `MAX_FILE_SIZE` definition (1MB)
- [file_cache.c](src/file_cache.c#L179-L183) - Size check in `file_cache_put()`
- [http.c](src/http.c#L148) - Cacheable file size check

```c
#define MAX_FILE_SIZE (1024 * 1024)  // 1MB max file size for caching
```

### 15. How does `sendfile()` improve performance for large files?

**Answer:** `sendfile()` transfers data directly from file descriptor to socket in kernel space, avoiding expensive user-space copying. This is zero-copy I/O, significantly faster for large files.

**Code Reference:**
- [http.c](src/http.c#L191-L200) - `sendfile()` loop for large files

```c
// Use sendfile for large files or when cache is not available
off_t offset = 0;
while (offset < file_size) {
    ssize_t sent = sendfile(client_fd, fd, &offset, file_size - offset);
    if (sent <= 0) {
        if (errno == EINTR) continue;
        break;
    }
}
```

---

## Implementation Details Questions

### 16. How are worker processes created and managed?

**Answer:** The master process forks `NUM_WORKERS` child processes using `fork()`. Each worker inherits the listening socket and runs independently. Worker PIDs are stored for graceful shutdown.

**Code Reference:**
- [main.c](src/main.c#L64-L83) - Worker process creation loop
- [main.c](src/main.c#L111-L124) - Graceful shutdown with `SIGTERM` and `waitpid()`

```c
// Fork worker processes
pid_t* worker_pids = malloc(sizeof(pid_t) * config.num_workers);

for (int i = 0; i < config.num_workers; i++) {
    pid_t pid = fork();
    
    if (pid < 0) {
        perror("fork failed");
        exit(EXIT_FAILURE);
    }
    
    if (pid == 0) {
        // Child process - worker
        worker_process(server_fd, i, &config);
        close(server_fd);
        exit(EXIT_SUCCESS);
    } else {
        // Parent process - store worker PID
        worker_pids[i] = pid;
    }
}
```

### 17. How does graceful shutdown work across all workers and threads?

**Answer:** 
1. Master receives `SIGINT`/`SIGTERM` and sets `keep_running = 0`
2. Master sends `SIGTERM` to all workers
3. Each worker stops accepting new connections
4. `connection_queue_shutdown()` signals all waiting threads
5. Threads finish current requests and exit
6. Master waits for all workers with `waitpid()`

**Code Reference:**
- [main.c](src/main.c#L111-L124) - Master shutdown sequence
- [server.c](src/server.c#L358-L370) - Worker graceful shutdown
- [connection_queue.c](src/connection_queue.c#L193-L205) - Queue shutdown signaling

```c
void connection_queue_shutdown(connection_queue_t* queue) {
    if (!queue) {
        return;
    }
    
    sem_wait(&queue->mutex);
    queue->shutdown = 1;
    sem_post(&queue->mutex);
    
    // Wake up all waiting consumers by posting to filled_slots
    // This ensures threads waiting on dequeue will wake up and check shutdown flag
    for (int i = 0; i < QUEUE_SIZE; i++) {
        sem_post(&queue->filled_slots);
    }
    
    log_message("Connection queue shutdown signaled");
}
```

### 18. What's the purpose of `MSG_PEEK` in `is_priority_endpoint()`?

**Answer:** `MSG_PEEK` reads data from the socket without removing it from the receive buffer. This allows inspection of the HTTP request to determine if it's a priority endpoint without consuming the data needed for actual request processing.

**Code Reference:**
- [server.c](src/server.c#L148-L164) - Priority endpoint detection with `MSG_PEEK`

```c
int is_priority_endpoint(int client_fd) {
    char buffer[512];
    
    // Peek at the request without consuming it
    ssize_t bytes = recv(client_fd, buffer, sizeof(buffer) - 1, MSG_PEEK);
    if (bytes <= 0) {
        return 0;
    }
    
    buffer[bytes] = '\0';
    
    // Check if it's a GET/HEAD request for priority endpoints
    if (strstr(buffer, "GET /metrics") == buffer || 
        strstr(buffer, "HEAD /metrics") == buffer ||
        strstr(buffer, "GET /health") == buffer || 
        strstr(buffer, "HEAD /health") == buffer ||
        strstr(buffer, "GET /stats") == buffer || 
        strstr(buffer, "HEAD /stats") == buffer) {
        return 1;
    }
    
    return 0;
}
```

### 19. How is response time measured and tracked?

**Answer:** Response time is tracked per request in [http.c](src/http.c) using `clock_gettime()`. The `add_response_time()` function accumulates total time and count in shared memory for calculating averages.

**Code Reference:**
- [stats.c](src/stats.c#L150-L158) - `add_response_time()` function
- [stats.c](src/stats.c#L201-L212) - Average response time calculation in metrics

```c
void add_response_time(long long time_ms) {
    if (!global_stats) return;
    
    sem_wait(&global_stats->semaphore);
    global_stats->total_response_time_ms += time_ms;
    global_stats->response_count++;
    sem_post(&global_stats->semaphore);
}
```

### 20. How does the logger ensure thread-safety?

**Answer:** The logger implementation uses mutexes or file locking to serialize log writes from multiple threads/processes. Each log message is written atomically to prevent interleaved output.

**Code Reference:**
- [logger.c](src/logger.c) - Logger implementation (see source for details)
- [logger.h](src/logger.h) - Logger interface

---

## Configuration & Tuning Questions

### 21. How do you determine optimal `NUM_WORKERS` and `THREADS_PER_WORKER` values?

**Answer:** 
- **NUM_WORKERS:** Typically set to CPU core count for CPU-bound workloads or 2x cores for I/O-bound workloads
- **THREADS_PER_WORKER:** Balance between connection capacity and context switching overhead. Start with 4-8 threads per worker.

Formula: `Total Capacity = NUM_WORKERS × THREADS_PER_WORKER × Queue Size`

**Code Reference:**
- [config.h](src/config.h#L5-L12) - Configuration structure
- [main.c](src/main.c#L58) - Number of workers logged at startup

### 22. What's the tradeoff between `QUEUE_SIZE` and memory usage?

**Answer:** Larger queues absorb traffic bursts better but consume more memory (each slot holds a file descriptor and pointer). Too small causes frequent 503 errors. Recommended: 50-200 slots per worker.

Memory impact: `QUEUE_SIZE × sizeof(int) × NUM_WORKERS` (minimal, ~400-800 bytes per worker)

**Code Reference:**
- [connection_queue.h](src/connection_queue.h#L8) - `QUEUE_SIZE` set to 100

```c
#define QUEUE_SIZE 100  // Bounded circular buffer size
```

### 23. How does `CACHE_SIZE_MB` affect hit rate and memory?

**Answer:** Larger caches improve hit rates but consume more RAM per worker. Total memory = `CACHE_SIZE_MB × NUM_WORKERS`. Monitor cache stats to tune. Set to 0 to disable caching.

**Code Reference:**
- [server.c](src/server.c#L266-L278) - Cache initialization with size check
- [file_cache.c](src/file_cache.c#L105) - Converting MB to bytes

```c
cache->max_size = max_size_mb * 1024 * 1024;  // Convert MB to bytes
```

### 24. What's the purpose of the `TIMEOUT_SECONDS` parameter?

**Answer:** Socket timeouts prevent slow clients from holding threads indefinitely. Both `SO_RCVTIMEO` and `SO_SNDTIMEO` are set to terminate inactive connections.

**Code Reference:**
- [server.c](src/server.c#L93-L97) - Socket timeout configuration

```c
// Set socket timeouts
struct timeval tv;
tv.tv_sec = ctx->config->timeout_seconds;
tv.tv_usec = 0;
setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
```

---

## Error Handling Questions

### 25. How are 503 Service Unavailable responses generated?

**Answer:** When `connection_queue_try_enqueue()` fails (queue full), the `send_503_response()` function sends a complete HTTP response with `Retry-After: 1` header, then closes the connection immediately.

**Code Reference:**
- [server.c](src/server.c#L124-L136) - `send_503_response()` implementation
- [server.c](src/server.c#L345-L355) - 503 response trigger

```c
void send_503_response(int client_fd) {
    const char* response = 
        "HTTP/1.1 503 Service Unavailable\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "Retry-After: 1\r\n"
        "\r\n"
        "<html><body><h1>503 Service Unavailable</h1>"
        "<p>Server is overloaded. Please try again later.</p>"
        "</body></html>";
    update_stats_with_code(strlen(response), 503);
    send(client_fd, response, strlen(response), 0);
    close(client_fd);
}
```

### 26. What happens if semaphore initialization fails?

**Answer:** The initialization functions return -1 and cleanup already-initialized semaphores. The worker process logs the error and exits, preventing undefined behavior.

**Code Reference:**
- [connection_queue.c](src/connection_queue.c#L20-L45) - Semaphore initialization with cleanup on failure

```c
if (sem_init(&queue->empty_slots, 0, QUEUE_SIZE) != 0) {
    log_message("Failed to initialize empty_slots semaphore: %s", strerror(errno));
    return -1;
}

if (sem_init(&queue->filled_slots, 0, 0) != 0) {
    log_message("Failed to initialize filled_slots semaphore: %s", strerror(errno));
    sem_destroy(&queue->empty_slots);  // Cleanup
    return -1;
}
```

### 27. How does the system handle file I/O errors?

**Answer:** File operations check return values and fall back gracefully:
- `fopen()` fails → Send 404 response
- `fstat()` fails → Send 500 response  
- `sendfile()` fails → Break loop and close connection
- Cache allocation fails → Serve directly without caching

**Code Reference:**
- [http.c](src/http.c#L118-L125) - File open error handling
- [http.c](src/http.c#L128-L135) - `fstat()` error handling

```c
FILE* file = fopen(full_path, "rb");
if (!file) {
    const char* body = "<h1>404 Not Found</h1>";
    send_http_response(client_fd, 404, "Not Found", "text/html", body, strlen(body));
    update_stats_with_code(strlen(body), 404);
    return;
}
```

### 28. What's the recovery strategy for queue overflow?

**Answer:** Queue overflow is handled through backpressure - clients receive 503 responses with `Retry-After` headers. This prevents resource exhaustion while maintaining service availability for monitoring endpoints.

**Code Reference:**
- [server.c](src/server.c#L345-L355) - Queue overflow handling
- [connection_queue.c](src/connection_queue.c#L84-L113) - Non-blocking enqueue

---

## Testing & Debugging Questions

### 29. How would you detect memory leaks in this multi-process server?

**Answer:** Use these approaches:
1. **Valgrind:** `valgrind --leak-check=full --trace-children=yes ./server`
2. **AddressSanitizer:** Compile with `-fsanitize=address`
3. Monitor `/proc/[pid]/status` for `VmRSS` growth
4. Check cache statistics for unbounded growth

**Code Reference:**
- [server.c](src/server.c#L374-L379) - Cache statistics logging before exit
- [file_cache.c](src/file_cache.c#L291-L303) - `file_cache_stats()` function

### 30. What tools would you use to identify race conditions?

**Answer:**
1. **ThreadSanitizer:** Compile with `-fsanitize=thread` (detects data races)
2. **Helgrind:** `valgrind --tool=helgrind ./server`
3. **strace:** Monitor syscalls for unexpected patterns
4. Stress testing with high concurrency (see [tests/](tests/))

**Code Reference:**
- [tests/stress.js](tests/stress.js) - K6 stress test script
- [tests/docker-compose.k6.stresstest.yml](tests/docker-compose.k6.stresstest.yml) - Stress test configuration

### 31. How do you simulate high load to test queue saturation?

**Answer:** Use ApacheBench or K6:
- **ApacheBench:** `ab -n 100000 -c 1000 http://localhost:8080/`
- **K6:** Run `docker-compose -f tests/docker-compose.k6.stresstest.yml up`

Monitor logs for "Queue full, rejected" messages and 503 responses.

**Code Reference:**
- [tests/stress.js](tests/stress.js) - K6 stress test configuration
- [tests/bash_test.sh](tests/bash_test.sh) - Bash test script
- [server.c](src/server.c#L350-L354) - Queue rejection logging

### 32. How can you verify cache hit/miss ratios?

**Answer:** Check server logs for "Cache: HIT" vs "Cache: MISS" messages. The HTTP response includes `X-Cache: HIT` or `X-Cache: MISS` headers. Also monitor worker exit cache statistics.

**Code Reference:**
- [http.c](src/http.c#L103) - Cache hit header `X-Cache: HIT`
- [http.c](src/http.c#L177) - Cache miss header `X-Cache: MISS`
- [file_cache.c](src/file_cache.c#L162) - Cache hit logging
- [server.c](src/server.c#L374-L379) - Final cache statistics on worker exit

---

## Monitoring Questions

### 33. What metrics are exposed via the `/metrics` endpoint?

**Answer:** Prometheus-compatible metrics:
- `http_requests_total` - Total requests (counter)
- `http_requests_bytes_sent_total` - Total bytes sent (counter)
- `http_responses_total{code}` - Responses by status code (counter)
- `http_connections_active` - Active connections (gauge)
- `http_response_time_milliseconds_avg` - Overall average response time (gauge)
- `http_response_time_milliseconds_since_last` - Average since last metrics call (gauge)

**Code Reference:**
- [stats.c](src/stats.c#L179-L241) - `generate_metrics_response()` implementation

```c
*response_len = snprintf(response, sizeof(response),
    "# HELP http_requests_total Total number of HTTP requests\n"
    "# TYPE http_requests_total counter\n"
    "http_requests_total %d\n"
    "\n"
    "# HELP http_requests_bytes_sent_total Total bytes sent in HTTP responses\n"
    "# TYPE http_requests_bytes_sent_total counter\n"
    "http_requests_bytes_sent_total %d\n"
    // ... more metrics
```

### 34. How is the "average response time since last call" calculated?

**Answer:** The metrics endpoint stores snapshots of `total_response_time_ms` and `response_count`. On the next call, it calculates the delta and divides to get the average since last check.

**Code Reference:**
- [stats.c](src/stats.c#L201-L212) - Delta calculation for "since last" metrics

```c
// Calculate response time since last metrics call
long long avg_response_time_since_last = 0;
int requests_since_last = global_stats->response_count - global_stats->last_response_count;
if (requests_since_last > 0) {
    long long time_since_last = global_stats->total_response_time_ms - global_stats->last_total_response_time_ms;
    avg_response_time_since_last = time_since_last / requests_since_last;
}

// ...

// Update last snapshot for next call
global_stats->last_total_response_time_ms = global_stats->total_response_time_ms;
global_stats->last_response_count = global_stats->response_count;
```

### 35. What's the difference between `/health`, `/metrics`, and `/stats`?

**Answer:**
- **`/health`:** Simple JSON with service status and active connections (for load balancer health checks)
- **`/metrics`:** Prometheus-formatted metrics (for monitoring systems)
- **`/stats`:** Detailed JSON statistics with all counters (for debugging/dashboards)

**Code Reference:**
- [stats.c](src/stats.c#L170-L176) - `/health` endpoint JSON generation
- [stats.c](src/stats.c#L179-L241) - `/metrics` endpoint Prometheus format
- [stats.c](src/stats.c#L247-L285) - `/stats` endpoint detailed JSON

### 36. How would you integrate this with Prometheus?

**Answer:** Prometheus scrapes the `/metrics` endpoint. Configure `prometheus.yml`:

```yaml
scrape_configs:
  - job_name: 'http-server'
    static_configs:
      - targets: ['localhost:8080']
    metrics_path: '/metrics'
    scrape_interval: 15s
```

**Code Reference:**
- [prometheus.yml](prometheus.yml) - Prometheus configuration
- [monitoring/prometheus.yml](monitoring/prometheus.yml) - Monitoring stack configuration
- [docker-compose.monitoring.yml](docker-compose.monitoring.yml) - Docker compose with Prometheus + Grafana

---

## Potential Issues Questions

### 37. What could cause a deadlock in this implementation?

**Answer:** Potential deadlock scenarios:
1. Incorrect semaphore acquisition order (prevented by consistent order: `empty_slots` → `mutex` → `filled_slots`)
2. Holding queue lock while acquiring cache lock (avoided - independent subsystems)
3. Signal handler interrupting critical section (prevented by signal masking)

**Prevention:** Minimal critical sections, no nested locks across subsystems, consistent lock ordering.

**Code Reference:**
- [connection_queue.c](src/connection_queue.c#L52-L79) - Consistent semaphore ordering
- [file_cache.c](src/file_cache.c#L142-L164) - Independent cache locking

### 38. How do you prevent path traversal attacks (e.g., `/../../etc/passwd`)?

**Answer:** The code should canonicalize paths and check that resolved paths remain within `document_root`. While not explicitly shown in the provided snippets, this is typically done with `realpath()` or by rejecting paths containing `..`.

**Recommendation:** Add path validation in [http.c](src/http.c) before file access:

```c
char resolved_path[PATH_MAX];
if (realpath(full_path, resolved_path) == NULL || 
    strncmp(resolved_path, document_root, strlen(document_root)) != 0) {
    // Path traversal attempt - send 403
}
```

### 39. What happens if a worker crashes?

**Answer:** 
- Other workers continue serving requests unaffected
- Master process reaps the dead worker with `waitpid()` in the main loop
- Current implementation logs the death but doesn't restart the worker
- **Enhancement needed:** Implement worker respawning for fault tolerance

**Code Reference:**
- [main.c](src/main.c#L99-L103) - Dead child reaping in master loop

```c
// Reap any dead children
int status;
pid_t pid;
while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    log_message("Reaped child process %d", pid);
}
```

### 40. How do you handle slow clients that block threads?

**Answer:** Socket timeouts (`SO_RCVTIMEO` and `SO_SNDTIMEO`) automatically close connections that are idle for longer than `timeout_seconds`. This prevents thread starvation.

**Code Reference:**
- [server.c](src/server.c#L93-L97) - Socket timeout configuration

```c
struct timeval tv;
tv.tv_sec = ctx->config->timeout_seconds;
tv.tv_usec = 0;
setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
```

---

## Additional Technical Details

### Queue Implementation: Circular Buffer with Semaphores

The connection queue uses a **bounded circular buffer** with the **Producer-Consumer pattern**:

**Code Reference:**
- [connection_queue.h](src/connection_queue.h#L13-L21) - Queue structure definition

```c
typedef struct {
    int connections[QUEUE_SIZE];
    int head;  // Consumer reads from here
    int tail;  // Producer writes here
    
    // Semaphores for synchronization
    sem_t empty_slots;  // Counts available slots
    sem_t filled_slots; // Counts filled slots
    sem_t mutex;        // Mutual exclusion for head/tail updates
    
    int shutdown;
} connection_queue_t;
```

### File Cache: LRU with Doubly-Linked List

The cache maintains a **doubly-linked list** where:
- **Head:** Most recently used (MRU)
- **Tail:** Least recently used (LRU)
- **Eviction:** Remove from tail when space is needed

**Code Reference:**
- [file_cache.h](src/file_cache.h#L15-L23) - Cache entry structure

```c
typedef struct cache_entry {
    char path[MAX_PATH_LEN];       // File path (key)
    char* content;                  // File content
    size_t content_size;            // Size of content in bytes
    time_t last_access;             // Last access time (for LRU)
    struct cache_entry* prev;       // Doubly-linked list for LRU
    struct cache_entry* next;
} cache_entry_t;
```

### Statistics: Shared Memory with Process-Shared Semaphore

Statistics are stored in **shared memory** (`mmap()` with `MAP_SHARED | MAP_ANONYMOUS`) and protected by a **process-shared semaphore**.

**Code Reference:**
- [stats.c](src/stats.c#L10-L33) - Shared memory initialization

```c
global_stats = mmap(NULL, sizeof(server_stats_t), 
                   PROT_READ | PROT_WRITE, 
                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
if (global_stats == MAP_FAILED) {
    perror("mmap failed for statistics");
    return -1;
}
// Initialize fields...
sem_init(&global_stats->semaphore, 1, 1);  // 1 = shared between processes
```

---

## Summary

This HTTP server demonstrates several advanced systems programming concepts:

1. **Prefork architecture** for process isolation and scalability
2. **SO_REUSEPORT** for kernel-level load balancing
3. **Bounded queues** with semaphores for backpressure
4. **LRU caching** with reader-writer locks
5. **Shared memory** statistics across processes
6. **Zero-copy I/O** with `sendfile()`
7. **Priority endpoints** bypassing queues
8. **Graceful shutdown** with proper cleanup

All synchronization is carefully designed to avoid deadlocks while maintaining high performance under load.
