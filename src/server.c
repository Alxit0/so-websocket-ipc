// src/server.c
// Server socket and worker process implementation

#define _GNU_SOURCE
#include "server.h"
#include "http.h"
#include "logger.h"
#include "thread_pool.h"
#include "file_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BACKLOG 128
#define THREADS_PER_WORKER 10

static volatile sig_atomic_t keep_running = 1;

// ============================================================================
// Create Server Socket
// ============================================================================
int create_server_socket(int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket failed");
        return -1;
    }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        close(sockfd);
        return -1;
    }

    if (listen(sockfd, BACKLOG) < 0) {
        perror("listen failed");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

// ============================================================================
// Thread Pool Worker Context
// ============================================================================
typedef struct {
    work_queue_t* queue;
    int worker_id;
    int thread_id;
    const server_config_t* config;
    file_cache_t* cache;
} thread_context_t;

// ============================================================================
// Thread Pool Worker Function
// ============================================================================
void* thread_worker(void* arg) {
    thread_context_t* ctx = (thread_context_t*)arg;
    
    log_message("Worker %d: Thread %d started (TID: %lu)", 
               ctx->worker_id, ctx->thread_id, (unsigned long)pthread_self());
    
    pthread_mutex_lock(&ctx->queue->mutex);
    ctx->queue->active_threads++;
    pthread_mutex_unlock(&ctx->queue->mutex);
    
    while (1) {
        int client_fd = work_queue_pop(ctx->queue);
        
        if (client_fd < 0) {
            // Shutdown signal
            break;
        }
        
        // Set socket timeouts
        struct timeval tv;
        tv.tv_sec = ctx->config->timeout_seconds;
        tv.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        
        // Handle the connection
        handle_client_connection(client_fd, ctx->config, ctx->cache);
    }
    
    pthread_mutex_lock(&ctx->queue->mutex);
    ctx->queue->active_threads--;
    pthread_mutex_unlock(&ctx->queue->mutex);
    
    log_message("Worker %d: Thread %d exiting", ctx->worker_id, ctx->thread_id);
    free(ctx);
    return NULL;
}

// ============================================================================
// Signal Handler
// ============================================================================
void worker_signal_handler(int signum) {
    (void)signum;
    keep_running = 0;
}

// ============================================================================
// Worker Process Loop (com Thread Pool)
// ============================================================================
void worker_process(int server_fd, int worker_id, const server_config_t* config) {
    // Setup signal handler for worker
    signal(SIGTERM, worker_signal_handler);
    signal(SIGINT, worker_signal_handler);
    
    log_message("Worker %d started (PID: %d) with %d threads", 
               worker_id, getpid(), THREADS_PER_WORKER);

    // Initialize file cache for this worker
    file_cache_t cache;
    if (file_cache_init(&cache, config->cache_size_mb) != 0) {
        log_message("Worker %d: Failed to initialize file cache", worker_id);
        return;
    }
    log_message("Worker %d: File cache initialized (%d MB)", worker_id, config->cache_size_mb);

    // Inicializar fila de trabalho
    work_queue_t queue;
    work_queue_init(&queue);
    
    // Criar thread pool
    pthread_t threads[THREADS_PER_WORKER];
    
    for (int i = 0; i < THREADS_PER_WORKER; i++) {
        thread_context_t* ctx = malloc(sizeof(thread_context_t));
        ctx->queue = &queue;
        ctx->worker_id = worker_id;
        ctx->thread_id = i;
        ctx->config = config;
        ctx->cache = &cache;
        
        if (pthread_create(&threads[i], NULL, thread_worker, ctx) != 0) {
            log_message("Worker %d: Failed to create thread %d", worker_id, i);
            free(ctx);
        }
    }
    
    log_message("Worker %d: Thread pool initialized", worker_id);

    // Loop principal: aceitar conexões e distribuir para threads
    while (keep_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            log_message("Worker %d: accept error: %s", worker_id, strerror(errno));
            continue;
        }

        // Adicionar trabalho à fila (threads vão processar)
        work_queue_push(&queue, client_fd);
    }

    // Shutdown gracioso do thread pool
    log_message("Worker %d: Initiating graceful shutdown", worker_id);
    work_queue_shutdown(&queue);
    
    // Aguardar todas as threads terminarem
    for (int i = 0; i < THREADS_PER_WORKER; i++) {
        pthread_join(threads[i], NULL);
    }
    
    work_queue_destroy(&queue);
    
    // Print cache statistics before destroying
    int entries;
    size_t total_size;
    file_cache_stats(&cache, &entries, &total_size);
    log_message("Worker %d: Final cache stats - %d entries, %zu bytes", 
                worker_id, entries, total_size);
    
    // Destroy file cache
    file_cache_destroy(&cache);
    
    log_message("Worker %d exiting (all threads terminated)", worker_id);
}
