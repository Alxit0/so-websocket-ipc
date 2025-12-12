#ifndef LOGGER_H
#define LOGGER_H

// ============================================================================
// Thread-Safe Logger
// ============================================================================

// Initialize the logger (must be called before using log_message)
int logger_init(const char* log_file_path);

// Cleanup the logger (should be called on shutdown)
void logger_cleanup(void);

// Thread-safe logging function
void log_message(const char* format, ...);

#endif // LOGGER_H
