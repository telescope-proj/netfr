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

#include "netfr/netfr.h"
#include "common/nfr.h"
#include "common/nfr_constants.h"
#include "common/nfr_protocol.h"
#include "common/nfr_log.h"

ssize_t nfr_PostTransfer(struct NFRResource * res, struct NFR_TransferInfo * ti)
{
  assert(res);
  assert(ti);

  int ret;
  struct NFRFabricContext * ctx = 0;
  struct NFRFabricContext * wctx = 0;
  struct fid_ep * ep = res->ep;

  NFR_LOG_TRACE("Posting transfer of type %d on resource %p", ti->opType, res);
  
  switch (ti->opType)
  {
    /* For Libfabric MSG endpoints and RDMA RC endpoints, the message order is
       preserved. The message confirming the write is immediately sent after the
       write operation, reducing latency. */
    case NFR_OP_WRITE:
    { 
      uint8_t ctxIdx, wctxIdx;

      ctx = nfr_ContextGet(res, NFR_OP_SEND, &ctxIdx);
      if (!ctx)
      {
        NFR_LOG_TRACE("Send context unavailable for write operation");
        return -EAGAIN;
      }

      wctx = nfr_ContextGet(res, NFR_OP_WRITE, &wctxIdx);
      if (!wctx)
      {
        NFR_LOG_TRACE("Write context unavailable");
        NFR_RESET_CONTEXT(ctx);
        return -EAGAIN;
      }

      NFR_LOG_TRACE("Using contexts %p and %p for write operation", ctx, wctx);

      assert(ti->length);

      struct NFR_TransferWrite * tiw = &ti->writeOpts;
      assert(tiw->localMem);
      assert(tiw->remoteMem);
      assert(tiw->localOffset + ti->length <= tiw->localMem->size);
      assert(tiw->remoteOffset + ti->length <= tiw->remoteMem->size);
      
      struct NFRMsgBufferUpdate * bu = (struct NFRMsgBufferUpdate *) \
        ctx->slot->data;
      nfr_SetHeader(&bu->header, NFR_MSG_BUFFER_UPDATE);
      bu->bufferIndex   = tiw->remoteMem->index;
      bu->payloadSize   = ti->length;
      bu->payloadOffset = tiw->remoteOffset;

      assert(bu->bufferIndex < NETFR_MAX_MEM_REGIONS);

      void * lbuf = (void *) (uint8_t *) tiw->localMem->addr + tiw->localOffset;
      uint64_t rbuf = tiw->remoteMem->addr + tiw->remoteOffset;
      
      ret = fi_write(ep, lbuf, ti->length, fi_mr_desc(tiw->localMem->mr), 0,
                      rbuf, tiw->remoteMem->rkey, wctx);
      if (ret < 0)
      {
        NFR_LOG_DEBUG("Failed to post write: %s (%d)", fi_strerror(-ret), ret);
        NFR_RESET_CONTEXT(ctx);
        NFR_RESET_CONTEXT(wctx);
        return ret;
      }

      nfr_MemCpyOptional(&wctx->cbInfo, tiw->writeCbInfo, sizeof(*tiw->writeCbInfo));
      wctx->state = CTX_STATE_WAITING;

      ret = fi_send(ep, ctx->slot->data, sizeof(*bu),
                    fi_mr_desc(res->commBuf.memRegion->mr), 0, ctx);
      if (ret < 0)
      {
        NFR_LOG_DEBUG("Failed to post send: %s (%d)", fi_strerror(-ret), ret);
        NFR_RESET_CONTEXT(ctx);
        NFR_RESET_CONTEXT(wctx);
        int ret2 = (int) fi_cancel(&ep->fid, wctx);
        if (ret2 < 0)
          return ret2;
        return ret;
      }

      NFR_LOG_TRACE("Write op posted, ctx %p, wctx %p", ctx, wctx);
      tiw->remoteMem->state = NFR_RMEM_BUSY_LOCAL;
      break;
    }
    case NFR_OP_RECV:
    {
      ctx = nfr_ContextGet(res, NFR_OP_RECV, 0);
      if (!ctx)
      {
        // NFR_LOG_TRACE("Receive context unavailable");
        return -EAGAIN;
      }

      ret = fi_recv(ep, ctx->slot->data, NETFR_MESSAGE_MAX_SIZE,
                    fi_mr_desc(res->commBuf.memRegion->mr), 0, ctx);
      if (ret < 0)
      {
        if (ret != -FI_EAGAIN)
          NFR_LOG_DEBUG("Failed to post receive: %s (%d)", fi_strerror(-ret), ret);
        NFR_RESET_CONTEXT(ctx);
        return ret;
      }

      NFR_LOG_TRACE("Receive op posted, ctx %p, wctx %p", ctx, wctx);
      break;
    }
    case NFR_OP_SEND:
    {
      assert(ti->length);
      assert(ti->context);
      assert(ti->length <= NETFR_MESSAGE_MAX_SIZE);
      assert(ti->context->slot);

      ret = fi_send(ep, ti->context->slot->data, ti->length,
                    fi_mr_desc(res->commBuf.memRegion->mr), 0, ti->context);
      if (ret < 0)
      {
        NFR_RESET_CONTEXT(ti->context);
        return ret;
      }
      return 0;
    }
    case NFR_OP_SEND_COPY:
    {
      ctx = nfr_ContextGet(res, NFR_OP_SEND, 0);
      if (!ctx)
      {
        NFR_LOG_TRACE("Send context unavailable for copied send");
        return -EAGAIN;
      }

      assert(ti->data);
      assert(ti->length);
      assert(ti->length <= NETFR_MESSAGE_MAX_SIZE);
      memcpy(ctx->slot->data, ti->data, ti->length);

      ret = fi_send(ep, ctx->slot->data, ti->length,
                    fi_mr_desc(res->commBuf.memRegion->mr), 0, ctx);
      if (ret < 0)
      {
        NFR_LOG_DEBUG("Failed to post send: %s (%d)", fi_strerror(-ret), ret);
        NFR_RESET_CONTEXT(ctx);
        return ret;
      }
    }
    case NFR_OP_INJECT:
    {
      assert(ti->data);
      assert(ti->length);
      assert(ti->length <= NETFR_MESSAGE_MAX_SIZE);
      assert(ti->length <= res->info->tx_attr->inject_size);

      ret = fi_inject(ep, ti->data, ti->length, 0);
      if (ret < 0)
      {
        // Try and convert this to a regular send
        NFR_LOG_DEBUG("Failed to inject: %s (%d), trying send", 
                      fi_strerror(-ret), ret);
        struct NFR_TransferInfo ti2 = {0};
        memcpy(&ti2, ti, sizeof(ti2));
        ti2.opType   = NFR_OP_SEND_COPY;
        ssize_t ret2 = nfr_PostTransfer(res, &ti2);
        if (ret2 < 0)
        {
          NFR_LOG_DEBUG("Failed to convert inject to send: %s (%d)",
                        fi_strerror(-ret2), ret2);
          return ret2;
        }
      }

      break;
    }
    default:
    {
      NFR_LOG_ERROR("Invalid operation type %d", ti->opType);
      assert(!"Invalid operation type");
      return -EINVAL;
    }
  }

  if (ctx)
  {
    nfr_MemCpyOptional(&ctx->cbInfo, ti->cbInfo, sizeof(*ti->cbInfo));
    ctx->state = CTX_STATE_WAITING;
  }
  return 0;
}

/**
 * @brief Free supporting resources associated with an RDMA memory region, and
 *        if the memory region is internal, free the memory buffer.
 *
 * This function performs tasks such as page-unpinning, closing the MR and
 * freeing the NFRMemory structure.
 *
 * @param mem Pointer to the memory region to free
 */
void nfrFreeMemory(PNFRMemory * mem)
{
  assert(mem);
  if (!*mem)
  {
    assert(!"Memory region already closed");
    return;
  }

  if ((*mem)->mr)
    fi_close(&(*mem)->mr->fid);
  if (!(*mem)->extMem)
    nfr_MemFreeAlign((*mem)->addr);
  
  // We only free if it's not part of the internal memory region array,
  // because those are part of the NFRResource struct.
  if ((*mem) < (*mem)->parentResource->memRegions
      || (*mem) > (*mem)->parentResource->memRegions + NETFR_MAX_MEM_REGIONS)
  {
    NFR_LOG_DEBUG("Freeing external memory region %p", *mem);
    free((*mem));
  }
  *mem = 0;
}