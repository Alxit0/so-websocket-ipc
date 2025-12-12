// src/http.c
// HTTP request/response handling

#define _GNU_SOURCE
#include "http.h"
#include "stats.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#define BUF_SIZE 8192
#define MAX_PATH 4096

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
// HTTP Response Builder
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
// HTTP Request Parser
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
        update_stats_with_code(strlen(body), 404);
        return;
    }

    // Get file size
    int fd = fileno(file);
    struct stat st;
    if (fstat(fd, &st) < 0) {
        fclose(file);
        const char* body = "<h1>500 Internal Server Error</h1>";
        send_http_response(client_fd, 500, "Internal Server Error", "text/html", body, strlen(body));
        update_stats_with_code(strlen(body), 500);
        return;
    }

    // Check if it's a directory
    if (S_ISDIR(st.st_mode)) {
        fclose(file);
        const char* body = "<h1>403 Forbidden</h1>";
        send_http_response(client_fd, 403, "Forbidden", "text/html", body, strlen(body));
        update_stats_with_code(strlen(body), 500);
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
    update_stats_with_code(file_size, 200);
}

// ============================================================================
// Handle Client Connection
// ============================================================================
void handle_client_connection(int client_fd, const server_config_t* config) {
    increment_active_connections();
    
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    char buffer[BUF_SIZE];
    ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_read <= 0) {
        close(client_fd);
        decrement_active_connections();
        return;
    }
    
    buffer[bytes_read] = '\0';

    // Parse HTTP request
    http_request_t req;
    if (parse_http_request(buffer, &req) < 0) {
        const char* body = "<h1>400 Bad Request</h1>";
        send_http_response(client_fd, 400, "Bad Request", "text/html", body, strlen(body));
        update_stats_with_code(strlen(body), 500);
        close(client_fd);
        return;
    }

    // Only support GET and HEAD
    if (strcmp(req.method, "GET") != 0 && strcmp(req.method, "HEAD") != 0) {
        const char* body = "<h1>501 Not Implemented</h1>";
        send_http_response(client_fd, 501, "Not Implemented", "text/html", body, strlen(body));
        update_stats_with_code(strlen(body), 500);
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
            update_stats_with_code(strlen(body), 500);
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
    
    // Calculate and record response time
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    long long response_time_ms = (end_time.tv_sec - start_time.tv_sec) * 1000LL +
                                  (end_time.tv_nsec - start_time.tv_nsec) / 1000000LL;
    add_response_time(response_time_ms);
    
    decrement_active_connections();
}
