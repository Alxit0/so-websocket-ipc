// producer-Consumer connection queue with semaphores

#include "connection_queue.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

// ============================================================================
// Initialize Connection Queue
// ============================================================================
int connection_queue_init(connection_queue_t* queue) {
    if (!queue) {
        return -1;
    }
    
    // init circular buffer indices
    queue->head = 0;
    queue->tail = 0;
    queue->shutdown = 0;
    
    memset(queue->connections, -1, sizeof(queue->connections));
    
    // init semaphores
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
    
    log_message("Connection queue initialized (size: %d)", QUEUE_SIZE);
    return 0;
}

// ============================================================================
// Enqueue Connection (Producer - Blocking)
// ============================================================================
int connection_queue_enqueue(connection_queue_t* queue, int client_fd) {
    if (!queue || client_fd < 0) {
        return -1;
    }
    
    // Wait for an empty slot
    if (sem_wait(&queue->empty_slots) != 0) {
        return -1;
    }
    
    // Check if shutdown was signaled
    if (queue->shutdown) {
        sem_post(&queue->empty_slots);  // Release the slot
        return -1;
    }
    
    // Enter critical section
    sem_wait(&queue->mutex);
    
    // Add connection to the queue
    queue->connections[queue->tail] = client_fd;
    queue->tail = (queue->tail + 1) % QUEUE_SIZE;
    
    // Exit critical section
    sem_post(&queue->mutex);
    
    // Signal that a slot is now filled
    sem_post(&queue->filled_slots);
    
    return 0;
}

// ============================================================================
// Try Enqueue Without Blocking (for 503 handling)
// ============================================================================
int connection_queue_try_enqueue(connection_queue_t* queue, int client_fd) {
    if (!queue || client_fd < 0) {
        return -1;
    }
    
    // Try to acquire an empty slot without blocking
    if (sem_trywait(&queue->empty_slots) != 0) {
        // queue is full
        return -1;
    }
    
    // need to check if shutdown was signaled
    if (queue->shutdown) {
        sem_post(&queue->empty_slots);  // Release the slot
        return -1;
    }
    
    // Enter critical section
    sem_wait(&queue->mutex);
    
    // Add connection to the queue
    queue->connections[queue->tail] = client_fd;
    queue->tail = (queue->tail + 1) % QUEUE_SIZE;
    
    // Exit critical section
    sem_post(&queue->mutex);
    
    // Signal that a slot is now filled
    sem_post(&queue->filled_slots);
    
    return 0;
}

// ============================================================================
// Dequeue Connection (Consumer - Blocking)
// ============================================================================
int connection_queue_dequeue(connection_queue_t* queue) {
    if (!queue) {
        return -1;
    }
    
    // Wait for a filled slot
    if (sem_wait(&queue->filled_slots) != 0) {
        return -1;
    }
    
    // Check if shutdown was signaled
    if (queue->shutdown) {
        sem_post(&queue->filled_slots);  // Keep the semaphore count correct
        return -1;
    }
    
    // Enter critical section
    sem_wait(&queue->mutex);
    
    // Remove connection from the queue
    int client_fd = queue->connections[queue->head];
    queue->connections[queue->head] = -1;
    queue->head = (queue->head + 1) % QUEUE_SIZE;
    
    // Exit critical section
    sem_post(&queue->mutex);
    
    // Signal that a slot is now empty
    sem_post(&queue->empty_slots);
    
    return client_fd;
}

// ============================================================================
// Get Queue Size
// ============================================================================
int connection_queue_size(connection_queue_t* queue) {
    if (!queue) {
        return -1;
    }
    
    int size;
    sem_wait(&queue->mutex);
    
    // Calculate size from head and tail
    if (queue->tail >= queue->head) {
        size = queue->tail - queue->head;
    } else {
        size = QUEUE_SIZE - queue->head + queue->tail;
    }
    
    sem_post(&queue->mutex);
    return size;
}

// ============================================================================
// Shutdown Queue
// ============================================================================
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

// ============================================================================
// Destroy Queue
// ============================================================================
void connection_queue_destroy(connection_queue_t* queue) {
    if (!queue) {
        return;
    }
    
    // Close any remaining connections
    sem_wait(&queue->mutex);
    for (int i = 0; i < QUEUE_SIZE; i++) {
        if (queue->connections[i] >= 0) {
            close(queue->connections[i]);
            queue->connections[i] = -1;
        }
    }
    sem_post(&queue->mutex);
    
    // Destroy semaphores
    sem_destroy(&queue->empty_slots);
    sem_destroy(&queue->filled_slots);
    sem_destroy(&queue->mutex);
    
    log_message("Connection queue destroyed");
}
