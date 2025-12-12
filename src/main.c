// src/main.c
// Prefork HTTP server with shared memory statistics
// Compile: gcc -o server main.c -Wall -Wextra -pthread -lrt
// Run: ./server [config_file]

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>   // <-- ADICIONADO para shared memory
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>    // <-- ADICIONADO para threads
#include <semaphore.h>  // <-- ADICIONADO para semáforos

#define BACKLOG 128
#define BUF_SIZE 8192
#define MAX_PATH 4096
#define THREADS_PER_WORKER 10  // <-- ADICIONADO para thread pool

// ============================================================================
// Configuration Structure (based on template)
// ============================================================================
typedef struct {
    int port;
    char document_root[256];
    int num_workers;
    int timeout_seconds;
} server_config_t;

// ============================================================================
// HTTP Request/Response Structures (based on template)
// ============================================================================
typedef struct {
    char method[16];
    char path[512];
    char version[16];
} http_request_t;

// ============================================================================
// Statistics Structure (NEW - para estatísticas compartilhadas)
// ============================================================================
typedef struct {
    int total_requests;
    int bytes_sent;
    sem_t semaphore;  // Semáforo para proteção
} server_stats_t;

server_stats_t* global_stats = NULL;  // Ponteiro para shared memory

static volatile sig_atomic_t keep_running = 1;

// ============================================================================
// Configuration Loader (based on template)
// ============================================================================
int load_config(const char* filename, server_config_t* config) {
    // Set defaults
    config->port = 8080;
    strncpy(config->document_root, "/var/www/html", sizeof(config->document_root));
    config->num_workers = 4;
    config->timeout_seconds = 30;

    FILE* fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Could not open config '%s', using defaults\n", filename);
        return -1;
    }

    char line[512], key[128], value[256];
    while (fgets(line, sizeof(line), fp)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n') continue;

        if (sscanf(line, "%[^=]=%s", key, value) == 2) {
            // Trim whitespace from key
            char* k = key;
            while (*k == ' ' || *k == '\t') k++;
            char* k_end = k + strlen(k) - 1;
            while (k_end > k && (*k_end == ' ' || *k_end == '\t')) {
                *k_end = '\0';
                k_end--;
            }

            // Trim whitespace from value
            char* v = value;
            while (*v == ' ' || *v == '\t') v++;
            char* v_end = v + strlen(v) - 1;
            while (v_end > v && (*v_end == ' ' || *v_end == '\t' || *v_end == '\n' || *v_end == '\r')) {
                *v_end = '\0';
                v_end--;
            }

            if (strcmp(k, "PORT") == 0) config->port = atoi(v);
            else if (strcmp(k, "NUM_WORKERS") == 0) config->num_workers = atoi(v);
            else if (strcmp(k, "TIMEOUT_SECONDS") == 0) config->timeout_seconds = atoi(v);
            else if (strcmp(k, "DOCUMENT_ROOT") == 0) 
                strncpy(config->document_root, v, sizeof(config->document_root) - 1);
        }
    }
    fclose(fp);
    return 0;
}

// ============================================================================
// Thread-Safe Logger (based on template)
// ============================================================================
void log_message(const char* format, ...) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%d/%b/%Y:%H:%M:%S %z", tm_info);

    fprintf(stderr, "[%s] ", timestamp);
    
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    
    fprintf(stderr, "\n");
}

// ============================================================================
// Update Statistics Function (NEW - para estatísticas com semáforo)
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
// Signal Handler
// ============================================================================
void signal_handler(int signum) {
    (void)signum;
    keep_running = 0;
}

// ============================================================================
// MIME Type Detection
// ============================================================================
const char* get_mime_type(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    
    if (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0) return "text/html";
    if (strcasecmp(ext, ".css") == 0) return "text/css";
    if (strcasecmp(ext, ".js") == 0) return "application/javascript";
    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcasecmp(ext, ".png") == 0) return "image/png";
    if (strcasecmp(ext, ".gif") == 0) return "image/gif";
    if (strcasecmp(ext, ".svg") == 0) return "image/svg+xml";
    if (strcasecmp(ext, ".txt") == 0) return "text/plain";
    if (strcasecmp(ext, ".json") == 0) return "application/json";
    
    return "application/octet-stream";
}

