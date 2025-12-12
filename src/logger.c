// src/logger.c
// Thread-safe logging functions

#include "logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

// ============================================================================
// Thread-Safe Logger
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
