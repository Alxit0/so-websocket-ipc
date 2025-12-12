// src/stats.c
// Shared memory statistics management

#include "stats.h"
#include "logger.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>

static server_stats_t* global_stats = NULL;

// ============================================================================
// Initialize Statistics
// ============================================================================
int init_stats(void) {
    global_stats = mmap(NULL, sizeof(server_stats_t), 
                       PROT_READ | PROT_WRITE, 
                       MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (global_stats == MAP_FAILED) {
        perror("mmap failed for statistics");
        return -1;
    }
    global_stats->total_requests = 0;
    global_stats->bytes_sent = 0;
    sem_init(&global_stats->semaphore, 1, 1);  // 1 = compartilhado entre processos
    return 0;
}

// ============================================================================
// Cleanup Statistics
// ============================================================================
void cleanup_stats(void) {
    if (global_stats) {
        sem_destroy(&global_stats->semaphore);
        munmap(global_stats, sizeof(server_stats_t));
        global_stats = NULL;
    }
}

// ============================================================================
// Update Statistics Function
// ============================================================================
void update_stats(int bytes) {
    if (!global_stats) return;  // Verificar se está inicializado
    
    sem_wait(&global_stats->semaphore);  // Proteger com semáforo
    
    global_stats->total_requests++;
    global_stats->bytes_sent += bytes;
    
    // Mostrar estatísticas a cada 15 pedidos
    if (global_stats->total_requests % 15 == 0) {
        log_message("STATS: Requests=%d, Bytes=%d", 
                   global_stats->total_requests, global_stats->bytes_sent);
    }
    
    sem_post(&global_stats->semaphore);  // Liberar semáforo
}

// ============================================================================
// Print Global Statistics
// ============================================================================
void print_global_stats(void) {
    if (global_stats) {
        sem_wait(&global_stats->semaphore);
        log_message("=== GLOBAL STATISTICS ===");
        log_message("Total requests: %d", global_stats->total_requests);
        log_message("Total bytes sent: %d", global_stats->bytes_sent);
        log_message("=========================");
        sem_post(&global_stats->semaphore);
    }
}

// ============================================================================
// Get Statistics Pointer
// ============================================================================
server_stats_t* get_stats(void) {
    return global_stats;
}
