#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Global log level
static LogLevel g_log_level = LOG_LEVEL_INFO;
// Log file
static FILE *g_log_file = NULL;

// Log level names
static const char *log_level_names[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "FATAL"
};

// Initialize log system
void log_init(void) {
    // Open log file
    g_log_file = fopen("catclaw.log", "a");
    if (!g_log_file) {
        fprintf(stderr, "Failed to open log file, logging to stderr only\n");
    }
    
    log_info("Log system initialized");
}

// Cleanup log system
void log_cleanup(void) {
    if (g_log_file) {
        log_info("Log system cleaned up");
        fclose(g_log_file);
        g_log_file = NULL;
    }
}

// Set log level
void log_set_level(LogLevel level) {
    g_log_level = level;
    log_info("Log level set to %s", log_level_names[level]);
}

// Internal log function
static void log_internal(LogLevel level, const char *format, va_list args) {
    if (level <= g_log_level) {
        return;
    }
    
    // Get current time
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    
    // Format log message
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);
    
    // Print to console
    fprintf(stderr, "[%s] %s: %s\n", time_str, log_level_names[level], buffer);
    
    // Print to log file
    if (g_log_file) {
        fprintf(g_log_file, "[%s] %s: %s\n", time_str, log_level_names[level], buffer);
        fflush(g_log_file);
    }
}

// Log debug message
void log_debug(const char *format, ...) {
    va_list args;
    va_start(args, format);
    log_internal(LOG_LEVEL_DEBUG, format, args);
    va_end(args);
}

// Log info message
void log_info(const char *format, ...) {
    va_list args;
    va_start(args, format);
    log_internal(LOG_LEVEL_INFO, format, args);
    va_end(args);
}

// Log warning message
void log_warn(const char *format, ...) {
    va_list args;
    va_start(args, format);
    log_internal(LOG_LEVEL_WARN, format, args);
    va_end(args);
}

// Log error message
void log_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    log_internal(LOG_LEVEL_ERROR, format, args);
    va_end(args);
}

// Log fatal message
void log_fatal(const char *format, ...) {
    va_list args;
    va_start(args, format);
    log_internal(LOG_LEVEL_FATAL, format, args);
    va_end(args);
    
    // Exit on fatal error
    exit(1);
}
