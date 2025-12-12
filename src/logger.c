// Thread-safe logging functions

#include "logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <fcntl.h>
#include <unistd.h>

#define MAX_LOG_SIZE (10 * 1024 * 1024)  // 10MB
#define LOG_SEM_NAME "/server_log_sem"

static FILE* log_file = NULL;
static sem_t* log_sem = NULL;
static char log_path[256] = {0};

// ============================================================================
// Thread-Safe Logger
// ============================================================================

int logger_init(const char* log_file_path) {
    // Create or open the semaphore
    log_sem = sem_open(LOG_SEM_NAME, O_CREAT, 0644, 1);
    if (log_sem == SEM_FAILED) {
        perror("sem_open failed");
        return -1;
    }

    // Store log file path
    strncpy(log_path, log_file_path, sizeof(log_path) - 1);

    // Open log file in append mode
    log_file = fopen(log_path, "a");
    if (!log_file) {
        perror("fopen log file failed");
        sem_close(log_sem);
        sem_unlink(LOG_SEM_NAME);
        return -1;
    }

    return 0;
}

void logger_cleanup(void) {
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
    if (log_sem) {
        sem_close(log_sem);
        sem_unlink(LOG_SEM_NAME);
        log_sem = NULL;
    }
}

static void rotate_log_if_needed(void) {
    struct stat st;
    if (stat(log_path, &st) != 0) {
        return;
    }

    if (st.st_size >= MAX_LOG_SIZE) {
        fclose(log_file);

        // Rename old log file
        char old_log[300];
        time_t now = time(NULL);
        struct tm* tm_info = localtime(&now);
        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", tm_info);
        snprintf(old_log, sizeof(old_log), "%s.%s", log_path, timestamp);
        rename(log_path, old_log);

        // Open new log file
        log_file = fopen(log_path, "a");
        if (!log_file) {
            perror("Failed to reopen log file after rotation");
        }
    }
}

void log_message(const char* format, ...) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%d/%b/%Y:%H:%M:%S %z", tm_info);

    // Always write to stderr
    fprintf(stderr, "[%s] ", timestamp);
    va_list args1;
    va_start(args1, format);
    vfprintf(stderr, format, args1);
    va_end(args1);
    fprintf(stderr, "\n");

    // Write to file if initialized
    if (log_file && log_sem) {
        sem_wait(log_sem);  // Lock

        // Check if rotation is needed
        rotate_log_if_needed();

        if (log_file) {
            fprintf(log_file, "[%s] ", timestamp);
            va_list args2;
            va_start(args2, format);
            vfprintf(log_file, format, args2);
            va_end(args2);
            fprintf(log_file, "\n");
            fflush(log_file);  // Ensure data is written
        }

        sem_post(log_sem);  // Unlock
    }
}
