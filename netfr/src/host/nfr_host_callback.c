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

/* Internal callbacks */

#include "host/nfr_host_callback.h"
#include "host/nfr_host.h"

#include "common/nfr_constants.h"
#include "common/nfr_protocol.h"
#include "common/nfr_resource.h"
#include "common/nfr.h"

// Process internal transmit completion
// udata: {NFRFabricContext}
void nfr_HostProcessInternalTx(struct NFRFabricContext * ctx)
{
  NFR_LOG_DEBUG("Processing txctx %p", ctx);
  ASSERT_CONTEXT_VALID(ctx);

  /* The context should be in either one of these states, otherwise this
     function should not have been called. */
  if (ctx->state != CTX_STATE_WAITING && ctx->state != CTX_STATE_ACK_ONLY)
  {
    assert(!"Invalid buffer state");
    goto release_mbuf;
  }

  // Always release the buffer
release_mbuf:
  NFR_RESET_CONTEXT(ctx);
}

// Process internal receive completion
// udata: {NFRHost, NFRHostChannel}
void nfr_HostProcessInternalRx(struct NFRFabricContext * ctx)
{
  NFR_LOG_DEBUG("Processing rxctx %p", ctx);
  ASSERT_CONTEXT_VALID(ctx);

  NFR_CAST_UDATA(struct NFRHostChannel *, chan, ctx, 0);
  struct NFRHost * host = chan->parent;
  
  assert(host);
  assert(chan);

  if (ctx->state != CTX_STATE_WAITING)
  {
    assert(!"Invalid buffer state");
    return;
  }
  
  struct NFRHeader * hdr = (struct NFRHeader *) ctx->slot->data;
  if (memcmp(hdr->magic, NETFR_MAGIC, 8) != 0 || hdr->version != NETFR_VERSION)
  {
    assert(!"Invalid message header");
    goto release_mbuf;
  }

  switch (hdr->type)
  {
    case NFR_MSG_BUFFER_STATE:
    {
      struct NFRMsgBufferState * state = (struct NFRMsgBufferState *) hdr;
      if (state->index >= NETFR_MAX_MEM_REGIONS)
      {
        assert(!"Invalid memory region index");
        goto release_mbuf;
      }

      struct NFRRemoteMemory * rmem = chan->clientRegions + state->index;
      if (rmem->state == NFR_RMEM_BUSY_LOCAL)
      {
        assert(!"Client caused invalid state transition");
        // tbd: cancel the operation if it's outstanding by using ownerContext
        goto release_mbuf;
      }
      if (!state->size)
      {
        // Releases the memory region
        memset(rmem, 0, sizeof(*rmem));
        rmem->state = NFR_RMEM_NONE;
        goto release_mbuf;
      }
      NFR_LOG_DEBUG("Got buf index %d / %p len %lu key %d st %d -> %d", state->index,
                    (uintptr_t) state->addr, state->size, state->rkey,
                    rmem->state, NFR_RMEM_AVAILABLE);
      rmem->addr         = state->addr;
      rmem->size         = state->size;
      rmem->rkey         = state->rkey;
      rmem->align        = state->pageSize;
      rmem->state        = NFR_RMEM_AVAILABLE;
      rmem->activeContext = 0;
      break;
    }
    case NFR_MSG_CLIENT_DATA:
    {
      // The host can call nfrHostReadData to read the message later. This must
      // be done regularly, since there is no alert mechanism yet.
      struct NFRMsgClientData * msg = (struct NFRMsgClientData *) hdr;
      if (msg->length > NETFR_MESSAGE_MAX_PAYLOAD_SIZE)
      {
        assert(!"Message size is invalid");
        goto release_mbuf;
      }
      ctx->state = CTX_STATE_HAS_DATA;
      ctx->slot->serial = chan->rxSerial++;
      return;
    }
    case NFR_MSG_HOST_DATA_ACK:
      ++chan->res->txCredits;
      break;
    case NFR_MSG_CLIENT_HELLO:
      assert(!"Already connected client should not send hello message");
      break;
    case NFR_MSG_HOST_DATA:
    case NFR_MSG_BUFFER_HINT:
      assert(!"Client sent server-side message");
      break;
    default:
      assert(!"Invalid message type");
      break;
  }

release_mbuf:
  NFR_RESET_CONTEXT(ctx);
}

// Process internal write completion 
// udata: (NFRHostChannel * ch, NFRMemory * localMem, NFRRemoteMemory * remoteMem, 
//         uint64_t localOffset, uint64_t remoteOffset, uint64_t length)
void nfr_HostProcessInternalWrite(struct NFRFabricContext * ctx)
{
  NFR_LOG_TRACE("Processing wrctx %p", ctx);
  assert(ctx);

  NFR_CAST_UDATA(struct NFRHostChannel *, ch, ctx, 0);
  NFR_CAST_UDATA(struct NFRMemory *, lmem, ctx, 1);
  NFR_CAST_UDATA(struct NFRRemoteMemory *, rmem, ctx, 2);
  NFR_CAST_UDATA_NUM(uint64_t, lOffset, ctx, 3);
  NFR_CAST_UDATA_NUM(uint64_t, rOffset, ctx, 4);
  NFR_CAST_UDATA_NUM(uint64_t, length, ctx, 5);
  NFR_CAST_UDATA(NFRCallback, userCb, ctx, 6);

  assert(ch);
  assert(lmem);
  assert(rmem);
  assert(length);
  assert(length <= lmem->size - lOffset);
  assert(length <= rmem->size - rOffset);
  assert(length <= NETFR_MAX_BUFFER_SIZE);

  if (ctx->state != CTX_STATE_WAITING)
  {
    assert(!"Invalid buffer state");
    return;
  }

  if (rmem->state != NFR_RMEM_BUSY_LOCAL)
  {
    assert(!"Invalid remote memory state");
    return;
  }
  rmem->state = NFR_RMEM_BUSY_REMOTE;

  // Invoke the user callback
  // tbd: check if the access is valid
  if (userCb)
  {
    const void ** uudata = (const void **)(ctx->cbInfo.uData + NFR_USER_CB_INDEX);
    userCb(uudata);
  }

  NFR_RESET_CONTEXT(ctx);
}