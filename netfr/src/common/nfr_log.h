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