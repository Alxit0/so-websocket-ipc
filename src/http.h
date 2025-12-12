#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>
#include "config.h"
#include "file_cache.h"

// ============================================================================
// HTTP Request/Response Structures
// ============================================================================
typedef struct {
    char method[16];
    char path[512];
    char version[16];
} http_request_t;

// ============================================================================
// HTTP Functions
// ============================================================================
const char* get_mime_type(const char* path);
void send_http_response(int fd, int status, const char* status_msg,
                       const char* content_type, const char* body, size_t body_len);
int parse_http_request(const char* buffer, http_request_t* req);
void send_file_response(int client_fd, const char* full_path, const char* method, file_cache_t* cache);
void handle_client_connection(int client_fd, const server_config_t* config, file_cache_t* cache);

#endif // HTTP_H
