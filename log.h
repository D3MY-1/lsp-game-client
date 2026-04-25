#ifndef LOG_H
#define LOG_H

#include <stdarg.h>
#include <stdio.h>

typedef enum { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR } LogLevel;

// Returns 0 on success, -1 on failure
int log_init(void);

// Close the log file
void log_close(void);

// Use macros instead of this
void log_write(LogLevel level, const char *file, int line, const char *fmt,
               ...);

#define LOG_DEBUG(...) log_write(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...) log_write(LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...) log_write(LOG_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) log_write(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#endif // LOG_H
