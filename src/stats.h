#ifndef STATS_H
#define STATS_H

#include <semaphore.h>

// ============================================================================
// Statistics Structure
// ============================================================================
typedef struct {
    int total_requests;
    int bytes_sent;
    sem_t semaphore;
} server_stats_t;

// ============================================================================
// Statistics Functions
// ============================================================================
int init_stats(void);
void cleanup_stats(void);
void update_stats(int bytes);
void print_global_stats(void);
server_stats_t* get_stats(void);

#endif // STATS_H
