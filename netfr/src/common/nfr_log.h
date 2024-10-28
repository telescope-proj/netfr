#ifndef NETFR_PRIVATE_LOG_H
#define NETFR_PRIVATE_LOG_H

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>

unsigned long getTimestamp(void);

void nfr_Log(int level, const char * func, const char * file, int line,
            const char * fmt, ...);

#define NFR_LOG_TRACE(fmt, ...) \
  nfr_Log(NFR_LOG_LEVEL_TRACE, __func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define NFR_LOG_DEBUG(fmt, ...) \
  nfr_Log(NFR_LOG_LEVEL_DEBUG, __func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define NFR_LOG_INFO(fmt, ...) \
  nfr_Log(NFR_LOG_LEVEL_INFO, __func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define NFR_LOG_WARNING(fmt, ...) \
  nfr_Log(NFR_LOG_LEVEL_WARNING, __func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define NFR_LOG_ERROR(fmt, ...) \
  nfr_Log(NFR_LOG_LEVEL_ERROR, __func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define NFR_LOG_FATAL(fmt, ...) \
  nfr_Log(NFR_LOG_LEVEL_FATAL, __func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif