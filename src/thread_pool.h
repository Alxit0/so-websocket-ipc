#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>
#include "connection_queue.h"

// ============================================================================
// Thread Pool Structures
// ============================================================================
typedef struct {
    connection_queue_t* queue;  // Connection queue with semaphores
    int active_threads;
    pthread_mutex_t active_mutex;  // Mutex for active_threads counter
} thread_pool_t;

// ============================================================================
// Thread Pool Functions
// ============================================================================
void thread_pool_init(thread_pool_t* pool, connection_queue_t* queue);
void thread_pool_destroy(thread_pool_t* pool);
int thread_pool_get_active_threads(thread_pool_t* pool);
void thread_pool_increment_active(thread_pool_t* pool);
void thread_pool_decrement_active(thread_pool_t* pool);

#endif // THREAD_POOL_H
