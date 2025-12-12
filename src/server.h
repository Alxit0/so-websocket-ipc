#ifndef SERVER_H
#define SERVER_H

#include "config.h"

// ============================================================================
// Server Functions
// ============================================================================
int create_server_socket(int port);
void worker_process(int server_fd, int worker_id, const server_config_t* config);
int is_priority_endpoint(int client_fd);
void handle_priority_endpoint(int client_fd);

#endif // SERVER_H
