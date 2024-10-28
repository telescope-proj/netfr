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

#ifndef NETFR_PRIVATE_RESOURCE_H
#define NETFR_PRIVATE_RESOURCE_H

#include <rdma/fabric.h>
#include <rdma/fi_eq.h>

#include <stdalign.h>
#include <stdint.h>

#include "common/nfr_mem.h"
#include "common/nfr_constants.h"
#include "common/nfr_callback.h"
#include "common/nfr_resource_types.h"

#define NFR_TX_SLOT_BASE(info)    0
#define NFR_RX_SLOT_BASE(info)    ((info).txSlots)
#define NFR_WRITE_SLOT_BASE(info) (NFR_RX_SLOT_BASE(info) + (info).rxSlots)
#define NFR_ACK_SLOT_BASE(info)   (NFR_WRITE_SLOT_BASE(info) + (info).writeSlots)
#define NFR_TOTAL_SLOTS(info)     (NFR_ACK_SLOT_BASE(info) + (info).ackSlots)

#define GET_DATA_SLOT_OFFSET(resource, slot) \
  ((uintptr_t) slot->data - (uintptr_t) resource->commBuf->memRegion->addr)

#define NFR_RESET_CONTEXT(ctx) \
  do { \
    assert(ctx); \
    if ((ctx)->state != CTX_STATE_ACK_ONLY) \
      (ctx)->state = CTX_STATE_AVAILABLE; \
  } while (0)

#define NFR_PRINT_CQ_ERROR(logLevel, ch, err) \
    nfr_PrintCQError(logLevel, __func__, __FILE__, __LINE__, \
                     (int)(ch - ch->parent->channels), res, err) \

static_assert(NFR_INTERNAL_CB_UDATA_COUNT - NETFR_CALLBACK_USER_DATA_COUNT >= 8,
              "At least 8 user data slots must be available for internal use");

int nfr_ResourceCQProcess(struct NFRResource * res,
                          struct NFRCompQueueEntry * cqe);

int nfr_ResourceConsumeRxSlots(struct NFRResource * res,
                               struct NFR_CallbackInfo * cbInfo);

int nfr_ContextGetOldestMessage(struct NFRResource * res,
                                struct NFRFabricContext ** ctx);

int nfr_ResourceOpenSingle(const struct NFRInitOpts * opts, int index,
                           struct NFRResource ** result);
                           
int nfr_ResourceOpen(const struct NFRInitOpts * opts,
                     struct NFRResource ** result);

void nfr_ResourceClose(struct NFRResource * t);

struct NFRFabricContext * nfr_ContextGet(struct NFRResource * res,
                                         uint8_t opType, uint8_t * index);

int nfr_CommBufOpen(struct NFRResource * res, 
                    const struct NFRCommBufInfo * hints);

void nfr_CommBufClose(struct NFRCommBuf * buf);

int nfr_ContextDebugCheck(struct NFRResource * res);

int nfr_GetContextLocation(void * op_context, struct NFRResource * res,
                           uint8_t * typeOut);

int nfr_PrintCQError(int logLevel, const char * func, const char * file, int line, int channel,
                     struct NFRResource * res, struct fi_cq_err_entry * err);

inline static struct NFRCommBufInfo nfr_GetDefaultCommBufInfo(void)
{
  struct NFRCommBufInfo info = {0};
  info.txSlots     = 60;
  info.rxSlots     = 60;
  info.writeSlots  = 6;
  info.ackSlots    = 2;
  info.slotSize    = NETFR_MESSAGE_MAX_SIZE;
  assert(NFR_TOTAL_SLOTS(info) == NETFR_TOTAL_CONTEXT_COUNT);
  return info;
}

#endif