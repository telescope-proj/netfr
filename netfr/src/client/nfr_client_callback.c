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

#include "client/nfr_client_callback.h"

#include "common/nfr_protocol.h"

void nfr_ClientProcessInternalTx(struct NFRFabricContext * ctx)
{
  ASSERT_CONTEXT_VALID(ctx);
  
  struct NFRHeader * hdr = (struct NFRHeader *) ctx->slot->data;
  NFR_LOG_DEBUG("Processing txctx %p -> type %d", ctx, hdr->type);
  
  assert(ctx->state == CTX_STATE_WAITING || ctx->state == CTX_STATE_ACK_ONLY);
  NFR_RESET_CONTEXT(ctx);
}

void nfr_ClientProcessInternalRx(struct NFRFabricContext * ctx)
{
  NFR_LOG_DEBUG("Processing rxctx %p", ctx);
  ASSERT_CONTEXT_VALID(ctx);
  
  NFR_CAST_UDATA(struct NFRClientChannel *, chan, ctx, 0);
  
  struct NFRClient * client = chan->parent;
  assert(client);
  assert(chan);

  struct NFRHeader * hdr = (struct NFRHeader *) ctx->slot->data;
  if (memcmp(hdr->magic, NETFR_MAGIC, 8) != 0 || hdr->version != NETFR_VERSION)
  {
    assert(!"Invalid message header");
    NFR_RESET_CONTEXT(ctx);
    return;
  }

  switch (hdr->type)
  {
    case NFR_MSG_BUFFER_UPDATE:
    {
      struct NFRMsgBufferUpdate * update = (struct NFRMsgBufferUpdate *) hdr;
      if (update->bufferIndex >= NETFR_MAX_MEM_REGIONS)
      {
        assert(!"Invalid buffer index");
        NFR_RESET_CONTEXT(ctx);
        return;
      }
      struct NFRMemory * mem = chan->res->memRegions + update->bufferIndex;
      if (update->payloadOffset + update->payloadSize > mem->size)
      {
        assert(!"Invalid buffer update");
        NFR_RESET_CONTEXT(ctx);
        return;
      }
      mem->state         = MEM_STATE_HAS_DATA;
      mem->payloadOffset = update->payloadOffset;
      mem->payloadLength = update->payloadSize;
      mem->writeSerial   = update->writeSerial;
      mem->channelSerial = update->channelSerial;
      NFR_RESET_CONTEXT(ctx);
      return;
    }
    case NFR_MSG_HOST_DATA:
    {
      struct NFRMsgHostData * msg = (struct NFRMsgHostData *) hdr;
      if (msg->length > NETFR_MESSAGE_MAX_PAYLOAD_SIZE || msg->length == 0)
      {
        assert(!"Invalid message length");
        NFR_RESET_CONTEXT(ctx);
        return;
      }
      ctx->state               = CTX_STATE_HAS_DATA;
      ctx->slot->msgSerial     = msg->msgSerial;
      ctx->slot->channelSerial = msg->channelSerial;
      return;
    }
    case NFR_MSG_CLIENT_DATA_ACK:
    {
      NFR_RESET_CONTEXT(ctx);
      ++chan->res->txCredits;
      return;
    }
    default:
    {
      assert(!"Invalid message type");
      return;
    }
  }
}