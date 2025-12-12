#ifndef SERVER_H
#define SERVER_H

#include "config.h"

// ============================================================================
// Server Functions
// ============================================================================
int create_server_socket(int port);
void worker_process(int server_fd, int worker_id, const server_config_t* config);

#endif // SERVER_H
