// src/config.c
// Configuration loading from file

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ============================================================================
// Configuration Loader
// ============================================================================
int load_config(const char* filename, server_config_t* config) {
    // Set defaults
    config->port = 8080;
    strncpy(config->document_root, "/var/www/html", sizeof(config->document_root));
    config->num_workers = 4;
    config->timeout_seconds = 30;
    config->cache_size_mb = 10;
    config->threads_per_worker = 10;

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
            else if (strcmp(k, "CACHE_SIZE_MB") == 0) config->cache_size_mb = atoi(v);
            else if (strcmp(k, "THREADS_PER_WORKER") == 0) config->threads_per_worker = atoi(v);
            else if (strcmp(k, "DOCUMENT_ROOT") == 0) 
                strncpy(config->document_root, v, sizeof(config->document_root) - 1);
        }
    }
    fclose(fp);
    return 0;
}
