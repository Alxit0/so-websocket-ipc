// src/main.c
// Prefork HTTP server with shared memory statistics
// Compile: gcc -o server main.c config.c logger.c stats.c http.c thread_pool.c server.c -Wall -Wextra -pthread -lrt
// Run: ./server [config_file]

#include "config.h"
#include "logger.h"
#include "stats.h"
#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

static volatile sig_atomic_t keep_running = 1;

// ============================================================================
// Signal Handler
// ============================================================================
void signal_handler(int signum) {
    (void)signum;
    keep_running = 0;
}

// ============================================================================
// Main Function
// ============================================================================
int main(int argc, char** argv) {
    // Load configuration
    const char* config_file = (argc > 1) ? argv[1] : "server.conf";
    server_config_t config;
    load_config(config_file, &config);

    // Initialize logger
    if (logger_init("server.log") < 0) {
        fprintf(stderr, "Failed to initialize logger\n");
        exit(EXIT_FAILURE);
    }

    // Initialize statistics with shared memory
    if (init_stats() < 0) {
        fprintf(stderr, "Failed to initialize statistics\n");
        logger_cleanup();
        exit(EXIT_FAILURE);
    }

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
        
        // Show global statistics every 30 seconds
        static int counter = 0;
        counter++;
        if (counter >= 30) {
            counter = 0;
            print_global_stats();
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
    
    // Cleanup shared memory and semaphore
    cleanup_stats();
    
    log_message("Shutdown complete");
    
    // Cleanup logger
    logger_cleanup();
    
    return EXIT_SUCCESS;
}
