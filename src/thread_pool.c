// src/thread_pool.c
// Thread pool and work queue implementation

#include "thread_pool.h"
#include <stdlib.h>
#include <unistd.h>

// ============================================================================
// Work Queue Functions
// ============================================================================
void work_queue_init(work_queue_t* queue) {
    queue->head = NULL;
    queue->tail = NULL;
    queue->shutdown = 0;
    queue->active_threads = 0;
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);
}

void work_queue_push(work_queue_t* queue, int client_fd) {
    work_item_t* item = malloc(sizeof(work_item_t));
    item->client_fd = client_fd;
    item->next = NULL;

    pthread_mutex_lock(&queue->mutex);
    
    if (queue->tail) {
        queue->tail->next = item;
    } else {
        queue->head = item;
    }
    queue->tail = item;
    
    pthread_cond_signal(&queue->cond);  // Acordar uma thread
    pthread_mutex_unlock(&queue->mutex);
}

int work_queue_pop(work_queue_t* queue) {
    pthread_mutex_lock(&queue->mutex);
    
    // Bloquear enquanto não houver trabalho e não for shutdown
    while (!queue->head && !queue->shutdown) {
        pthread_cond_wait(&queue->cond, &queue->mutex);
    }
    
    // Se for shutdown e não houver trabalho, retornar -1
    if (queue->shutdown && !queue->head) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }
    
    work_item_t* item = queue->head;
    int client_fd = item->client_fd;
    queue->head = item->next;
    
    if (!queue->head) {
        queue->tail = NULL;
    }
    
    pthread_mutex_unlock(&queue->mutex);
    free(item);
    
    return client_fd;
}

void work_queue_shutdown(work_queue_t* queue) {
    pthread_mutex_lock(&queue->mutex);
    queue->shutdown = 1;
    pthread_cond_broadcast(&queue->cond);  // Acordar todas as threads
    pthread_mutex_unlock(&queue->mutex);
}

void work_queue_destroy(work_queue_t* queue) {
    // Limpar itens pendentes
    while (queue->head) {
        work_item_t* item = queue->head;
        queue->head = item->next;
        close(item->client_fd);
        free(item);
    }
    
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond);
}
