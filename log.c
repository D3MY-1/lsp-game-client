#include "log.h"
#include <time.h>

#include <errno.h>
#include <sys/stat.h>

static FILE *log_file = NULL;

static const char *level_strings[] = {"DEBUG", "INFO", "WARN", "ERROR"};

int log_init(void) {
  if (log_file != NULL)
    return 0;

  // Create logs directory if it doesn't exist
  if (mkdir("logs", 0777) == -1) {
    if (errno != EEXIST) {
      return -1;
    }
  }

  time_t t = time(NULL);
  struct tm *tm_info = localtime(&t);
  char filepath[128];
  strftime(filepath, sizeof(filepath), "logs/client_%Y%m%d_%H%M%S.log",
           tm_info);

  log_file = fopen(filepath, "w");
  if (log_file == NULL) {
    return -1;
  }
  return 0;
}

void log_close(void) {
  if (log_file) {
    fclose(log_file);
    log_file = NULL;
  }
}

void log_write(LogLevel level, const char *file, int line, const char *fmt,
               ...) {
  if (!log_file)
    return;

  time_t t = time(NULL);
  struct tm *tm_info = localtime(&t);
  char time_buf[26];
  strftime(time_buf, 26, "%Y-%m-%d %H:%M:%S", tm_info);

  fprintf(log_file, "[%s] [%s] %s:%d: ", time_buf, level_strings[level], file,
          line);

  va_list args;
  va_start(args, fmt);
  vfprintf(log_file, fmt, args);
  va_end(args);

  fprintf(log_file, "\n");

  // MUST FLUSH TO SEE LOGS WHEN CRASHING
  fflush(log_file);
}
