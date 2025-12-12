# LRU File Cache Implementation Summary

## Overview
Successfully implemented a per-worker LRU (Least Recently Used) file cache for the HTTP server with the following features:

## Implementation Details

### 1. Configuration (`server.conf`)
- Added `CACHE_SIZE_MB=10` parameter to configure maximum cache size per worker
- Default value: 10MB per worker

### 2. Core Cache Module

#### `file_cache.h`
- Cache entry structure with:
  - File path (key)
  - File content (data)
  - Content size
  - Last access timestamp
  - Doubly-linked list pointers for LRU ordering
- Cache configuration:
  - `MAX_FILE_SIZE`: 1MB (files larger than this are not cached)
  - Maximum cache size: configurable via `CACHE_SIZE_MB`
- Thread-safe API using pthread_rwlock

#### `file_cache.c`
- **LRU Algorithm**: Doubly-linked list implementation
  - Head = Most Recently Used (MRU)
  - Tail = Least Recently Used (LRU)
- **Thread Safety**: Reader-writer locks (`pthread_rwlock`)
  - Multiple threads can read simultaneously
  - Exclusive write lock for cache modifications
- **Cache Operations**:
  - `file_cache_get()`: Retrieve cached file (moves to MRU on hit)
  - `file_cache_put()`: Add file to cache (evicts LRU if needed)
  - `file_cache_stats()`: Get cache statistics
  - `file_cache_init()`: Initialize cache per worker
  - `file_cache_destroy()`: Clean up cache resources

### 3. Integration Points

#### `config.h` / `config.c`
- Added `cache_size_mb` field to `server_config_t`
- Parser reads `CACHE_SIZE_MB` from config file
- Default value: 10MB

#### `server.c`
- Each worker process initializes its own cache instance
- Cache pointer passed to all threads within the worker
- Cache statistics logged on worker shutdown
- Proper cleanup on process termination

#### `http.c`
- Modified `send_file_response()` to check cache first
- Cache hit: Send from memory (fast path)
  - Adds `X-Cache: HIT` header
- Cache miss: Read from disk
  - For small files (< 1MB): Cache the content
  - For large files: Use sendfile() syscall
  - Adds `X-Cache: MISS` header
- Modified `handle_client_connection()` to pass cache to file handler

#### `Makefile`
- Added `file_cache.c` to source files list

## Architecture

### Per-Worker Cache Design
```
Main Process
├── Worker 1 (Process)
│   ├── File Cache (10MB)
│   └── Thread Pool (10 threads)
│       └── All threads share same cache via rwlock
├── Worker 2 (Process)
│   ├── File Cache (10MB)
│   └── Thread Pool (10 threads)
│       └── All threads share same cache via rwlock
├── Worker 3 (Process)
│   ├── File Cache (10MB)
│   └── Thread Pool (10 threads)
│       └── All threads share same cache via rwlock
└── Worker 4 (Process)
    ├── File Cache (10MB)
    └── Thread Pool (10 threads)
        └── All threads share same cache via rwlock
```

### Cache Eviction Strategy
1. When adding new entry, check if space is available
2. If not enough space: evict entries from tail (LRU)
3. Continue evicting until sufficient space exists
4. Add new entry at head (MRU position)

### Thread Safety
- **Read operations** (cache hits): Multiple threads can read simultaneously
- **Write operations** (cache misses/evictions): Exclusive lock required
- **LRU updates** (on cache hit): Requires write lock to move entry to front

## Performance Benefits

1. **Reduced I/O**: Small, frequently-accessed files served from memory
2. **Lower Latency**: Memory access is much faster than disk I/O
3. **Scalability**: Each worker has independent cache (no cross-process contention)
4. **Efficient Thread Sharing**: Reader-writer locks allow concurrent reads

## Files Modified/Created

### Created:
- `src/file_cache.h` - Cache interface and structures
- `src/file_cache.c` - LRU cache implementation

### Modified:
- `server.conf` - Added CACHE_SIZE_MB=10
- `src/config.h` - Added cache_size_mb to config structure
- `src/config.c` - Added config parsing for cache size
- `src/server.h` - No changes (interface unchanged)
- `src/server.c` - Initialize cache per worker, pass to threads
- `src/http.h` - Updated function signatures to accept cache
- `src/http.c` - Integrated cache into file serving logic
- `Makefile` - Added file_cache.c to build

## Testing Recommendations

1. **Verify cache hits**: Check logs for "Cache: HIT" messages
2. **Verify eviction**: Fill cache beyond 10MB, verify LRU eviction
3. **Verify concurrency**: Run load tests to ensure thread safety
4. **Verify X-Cache headers**: Check HTTP responses for cache status
5. **Monitor cache statistics**: Check worker shutdown logs for final cache stats

## Example Log Output

```
Worker 0: File cache initialized (10 MB)
Cache: PUT '/var/www/html/index.html' (512 bytes) - Total: 1 entries, 512/10485760 bytes
Cache: HIT '/var/www/html/index.html' (512 bytes)
Cache: PUT '/var/www/html/style.css' (2048 bytes) - Total: 2 entries, 2560/10485760 bytes
Cache: Evicted LRU entry '/var/www/html/old-file.js' (102400 bytes)
Worker 0: Final cache stats - 15 entries, 8945234 bytes
```

## Configuration Example

To adjust cache size per worker, edit `server.conf`:
```bash
# Increase to 20MB per worker
CACHE_SIZE_MB=20

# Disable caching (set to 0)
CACHE_SIZE_MB=0
```
