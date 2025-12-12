// src/thread_pool.c
// Thread pool implementation for connection queue

#include "thread_pool.h"
#include <stdlib.h>

// ============================================================================
// Thread Pool Functions
// ============================================================================
void thread_pool_init(thread_pool_t* pool, connection_queue_t* queue) {
    pool->queue = queue;
    pool->active_threads = 0;
    pthread_mutex_init(&pool->active_mutex, NULL);
}

void thread_pool_destroy(thread_pool_t* pool) {
    pthread_mutex_destroy(&pool->active_mutex);
}

int thread_pool_get_active_threads(thread_pool_t* pool) {
    pthread_mutex_lock(&pool->active_mutex);
    int count = pool->active_threads;
    pthread_mutex_unlock(&pool->active_mutex);
    return count;
}

void thread_pool_increment_active(thread_pool_t* pool) {
    pthread_mutex_lock(&pool->active_mutex);
    pool->active_threads++;
    pthread_mutex_unlock(&pool->active_mutex);
}

void thread_pool_decrement_active(thread_pool_t* pool) {
    pthread_mutex_lock(&pool->active_mutex);
    pool->active_threads--;
    pthread_mutex_unlock(&pool->active_mutex);
}
