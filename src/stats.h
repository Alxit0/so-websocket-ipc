#ifndef STATS_H
#define STATS_H

#include <semaphore.h>

// ============================================================================
// Statistics Structure
// ============================================================================
typedef struct {
    int total_requests;
    int bytes_sent;
    
    // HTTP code counts
    int http_200_count;
    int http_404_count;
    int http_500_count;
    
    // Active connections
    int active_connections;
    
    // Response time tracking
    long long total_response_time_ms;  // soma total em milissegundos
    int response_count;                 // contador para calcular média
    
    sem_t semaphore;
} server_stats_t;

// ============================================================================
// Statistics Functions
// ============================================================================
int init_stats(void);
void cleanup_stats(void);
void update_stats(int bytes);
void update_stats_with_code(int bytes, int http_code);
void increment_active_connections(void);
void decrement_active_connections(void);
void add_response_time(long long time_ms);
void print_global_stats(void);
server_stats_t* get_stats(void);

#endif // STATS_H
