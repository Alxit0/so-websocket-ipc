#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>

// ============================================================================
// Thread Pool Structures
// ============================================================================
typedef struct work_item {
    int client_fd;
    struct work_item* next;
} work_item_t;

typedef struct {
    work_item_t* head;
    work_item_t* tail;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int shutdown;
    int active_threads;
} work_queue_t;

// ============================================================================
// Work Queue Functions
// ============================================================================
void work_queue_init(work_queue_t* queue);
void work_queue_push(work_queue_t* queue, int client_fd);
int work_queue_pop(work_queue_t* queue);
void work_queue_shutdown(work_queue_t* queue);
void work_queue_destroy(work_queue_t* queue);

#endif // THREAD_POOL_H
