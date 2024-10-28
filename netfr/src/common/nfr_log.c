#include "common/nfr_log.h"
#include "netfr/netfr.h"

int nfr_LogLevel = NFR_LOG_LEVEL_OFF;

unsigned long getTimestamp()
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void nfr_Log(int level, const char * func, const char * file, int line,
            const char * fmt, ...)
{
  if (level < nfr_LogLevel)
    return;
  const char * levelStr;
  switch (level)
  {
    case NFR_LOG_LEVEL_TRACE: levelStr   = "T"; break;
    case NFR_LOG_LEVEL_DEBUG: levelStr   = "D"; break;
    case NFR_LOG_LEVEL_INFO: levelStr    = "I"; break;
    case NFR_LOG_LEVEL_WARNING: levelStr = "!W"; break;
    case NFR_LOG_LEVEL_ERROR: levelStr   = "!E"; break;
    case NFR_LOG_LEVEL_FATAL: levelStr   = "!F"; break;
    default: levelStr = "Unknown"; break;
  }
  const char * filename = strrchr(file, '/');
  if (!filename)
    filename = file;
  else
    ++filename;

  fprintf(stderr, "%s; %lu; %s:%d ; %s ; ", levelStr, getTimestamp(),
          filename, line, func);
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
}

void nfrSetLogLevel(int level)
{
  nfr_LogLevel = level;
}