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

#ifndef NFR_PRIVATE_RESOURCE_TYPES_H
#define NFR_PRIVATE_RESOURCE_TYPES_H

#include <stdint.h>
#include <rdma/fabric.h>
#include <rdma/fi_eq.h>

#include "common/nfr_constants.h"

struct NFRFabricContext;

/**
 * @brief NetFR internal callback handle.
 *
 * @param ctx The context associated with the callback
 *         
 */
typedef void (*NFR_Callback)(struct NFRFabricContext * ctx);

/* INTERNAL callback structure */
struct NFR_CallbackInfo
{
  // The callback to invoke when the operation completes
  NFR_Callback callback;
  /* The user data to be made available to the callback. The elements can refer
     to arbitrary user data, and are not interpreted in any way by the internal
     queue manager. You can allocate NFR_CallbackInfo on the stack; the values
     of this array are copied into the context when the operation is posted.

     When the callback is invoked, the context, which contains the uData array,
     is passed as the first argument. */
  void * uData[NFR_INTERNAL_CB_UDATA_COUNT];
};

struct NFRResource;

struct NFRFabricContext
{
  struct NFRResource     * parentResource;
  uint8_t                  state;
  struct NFR_CallbackInfo  cbInfo;
  struct NFRDataSlot     * slot;
};

struct NFRCompQueueEntry
{
  union
  {
    struct fi_cq_data_entry data;
    struct fi_cq_err_entry  err;
  } entry;
  uint8_t isError;
};

struct NFRDataSlot
{
  uint32_t         msgSerial;
  uint32_t         channelSerial;
  alignas(16) char data[0];
};

struct NFRExtCMEntry {
	fid_t			       fid;
	struct fi_info * info;
	uint8_t			     data[NETFR_CM_MESSAGE_MAX_SIZE];
};

enum NFRMemoryType
{
  NFR_MEM_TYPE_INTERNAL,
  
  NFR_MEM_INDEX_SYSTEM_TYPES,
  NFR_MEM_TYPE_SYSTEM_MANAGED,
  NFR_MEM_TYPE_SYSTEM_MANAGED_DMABUF,

  NFR_MEM_INDEX_EXTERNAL_TYPES,
  NFR_MEM_TYPE_USER_MANAGED,
  NFR_MEM_TYPE_USER_MANAGED_DMABUF
};

static inline int nfr_MemIsExternal(enum NFRMemoryType type)
{
  return type > NFR_MEM_INDEX_EXTERNAL_TYPES;
}

struct NFRMemory
{
  struct NFRResource * parentResource;
  void               * addr;
  struct fid_mr      * mr;
  uint64_t             udata;
  uint64_t             size;
  uint32_t             writeSerial;    // Message id relative to other writes
  uint32_t             channelSerial;  // Message id relative to all messages
  uint32_t             payloadOffset;
  uint32_t             payloadLength;
  uint8_t              index;
  uint8_t              memType;       // Memory allocation type
  uint8_t              state;
  uint8_t              refCount;
  int                  dmaFd;          // DMABUF fd if enabled
};

struct NFRCommBufInfo
{
  uint32_t txSlots;
  uint32_t rxSlots;
  uint32_t writeSlots;
  uint32_t ackSlots;
  uint32_t slotSize; // Size of a single data slot
};

struct NFRCommBuf
{
  struct NFRMemory        * memRegion;
  struct NFRFabricContext * ctx;
  struct NFRCommBufInfo     info;
};

struct NFRResource
{
  void                    * parentTopLevel; // NFRHost * or NFRClient *
  struct fi_info          * info;
  struct fid_fabric       * fabric;
  struct fid_domain       * domain;
  struct fid_cq           * cq;
  struct fid_pep          * pep; 
  struct fid_eq           * eq;
  struct fid_ep           * ep;
  struct NFRCommBuf         commBuf;
  struct NFRMemory          memRegions[NETFR_MAX_MEM_REGIONS];
  uint64_t                  rkeyCounter;
  uint64_t                  lastPing;
  uint32_t                  txCredits;
  uint8_t                   connState;
};

#define ASSERT_COMM_BUF_READY(cb) \
  assert(cb.memRegion); \
  assert(cb.ctx); \
  assert(cb.info.txSlots); \
  assert(cb.info.rxSlots); \
  assert(cb.info.writeSlots); \
  assert(cb.info.ackSlots); \
  assert(cb.info.slotSize); 

#define ASSERT_CONTEXT_VALID(fctx) \
  assert(fctx); \
  assert(fctx->parentResource); \
  ASSERT_COMM_BUF_READY(fctx->parentResource->commBuf)

#endif