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

#ifndef NETFR_H
#define NETFR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdalign.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_rma.h>

#include "netfr/netfr_constants.h"

typedef void (*NFRCallback)(const void ** uData);

typedef struct NFRResource     * PNFRResource;
typedef struct NFRClient       * PNFRClient;
typedef struct NFRHost         * PNFRHost;
typedef struct NFRMemory       * PNFRMemory;
typedef struct NFRRemoteMemory * PNFRRemoteMemory;

extern int nfr_LogLevel;

struct NFRRemoteMemory
{
  struct NFRResource * parentResource;
  void               * activeContext;
  uint64_t             addr;
  uint64_t             size;
  uint64_t             rkey;
  uint32_t             align;
  uint8_t              state;
  uint8_t              index;
};

struct NFRCallbackInfo
{
  NFRCallback   callback;
  // This is passed to the callback when it is invoked
  void * uData[NETFR_CALLBACK_USER_DATA_COUNT];
};

enum NFRTransportType
{
  NFR_TRANSPORT_TCP  = 1,  // Libfabric TCP MSG provider
  NFR_TRANSPORT_RDMA = 2,  // Libfabric Verbs MSG provider
  NFR_TRANSPORT_MAX
};

struct NFRInitOpts
{
  uint32_t              apiVersion;
  uint64_t              flags;
  struct sockaddr_in    addrs[NETFR_NUM_CHANNELS];
  uint8_t               transportTypes[NETFR_NUM_CHANNELS];
};

/**
 * @brief Free resources associated with a memory region.
 * 
 * If this memory was allocated using nfrAllocMemory, the memory buffer will
 * also be freed. For externally attached memory regions, only the NetFR
 * specific resources will be freed, not the memory buffer itself.
 * 
 * @param mem Memory region to free
 */
void nfrReleaseMemory(PNFRMemory * mem);

void nfrAckBuffer(PNFRMemory mem);

void nfrFreeMemory(PNFRMemory * mem);

void nfrSetLogLevel(int level);

#ifdef __cplusplus
}
#endif

#endif