// ============================================================================
// HTTP Response Builder (based on template)
// ============================================================================
void send_http_response(int fd, int status, const char* status_msg,
                       const char* content_type, const char* body, size_t body_len) {
    char header[2048];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Server: TemplateHTTP/1.0\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, status_msg, content_type, body_len);

    send(fd, header, header_len, 0);
    if (body && body_len > 0) {
        send(fd, body, body_len, 0);
    }
}

// ============================================================================
// HTTP Request Parser (based on template)
// ============================================================================
int parse_http_request(const char* buffer, http_request_t* req) {
    char* line_end = strstr(buffer, "\r\n");
    if (!line_end) return -1;

    char first_line[1024];
    size_t len = line_end - buffer;
    if (len >= sizeof(first_line)) return -1;
    
    strncpy(first_line, buffer, len);
    first_line[len] = '\0';

    if (sscanf(first_line, "%15s %511s %15s", req->method, req->path, req->version) != 3) {
        return -1;
    }

    return 0;
}

// ============================================================================
// Send File with sendfile() optimization
// ============================================================================
void send_file_response(int client_fd, const char* full_path, const char* method) {
    // Open file
    FILE* file = fopen(full_path, "rb");
    if (!file) {
        const char* body = "<h1>404 Not Found</h1>";
        send_http_response(client_fd, 404, "Not Found", "text/html", body, strlen(body));
        update_stats(strlen(body));  // Atualizar estatísticas para erro 404
        return;
    }

    // Get file size
    int fd = fileno(file);
    struct stat st;
    if (fstat(fd, &st) < 0) {
        fclose(file);
        const char* body = "<h1>500 Internal Server Error</h1>";
        send_http_response(client_fd, 500, "Internal Server Error", "text/html", body, strlen(body));
        update_stats(strlen(body));  // Atualizar estatísticas para erro 500
        return;
    }

    // Check if it's a directory
    if (S_ISDIR(st.st_mode)) {
        fclose(file);
        const char* body = "<h1>403 Forbidden</h1>";
        send_http_response(client_fd, 403, "Forbidden", "text/html", body, strlen(body));
        update_stats(strlen(body));  // Atualizar estatísticas para erro 403
        return;
    }

    const char* mime = get_mime_type(full_path);
    long file_size = st.st_size;

    // Send headers
    char header[512];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Server: TemplateHTTP/1.0\r\n"
        "Connection: close\r\n"
        "\r\n", mime, file_size);
    
    send(client_fd, header, header_len, 0);

    // Send file content (skip for HEAD requests)
    if (strcmp(method, "HEAD") != 0) {
        off_t offset = 0;
        while (offset < file_size) {
            ssize_t sent = sendfile(client_fd, fd, &offset, file_size - offset);
            if (sent <= 0) {
                if (errno == EINTR) continue;
                break;
            }
        }
    }

    fclose(file);
    update_stats(file_size);  // Atualizar estatísticas para sucesso
}

// ============================================================================
// Handle Client Connection
// ============================================================================
void handle_client_connection(int client_fd, const server_config_t* config) {
    char buffer[BUF_SIZE];
    ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_read <= 0) {
        close(client_fd);
        return;
    }
    
    buffer[bytes_read] = '\0';

    // Parse HTTP request
    http_request_t req;
    if (parse_http_request(buffer, &req) < 0) {
        const char* body = "<h1>400 Bad Request</h1>";
        send_http_response(client_fd, 400, "Bad Request", "text/html", body, strlen(body));
        update_stats(strlen(body));
        close(client_fd);
        return;
    }

    // Only support GET and HEAD
    if (strcmp(req.method, "GET") != 0 && strcmp(req.method, "HEAD") != 0) {
        const char* body = "<h1>501 Not Implemented</h1>";
        send_http_response(client_fd, 501, "Not Implemented", "text/html", body, strlen(body));
        update_stats(strlen(body));
        close(client_fd);
        return;
    }

    // Sanitize path
    char rel_path[MAX_PATH];
    if (strcmp(req.path, "/") == 0) {
        snprintf(rel_path, sizeof(rel_path), "/index.html");
    } else {
        // Remove query string
        char* query = strchr(req.path, '?');
        if (query) *query = '\0';
        
        // Reject path traversal attempts
        if (strstr(req.path, "..")) {
            const char* body = "<h1>403 Forbidden</h1>";
            send_http_response(client_fd, 403, "Forbidden", "text/html", body, strlen(body));
            update_stats(strlen(body));
            close(client_fd);
            return;
        }
        snprintf(rel_path, sizeof(rel_path), "%s", req.path);
    }

    // Build full path
    char full_path[MAX_PATH];
    snprintf(full_path, sizeof(full_path), "%s%s", config->document_root, rel_path);

    log_message("Request: %s %s -> %s", req.method, req.path, full_path);

    // Serve the file
    send_file_response(client_fd, full_path, req.method);

    close(client_fd);
}

