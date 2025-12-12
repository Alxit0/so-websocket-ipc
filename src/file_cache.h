#ifndef FILE_CACHE_H
#define FILE_CACHE_H

#include <pthread.h>
#include <stddef.h>
#include <time.h>

// ============================================================================
// File Cache Configuration
// ============================================================================
#define MAX_FILE_SIZE (1024 * 1024)  // 1MB max file size for caching
#define MAX_PATH_LEN 512

// ============================================================================
// Cache Entry Structure
// ============================================================================
typedef struct cache_entry {
    char path[MAX_PATH_LEN];       // File path (key)
    char* content;                  // File content
    size_t content_size;            // Size of content in bytes
    time_t last_access;             // Last access time (for LRU)
    struct cache_entry* prev;       // Doubly-linked list for LRU
    struct cache_entry* next;
} cache_entry_t;

// ============================================================================
// File Cache Structure (per worker)
// ============================================================================
typedef struct {
    cache_entry_t* head;            // Most recently used
    cache_entry_t* tail;            // Least recently used
    size_t total_size;              // Total cache size in bytes
    size_t max_size;                // Maximum cache size in bytes
    int entry_count;                // Number of entries
    pthread_rwlock_t lock;          // Reader-writer lock
} file_cache_t;

// ============================================================================
// File Cache Functions
// ============================================================================

/**
 * Initialize the file cache for a worker process
 */
int file_cache_init(file_cache_t* cache, int max_size_mb);

/**
 * Destroy the file cache and free all resources
 */
void file_cache_destroy(file_cache_t* cache);

/**
 * Get a file from the cache
 */
int file_cache_get(file_cache_t* cache, const char* path, 
                   const char** content, size_t* content_size);

/**
 * Put a file into the cache
 */
int file_cache_put(file_cache_t* cache, const char* path, 
                   const char* content, size_t content_size);

/**
 * Get cache statistics
 */
void file_cache_stats(file_cache_t* cache, int* entries, size_t* total_size);

#endif // FILE_CACHE_H
