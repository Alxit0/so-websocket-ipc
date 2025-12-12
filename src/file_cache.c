// LRU File Cache Implementation for Worker Processes

#include "file_cache.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// Internal Helper Functions
// ============================================================================

/**
 * Move an entry to the front (head) of the LRU list
 */
static void move_to_front(file_cache_t* cache, cache_entry_t* entry) {
    if (cache->head == entry) {
        // Already at front
        return;
    }
    
    // Remove from current position
    if (entry->prev) {
        entry->prev->next = entry->next;
    }
    if (entry->next) {
        entry->next->prev = entry->prev;
    }
    
    // Update tail if this was the tail
    if (cache->tail == entry) {
        cache->tail = entry->prev;
    }
    
    // Insert at front
    entry->prev = NULL;
    entry->next = cache->head;
    
    if (cache->head) {
        cache->head->prev = entry;
    }
    cache->head = entry;
    
    // Update tail if this was the only entry
    if (!cache->tail) {
        cache->tail = entry;
    }
}

/**
 * Find an entry by path in the cache
 */
static cache_entry_t* find_entry(file_cache_t* cache, const char* path) {
    cache_entry_t* current = cache->head;
    while (current) {
        if (strcmp(current->path, path) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

/**
 * Remove the least recently used entry (from tail)
 */
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

/**
 * Make space in the cache for new content
 */
static void make_space(file_cache_t* cache, size_t needed_size) {
    // Evict entries until we have enough space
    while (cache->total_size + needed_size > cache->max_size && cache->tail) {
        evict_lru(cache);
    }
}

// ============================================================================
// Public API Implementation
// ============================================================================

int file_cache_init(file_cache_t* cache, int max_size_mb) {
    if (!cache || max_size_mb <= 0) {
        return -1;
    }
    
    cache->head = NULL;
    cache->tail = NULL;
    cache->total_size = 0;
    cache->max_size = max_size_mb * 1024 * 1024;  // Convert MB to bytes
    cache->entry_count = 0;
    
    if (pthread_rwlock_init(&cache->lock, NULL) != 0) {
        log_message("Cache: Failed to initialize rwlock");
        return -1;
    }
    
    log_message("Cache: Initialized with max size %d MB (%zu bytes)", 
                max_size_mb, cache->max_size);
    
    return 0;
}

void file_cache_destroy(file_cache_t* cache) {
    if (!cache) {
        return;
    }
    
    pthread_rwlock_wrlock(&cache->lock);
    
    // Free all entries
    cache_entry_t* current = cache->head;
    while (current) {
        cache_entry_t* next = current->next;
        free(current->content);
        free(current);
        current = next;
    }
    
    cache->head = NULL;
    cache->tail = NULL;
    cache->total_size = 0;
    cache->entry_count = 0;
    
    pthread_rwlock_unlock(&cache->lock);
    pthread_rwlock_destroy(&cache->lock);
    
    log_message("Cache: Destroyed");
}

int file_cache_get(file_cache_t* cache, const char* path, 
                   const char** content, size_t* content_size) {
    if (!cache || !path || !content || !content_size) {
        return -1;
    }
    
    pthread_rwlock_wrlock(&cache->lock);  // Write lock for LRU update
    
    cache_entry_t* entry = find_entry(cache, path);
    
    if (!entry) {
        pthread_rwlock_unlock(&cache->lock);
        return -1;  // Cache miss
    }
    
    // Update access time and move to front
    entry->last_access = time(NULL);
    move_to_front(cache, entry);
    
    // Return the cached content (caller should NOT free this)
    *content = entry->content;
    *content_size = entry->content_size;
    
    pthread_rwlock_unlock(&cache->lock);
    
    log_message("Cache: HIT '%s' (%zu bytes)", path, *content_size);
    
    return 0;  // Cache hit
}

int file_cache_put(file_cache_t* cache, const char* path, 
                   const char* content, size_t content_size) {
    if (!cache || !path || !content || content_size == 0) {
        return -1;
    }
    
    // Don't cache files larger than MAX_FILE_SIZE
    if (content_size > MAX_FILE_SIZE) {
        log_message("Cache: File '%s' too large (%zu bytes), not caching", 
                    path, content_size);
        return -1;
    }
    
    // Don't cache if larger than max cache size
    if (content_size > cache->max_size) {
        return -1;
    }
    
    pthread_rwlock_wrlock(&cache->lock);
    
    // Check if entry already exists
    cache_entry_t* existing = find_entry(cache, path);
    if (existing) {
        // Update existing entry
        char* new_content = malloc(content_size);
        if (!new_content) {
            pthread_rwlock_unlock(&cache->lock);
            return -1;
        }
        
        memcpy(new_content, content, content_size);
        
        // Update cache size
        cache->total_size -= existing->content_size;
        cache->total_size += content_size;
        
        free(existing->content);
        existing->content = new_content;
        existing->content_size = content_size;
        existing->last_access = time(NULL);
        
        move_to_front(cache, existing);
        
        pthread_rwlock_unlock(&cache->lock);
        
        log_message("Cache: UPDATED '%s' (%zu bytes)", path, content_size);
        return 0;
    }
    
    // Make space for new entry
    make_space(cache, content_size);
    
    // Create new entry
    cache_entry_t* new_entry = malloc(sizeof(cache_entry_t));
    if (!new_entry) {
        pthread_rwlock_unlock(&cache->lock);
        return -1;
    }
    
    new_entry->content = malloc(content_size);
    if (!new_entry->content) {
        free(new_entry);
        pthread_rwlock_unlock(&cache->lock);
        return -1;
    }
    
    // Copy data
    strncpy(new_entry->path, path, MAX_PATH_LEN - 1);
    new_entry->path[MAX_PATH_LEN - 1] = '\0';
    memcpy(new_entry->content, content, content_size);
    new_entry->content_size = content_size;
    new_entry->last_access = time(NULL);
    new_entry->prev = NULL;
    new_entry->next = cache->head;
    
    // Insert at front
    if (cache->head) {
        cache->head->prev = new_entry;
    }
    cache->head = new_entry;
    
    if (!cache->tail) {
        cache->tail = new_entry;
    }
    
    // Update cache statistics
    cache->total_size += content_size;
    cache->entry_count++;
    
    pthread_rwlock_unlock(&cache->lock);
    
    log_message("Cache: PUT '%s' (%zu bytes) - Total: %d entries, %zu/%zu bytes", 
                path, content_size, cache->entry_count, 
                cache->total_size, cache->max_size);
    
    return 0;
}

void file_cache_stats(file_cache_t* cache, int* entries, size_t* total_size) {
    if (!cache) {
        return;
    }
    
    pthread_rwlock_rdlock(&cache->lock);
    
    if (entries) {
        *entries = cache->entry_count;
    }
    if (total_size) {
        *total_size = cache->total_size;
    }
    
    pthread_rwlock_unlock(&cache->lock);
}