// ============================================================================
// Create Server Socket (based on template)
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
// Worker Process Loop
// ============================================================================
void worker_process(int server_fd, int worker_id, const server_config_t* config) {
    log_message("Worker %d started (PID: %d)", worker_id, getpid());

    while (keep_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            log_message("Worker %d: accept error: %s", worker_id, strerror(errno));
            continue;
        }

        // Set socket timeouts
        struct timeval tv;
        tv.tv_sec = config->timeout_seconds;
        tv.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        // Handle the connection
        handle_client_connection(client_fd, config);
    }

    log_message("Worker %d exiting", worker_id);
}

// ============================================================================
// Main Function (based on template master process)
// ============================================================================
int main(int argc, char** argv) {
    // Load configuration
    const char* config_file = (argc > 1) ? argv[1] : "server.conf";
    server_config_t config;
    load_config(config_file, &config);

    // Initialize statistics with shared memory (NEW)
    global_stats = mmap(NULL, sizeof(server_stats_t), 
                       PROT_READ | PROT_WRITE, 
                       MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (global_stats == MAP_FAILED) {
        perror("mmap failed for statistics");
        exit(EXIT_FAILURE);
    }
    global_stats->total_requests = 0;
    global_stats->bytes_sent = 0;
    sem_init(&global_stats->semaphore, 1, 1);  // 1 = compartilhado entre processos

    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGCHLD, SIG_IGN);

    // Create server socket
    int server_fd = create_server_socket(config.port);
    if (server_fd < 0) {
        fprintf(stderr, "Failed to create server socket\n");
        exit(EXIT_FAILURE);
    }

    log_message("Master process listening on port %d", config.port);
    log_message("Document root: %s", config.document_root);
    log_message("Number of workers: %d", config.num_workers);

    // Fork worker processes
    pid_t* worker_pids = malloc(sizeof(pid_t) * config.num_workers);
    
    for (int i = 0; i < config.num_workers; i++) {
        pid_t pid = fork();
        
        if (pid < 0) {
            perror("fork failed");
            exit(EXIT_FAILURE);
        }
        
        if (pid == 0) {
            // Child process - worker
            worker_process(server_fd, i, &config);
            close(server_fd);
            exit(EXIT_SUCCESS);
        } else {
            // Parent process - store worker PID
            worker_pids[i] = pid;
        }
    }

    // Master process - wait for shutdown signal
    while (keep_running) {
        sleep(1);
        
        // Show global statistics every 30 seconds (NEW)
        static int counter = 0;
        counter++;
        if (counter >= 30) {
            counter = 0;
            if (global_stats) {
                sem_wait(&global_stats->semaphore);
                log_message("=== GLOBAL STATISTICS ===");
                log_message("Total requests: %d", global_stats->total_requests);
                log_message("Total bytes sent: %d", global_stats->bytes_sent);
                log_message("=========================");
                sem_post(&global_stats->semaphore);
            }
        }
        
        // Reap any dead children
        int status;
        pid_t pid;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            log_message("Reaped child process %d", pid);
        }
    }

    // Shutdown: send SIGTERM to all workers
    log_message("Master shutting down, terminating workers...");
    for (int i = 0; i < config.num_workers; i++) {
        if (worker_pids[i] > 0) {
            kill(worker_pids[i], SIGTERM);
        }
    }

    // Wait for all workers to exit
    for (int i = 0; i < config.num_workers; i++) {
        if (worker_pids[i] > 0) {
            waitpid(worker_pids[i], NULL, 0);
        }
    }

    close(server_fd);
    free(worker_pids);
    
    // Cleanup shared memory and semaphore (NEW)
    if (global_stats) {
        sem_destroy(&global_stats->semaphore);
        munmap(global_stats, sizeof(server_stats_t));
        global_stats = NULL;
    }
    
    log_message("Shutdown complete");
    return EXIT_SUCCESS;
    
// Este servidor implementa arquitetura multi-process (master-worker)
// mas ainda não implementa thread pool completo. Cada worker processa
// conexões na thread principal.
}