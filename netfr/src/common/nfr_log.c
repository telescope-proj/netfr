/*
 * Telescope Network Frame Relay System
 *
 * Copyright (c) 2023-2024 Tim Dettmar
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "common/nfr_log.h"
#include "netfr/netfr.h"

int nfr_LogLevel = NFR_LOG_LEVEL_OFF;

unsigned long getTimestamp(void)
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