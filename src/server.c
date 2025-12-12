// Server socket and worker process implementation

#define _GNU_SOURCE
#include "server.h"
#include "http.h"
#include "logger.h"
#include "stats.h"
#include "thread_pool.h"
#include "connection_queue.h"
#include "file_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BACKLOG 128

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
    thread_pool_t* pool;
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
    
    thread_pool_increment_active(ctx->pool);
    
    while (1) {
        // Consumer: dequeue connection from bounded queue
        int client_fd = connection_queue_dequeue(ctx->pool->queue);
        
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
    
    thread_pool_decrement_active(ctx->pool);
    
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
// Send 503 Service Unavailable Response
// ============================================================================
void send_503_response(int client_fd) {
    const char* response = 
        "HTTP/1.1 503 Service Unavailable\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "Retry-After: 1\r\n"
        "\r\n"
        "<html><body><h1>503 Service Unavailable</h1>"
        "<p>Server is overloaded. Please try again later.</p>"
        "</body></html>";
    update_stats_with_code(strlen(response), 503);
    send(client_fd, response, strlen(response), 0);
    close(client_fd);
}

// ============================================================================
// Check if request is for a priority endpoint (metrics, health, stats)
// ============================================================================
int is_priority_endpoint(int client_fd) {
    char buffer[512];
    
    // Peek at the request without consuming it
    ssize_t bytes = recv(client_fd, buffer, sizeof(buffer) - 1, MSG_PEEK);
    if (bytes <= 0) {
        return 0;
    }
    
    buffer[bytes] = '\0';
    
    // Check if it's a GET/HEAD request for priority endpoints
    if (strstr(buffer, "GET /metrics") == buffer || 
        strstr(buffer, "HEAD /metrics") == buffer ||
        strstr(buffer, "GET /health") == buffer || 
        strstr(buffer, "HEAD /health") == buffer ||
        strstr(buffer, "GET /stats") == buffer || 
        strstr(buffer, "HEAD /stats") == buffer) {
        return 1;
    }
    
    return 0;
}

// ============================================================================
// Handle priority endpoints immediately (bypass queue)
// ============================================================================
void handle_priority_endpoint(int client_fd) {
    char buffer[512];
    ssize_t bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes <= 0) {
        close(client_fd);
        return;
    }
    
    buffer[bytes] = '\0';
    
    // Quick parse - just get the path
    char method[16], path[256];
    if (sscanf(buffer, "%15s %255s", method, path) != 2) {
        close(client_fd);
        return;
    }
    
    // Handle each priority endpoint
    if (strcmp(path, "/metrics") == 0 || strcmp(path, "/metrics/") == 0) {
        size_t response_len;
        char* body = generate_metrics_response(&response_len);
        
        char header[256];
        int header_len = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain; version=0.0.4\r\n"
            "Content-Length: %zu\r\n"
            "X-Priority: high\r\n"
            "Connection: close\r\n"
            "\r\n",
            response_len);
        
        send(client_fd, header, header_len, 0);
        if (strcmp(method, "GET") == 0) {
            send(client_fd, body, response_len, 0);
        }
        // Don't free - body is static buffer
        update_stats_with_code(response_len, 200);
    }
    else if (strcmp(path, "/health") == 0 || strcmp(path, "/health/") == 0) {
        size_t response_len;
        char* body = generate_health_response(&response_len);
        
        char header[256];
        int header_len = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n"
            "X-Priority: high\r\n"
            "Connection: close\r\n"
            "\r\n",
            response_len);
        
        send(client_fd, header, header_len, 0);
        if (strcmp(method, "GET") == 0) {
            send(client_fd, body, response_len, 0);
        }
        // Don't free - body is static buffer
        update_stats_with_code(response_len, 200);
    }
    else if (strcmp(path, "/stats") == 0 || strcmp(path, "/stats/") == 0) {
        size_t response_len;
        char* body = generate_stats_json_response(&response_len);
        
        char header[256];
        int header_len = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n"
            "X-Priority: high\r\n"
            "Connection: close\r\n"
            "\r\n",
            response_len);
        
        send(client_fd, header, header_len, 0);
        if (strcmp(method, "GET") == 0) {
            send(client_fd, body, response_len, 0);
        }
        // Don't free - body is static buffer
        update_stats_with_code(response_len, 200);
    }
    
    close(client_fd);
}

