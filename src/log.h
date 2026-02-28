#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <stdarg.h>

// Log levels
typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_FATAL
} LogLevel;

// Functions
void log_init(void);
void log_cleanup(void);
void log_set_level(LogLevel level);
void log_debug(const char *format, ...);
void log_info(const char *format, ...);
void log_warn(const char *format, ...);
void log_error(const char *format, ...);
void log_fatal(const char *format, ...);

#endif // LOG_H
