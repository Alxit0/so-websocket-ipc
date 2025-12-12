#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// Configuration Structure
// ============================================================================
typedef struct {
    int port;
    char document_root[256];
    int num_workers;
    int timeout_seconds;
    int cache_size_mb;
} server_config_t;

// ============================================================================
// Configuration Functions
// ============================================================================
int load_config(const char* filename, server_config_t* config);

#endif // CONFIG_H