// ============================================================================
// Worker Process Loop (com Thread Pool)
// ============================================================================
void worker_process(int server_fd, int worker_id, const server_config_t* config) {
    // Setup signal handler for worker
    signal(SIGTERM, worker_signal_handler);
    signal(SIGINT, worker_signal_handler);
    
    log_message("Worker %d started (PID: %d) with %d threads", 
               worker_id, getpid(), config->threads_per_worker);

    // Initialize file cache for this worker (if enabled)
    file_cache_t cache;
    file_cache_t* cache_ptr = NULL;
    
    if (config->cache_size_mb > 0) {
        if (file_cache_init(&cache, config->cache_size_mb) != 0) {
            log_message("Worker %d: Failed to initialize file cache", worker_id);
            return;
        }
        cache_ptr = &cache;
        log_message("Worker %d: File cache initialized (%d MB)", worker_id, config->cache_size_mb);
    } else {
        log_message("Worker %d: File caching disabled (CACHE_SIZE_MB=0)", worker_id);
    }

    // Initialize connection queue (bounded circular buffer with semaphores)
    connection_queue_t conn_queue;
    if (connection_queue_init(&conn_queue) != 0) {
        log_message("Worker %d: Failed to initialize connection queue", worker_id);
        if (cache_ptr) file_cache_destroy(cache_ptr);
        return;
    }
    
    // Initialize thread pool
    thread_pool_t pool;
    thread_pool_init(&pool, &conn_queue);
    
    // Create worker threads
    pthread_t* threads = malloc(sizeof(pthread_t) * config->threads_per_worker);
    if (!threads) {
        log_message("Worker %d: Failed to allocate thread array", worker_id);
        connection_queue_destroy(&conn_queue);
        if (cache_ptr) file_cache_destroy(cache_ptr);
        return;
    }
    
    for (int i = 0; i < config->threads_per_worker; i++) {
        thread_context_t* ctx = malloc(sizeof(thread_context_t));
        ctx->pool = &pool;
        ctx->worker_id = worker_id;
        ctx->thread_id = i;
        ctx->config = config;
        ctx->cache = cache_ptr;
        
        if (pthread_create(&threads[i], NULL, thread_worker, ctx) != 0) {
            log_message("Worker %d: Failed to create thread %d", worker_id, i);
            free(ctx);
        }
    }
    
    log_message("Worker %d: Thread pool initialized with bounded queue (size: %d)", 
                worker_id, QUEUE_SIZE);

    // Producer: Accept connections and enqueue them
    unsigned long total_accepted = 0;
    unsigned long total_rejected = 0;
    unsigned long priority_handled = 0;
    
    while (keep_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            log_message("Worker %d: accept error: %s", worker_id, strerror(errno));
            continue;
        }

        total_accepted++;
        
        // Check if this is a priority endpoint (metrics, health, stats)
        // Priority endpoints bypass the queue and are handled immediately
        if (is_priority_endpoint(client_fd)) {
            priority_handled++;
            handle_priority_endpoint(client_fd);
            continue;
        }
        
        // Try to enqueue connection (non-blocking)
        if (connection_queue_try_enqueue(&conn_queue, client_fd) != 0) {
            // Queue is full - reject with 503
            total_rejected++;
            send_503_response(client_fd);
            
            // Log every 100 rejections to avoid log spam
            if (total_rejected % 100 == 1) {
                log_message("Worker %d: Queue full, rejected %lu connections so far", 
                           worker_id, total_rejected);
            }
        }
    }

    // Shutdown gracioso
    log_message("Worker %d: Initiating graceful shutdown (accepted: %lu, priority: %lu, rejected: %lu)", 
                worker_id, total_accepted, priority_handled, total_rejected);
    
    connection_queue_shutdown(&conn_queue);
    
    // Wait for all threads to finish
    for (int i = 0; i < config->threads_per_worker; i++) {
        pthread_join(threads[i], NULL);
    }
    
    free(threads);
    thread_pool_destroy(&pool);
    connection_queue_destroy(&conn_queue);
    
    // Print cache statistics before destroying (if cache was enabled)
    if (cache_ptr) {
        int entries;
        size_t total_size;
        file_cache_stats(&cache, &entries, &total_size);
        log_message("Worker %d: Final cache stats - %d entries, %zu bytes", 
                    worker_id, entries, total_size);
        
        // Destroy file cache
        file_cache_destroy(&cache);
    }
    
    log_message("Worker %d exiting (all threads terminated)", worker_id);
}
