#ifndef CONNECTION_QUEUE_H
#define CONNECTION_QUEUE_H

#include <semaphore.h>

// ============================================================================
// Connection Queue Configuration
// ============================================================================
#define QUEUE_SIZE 100  // Bounded circular buffer size

// ============================================================================
// Connection Queue Structure
// ============================================================================
typedef struct {
    int connections[QUEUE_SIZE];  // Circular buffer for connection file descriptors
    int head;                      // Index for dequeue (consumer)
    int tail;                      // Index for enqueue (producer)
    
    // Semaphores for synchronization
    sem_t empty_slots;            // Counts empty slots (producer waits on this)
    sem_t filled_slots;           // Counts filled slots (consumer waits on this)
    sem_t mutex;                  // Mutual exclusion for critical section
    
    int shutdown;                 // Shutdown flag
} connection_queue_t;

// ============================================================================
// Connection Queue Functions
// ============================================================================

/**
 * Initialize the connection queue
 * Returns: 0 on success, -1 on error
 */
int connection_queue_init(connection_queue_t* queue);

/**
 * Enqueue a connection (producer)
 * Returns: 0 on success, -1 if queue is full
 */
int connection_queue_enqueue(connection_queue_t* queue, int client_fd);

/**
 * Dequeue a connection (consumer)
 * Returns: client_fd on success, -1 if shutdown
 */
int connection_queue_dequeue(connection_queue_t* queue);

/**
 * Try to enqueue without blocking (for handling 503)
 * Returns: 0 on success, -1 if queue is full
 */
int connection_queue_try_enqueue(connection_queue_t* queue, int client_fd);

/**
 * Signal shutdown and wake up all waiting consumers
 */
void connection_queue_shutdown(connection_queue_t* queue);

/**
 * Destroy the connection queue and cleanup resources
 */
void connection_queue_destroy(connection_queue_t* queue);

/**
 * Get current queue size (for monitoring)
 * Returns: number of connections in queue
 */
int connection_queue_size(connection_queue_t* queue);

#endif // CONNECTION_QUEUE_H
