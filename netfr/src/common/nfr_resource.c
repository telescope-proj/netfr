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

#include "common/nfr_protocol.h"
#include "common/nfr_resource.h"
#include "common/nfr.h"
#include "common/nfr_log.h"

inline static int nfr_GetSlotBase(struct NFRCommBufInfo info, uint8_t type,
                                  int * slotCount)
{
  switch (type)
  {
    case NFR_OP_SEND:
      if (slotCount)
        *slotCount = info.txSlots;
      return NFR_TX_SLOT_BASE(info);
    case NFR_OP_RECV:
      if (slotCount)
        *slotCount = info.rxSlots;
      return NFR_RX_SLOT_BASE(info);
    case NFR_OP_WRITE:
      if (slotCount)
        *slotCount = info.writeSlots;
      return NFR_WRITE_SLOT_BASE(info);
    case NFR_OP_ACK:
      if (slotCount)
        *slotCount = info.ackSlots;
      return NFR_ACK_SLOT_BASE(info);
    default:
      assert(!"Invalid operation type");
      return -1;
  }
}

struct NFRFabricContext * nfr_ContextGet(struct NFRResource * res,
                                         uint8_t opType, uint8_t * index)
{
  assert(res);
  int endIndex;
  int i = nfr_GetSlotBase(res->commBuf.info, opType, &endIndex);
  endIndex += i;
  assert(i >= 0);
  assert(endIndex <= NFR_TOTAL_SLOTS(res->commBuf.info));
  
  for (; i < endIndex; ++i)
  {
    if (res->commBuf.ctx[i].state == CTX_STATE_AVAILABLE)
    {
      NFR_LOG_TRACE("Allocating context %d for operation %d", i, opType);
      res->commBuf.ctx[i].state = CTX_STATE_ALLOCATED;
      if (index)
        *index = i;
      return res->commBuf.ctx + i;
    }
  }
  return 0;
}

int nfr_GetContextLocation(void * op_context, struct NFRResource * res,
                           uint8_t * typeOut)
{
  assert(op_context);
  assert(res);

  struct NFRFabricContext * ctx = op_context;

  if (ctx < res->commBuf.ctx || 
      ctx > res->commBuf.ctx + NFR_TOTAL_SLOTS(res->commBuf.info))
    return -EINVAL;

  int ctxIdx = (int) (ctx - res->commBuf.ctx);
  
  if (typeOut)
  {
    if (ctxIdx > NFR_ACK_SLOT_BASE(res->commBuf.info))
      *typeOut = NFR_OP_ACK;
    else if (ctxIdx > NFR_WRITE_SLOT_BASE(res->commBuf.info))
      *typeOut = NFR_OP_WRITE;
    else if (ctxIdx > NFR_RX_SLOT_BASE(res->commBuf.info))
      *typeOut = NFR_OP_RECV;
    else
      *typeOut = NFR_OP_SEND;
  }

  return ctxIdx;
}

int nfr_PrintCQError(int logLevel, const char * func, const char * file, int line, int channel,
                     struct NFRResource * res, struct fi_cq_err_entry * err)
{
  assert(res);
  assert(err);

  if (!res || !err)
    return -EINVAL;

  if (nfr_LogLevel > logLevel)
    return 0;

  char errStr[128] = {0};
  uint8_t ctxType = 0;
  int ctxPos = nfr_GetContextLocation(err->op_context, res, &ctxType);
  if (ctxPos < 0)
  {
    NFR_LOG_ERROR("Failed to get context location: %d", ctxPos);
    return ctxPos;
  }

  const char * slotType;
  switch (ctxType)
  {
    case NFR_OP_SEND:
      slotType = "send";
      break;
    case NFR_OP_RECV:
      slotType = "recv";
      break;
    case NFR_OP_WRITE:
      slotType = "write";
      break;
    case NFR_OP_ACK:
      slotType = "ack";
      break;
    default:
      slotType = "unknown";
      break;
  }

  nfr_Log(logLevel, func, file, line, "CQ Err ch[%d]->ctx[%d] (%s) / %s (%d) "
                                "/ ProvErr: %s (%d)",
           channel, ctxPos, slotType,
           fi_strerror(-err->err),
           err->err,
           fi_cq_strerror(res->cq, err->prov_errno, 
                          err->err_data,
                          errStr, sizeof(errStr)),
           err->prov_errno);
  return err->err;
}

/**
 * @brief Flush the completion queue for a fabric resource.
 *
 * This function will continually read completions from the CQ until there are
 * no more to process. The callbacks associated with the operation will also be
 * called from within this function when they complete.
 *
 * @param res   Fabric resource
 *
 * @param cqe   Completion queue entry. If an error occurs, the error will be
 *              stored in this structure, ``cqe->isError`` will be set to 1, and
 *              the function will return ``-FI_EAVAIL``.
 *
 * @return      0 upon success, ``-FI_EAVAIL`` for fabric errors, or a negative
 *              value for other errors. 
 *
 *              When ``-FI_EAVAIL`` is returned, the error will have already
 *              been read and stored in ``cqe->entry.err``.
 */
int nfr_ResourceCQProcess(struct NFRResource * res,
                          struct NFRCompQueueEntry * cqe)
{
  assert(res);
  assert(cqe);

  int ret = 0;
  int nComp = 0;
  int totalComp = 0;

  // Process all completed operations
  struct NFRFabricContext * ctx;
  do
  {
    cqe->entry.data.op_context = 0;
    nComp = (int) fi_cq_read(res->cq, &cqe->entry.data, 1);
    if (nComp == 0 || nComp == -FI_EAGAIN)
    {
      return 0;
    }
    if (nComp < 0 && nComp != -FI_EAGAIN)
    {
      if (nComp == -FI_EAVAIL)
      {
        ret = (int) fi_cq_readerr(res->cq, &cqe->entry.err, 0);
        if (ret < 0)
          return ret;
        
        // For canceled ops, we still want to call the callback; callbacks need
        // to use ctx->state to determine whether the operation was canceled
        // and return whether the error was handleable.
        if (cqe->entry.err.err == FI_ECANCELED)
        {
          ctx = cqe->entry.err.op_context;
          ASSERT_CONTEXT_VALID(ctx);
          if (ctx)
            ctx->state = CTX_STATE_CANCELED;
        }
        else
        {
          cqe->isError = 1;
          return -FI_EAVAIL;
        }
      }
    }
    if (nComp > 0)
    {
      ctx = cqe->entry.data.op_context;
      ASSERT_CONTEXT_VALID(ctx);
      assert(ctx->state > CTX_STATE_AVAILABLE);
    }

    // This goes to a specific handler for each operation type
    if (ctx->cbInfo.callback)
    {
      NFR_LOG_TRACE("Invoking callback for context %p", ctx);
      ctx->cbInfo.callback(ctx);
      memset(&ctx->cbInfo, 0, sizeof(ctx->cbInfo));
    }
    else
    {
      NFR_LOG_TRACE("No callback for context %p", ctx);
    }

    /* If the callback isn't waiting for something else to read out the data,
       we can safely release the context. */
    if (ctx->state != CTX_STATE_HAS_DATA)
      NFR_RESET_CONTEXT(ctx);

    ++totalComp;
  } 
  while (nComp > 0);

  return totalComp;
}

/**
 * @brief Post receive operations for all available receive buffers.
 * 
 * This function must be called often to ensure that there are always buffers
 * available for incoming messages.
 * 
 * @param res     Fabric resource
 * 
 * @param cbInfo  Callback to invoke when a receive operation completes
 * 
 * @return        0 upon success, or a negative value for errors
 */
int nfr_ResourceConsumeRxSlots(struct NFRResource * res,
                               struct NFR_CallbackInfo * cbInfo)
{
  ASSERT_COMM_BUF_READY(res->commBuf);
  int totalRx = 0;
  do
  {
    struct NFR_TransferInfo ti = {0};
    ti.opType = NFR_OP_RECV;
    ti.cbInfo = cbInfo;

    ssize_t ret = nfr_PostTransfer(res, &ti);
    if (ret < 0)
    {
      if (ret == -EAGAIN)
        return totalRx;
      return ret;
    }
  }
  while (1);
}

/**
 * @brief Get the oldest message in the queue waiting to be read. This does not
 *        remove the message from the queue; do so after copying the data out.
 * 
 * @param res   Fabric resource
 * 
 * @param ctx   Output pointer to the context of the oldest message
 * 
 * @return      The number of messages available (0/1)
 */
int nfr_ContextGetOldestMessage(struct NFRResource * res,
                                struct NFRFabricContext ** ctx)
{
  ASSERT_COMM_BUF_READY(res->commBuf);
  int base = NFR_RX_SLOT_BASE(res->commBuf.info);

  int haveData = 0;
  uint32_t limSerial = 0;
  uint32_t sub = 0;

  for (int i = base; i < base + res->commBuf.info.rxSlots; ++i)
  {
    struct NFRFabricContext * c = res->commBuf.ctx + i;
    if (c->state == CTX_STATE_HAS_DATA && c->slot->channelSerial > limSerial)
      limSerial = c->slot->channelSerial;
  }

  if (limSerial > ((uint32_t) -1) - 2048)
    sub = 4096;
  
  limSerial = (uint32_t) -1;

  for (int i = base; i < base + res->commBuf.info.rxSlots; ++i)
  {
    struct NFRFabricContext * c = res->commBuf.ctx + i;
    if (c->state == CTX_STATE_HAS_DATA)
    {
      if (!haveData || c->slot->channelSerial - sub < limSerial - sub)
      {
        limSerial = c->slot->channelSerial;
        *ctx = res->commBuf.ctx + i;
        haveData = 1;
      }
    }
  }

  return haveData;
}

int nfr_ContextDebugCheck(struct NFRResource * res)
{
  ASSERT_COMM_BUF_READY(res->commBuf);
  int base = NFR_RX_SLOT_BASE(res->commBuf.info);
  int count = 0;
  for (int i = base; i < base + res->commBuf.info.rxSlots; ++i)
  {
    if (res->commBuf.ctx[i].state == CTX_STATE_ALLOCATED)
    {
      assert(!"Context in unexpected state");
      abort();
    }
  }
  return count;
}

/**
 * @brief Open a single fabric resource at a specific index.
 * 
 * The allowed indexes are within [0, NETFR_NUM_CHANNELS).
 * 
 * @param opts     Initialization/addressing options
 * 
 * @param index    Index of the resource to open
 * 
 * @param result   Resulting fabric resource
 * 
 * @return int 
 */
int nfr_ResourceOpenSingle(const struct NFRInitOpts * opts,
                           int index, struct NFRResource ** result)
{
  struct NFRResource * res = calloc(1, sizeof(*res));
  if (!res)
  {
    NFR_LOG_DEBUG("Failed to allocate memory for resource");
    return -ENOMEM;
  }
  
  int ret = 0;
  struct fi_info * info = 0, * hints = fi_allocinfo();
  if (!hints)
  {
    NFR_LOG_DEBUG("Failed to allocate memory for hints");
    ret = -ENOMEM;
    goto free_struct;
  }

  switch (opts->transportTypes[index])
  {
    case NFR_TRANSPORT_TCP:
      hints->fabric_attr->prov_name = strdup("tcp");
      break;
    case NFR_TRANSPORT_RDMA:
      hints->fabric_attr->prov_name = strdup("verbs");
      break;
    default:
      assert(!"Invalid transport type");
      ret = -EINVAL;
      goto free_info;
  }

  NFR_LOG_DEBUG("Selecting transport %s", hints->fabric_attr->prov_name);

  hints->ep_attr->type          = FI_EP_MSG;
  // "equivalent to FI_MR_BASIC" except that it doesn't work
  // hints->domain_attr->mr_mode   = FI_MR_VIRT_ADDR | FI_MR_ALLOCATED 
  //                                 | FI_MR_PROV_KEY | FI_MR_LOCAL;
  hints->domain_attr->mr_mode   = FI_MR_BASIC;
  hints->mode                   = FI_RX_CQ_DATA | FI_LOCAL_MR;
  hints->caps                   = FI_MSG | FI_RMA;
  hints->tx_attr->msg_order     = FI_ORDER_SAS | FI_ORDER_SAW;
  hints->tx_attr->comp_order    = FI_ORDER_STRICT;
  hints->rx_attr->msg_order     = FI_ORDER_SAS | FI_ORDER_SAW;
  hints->rx_attr->comp_order    = FI_ORDER_STRICT;
  hints->ep_attr->protocol      = FI_PROTO_RDMA_CM_IB_RC;
  // struct sockaddr_in addr       = opts->addrs[index];
  hints->addr_format            = FI_SOCKADDR_IN;
  // hints->src_addr               = (void *) &addr;
  // hints->src_addrlen            = sizeof(addr);
  // hints->dest_addr              = (void *) &addr;
  // hints->dest_addrlen           = sizeof(addr);

  // Placing the destination address in the hints structure doesn't work
  // reliably and I don't think it's supposed to.
  char service[8];
  const char * node = inet_ntoa(opts->addrs[index].sin_addr);
  snprintf(service, 6, "%d", ntohs(opts->addrs[index].sin_port));

  uint64_t flags = opts->flags;
  if (!flags)
  {
    flags = FI_SOURCE | FI_NUMERICHOST;
  }

  // We first try enabling the FI_HMEM feature, which provides us with the most
  // flexible DMABUF options. If this fails, we can still use DMABUFs, just
  // self-allocated ones only and not those allocated by, e.g., the GPU. This
  // feature requires a relatively new version of libfabric (1.20+).
  if (fi_version() >= FI_VERSION(1, 20) 
      && opts->apiVersion >= FI_VERSION(1, 20))
  {
    flags |= FI_HMEM;
  }

  NFR_LOG_DEBUG("Finding fabric for address %s:%s", node, service);
  
  for (int i = 0; i < 2; ++i)
  {
    ret = fi_getinfo(opts->apiVersion, node, service, 
                     flags, hints, &info);
    if (ret < 0)
    {
      if (flags & FI_HMEM)
      {
        NFR_LOG_DEBUG("DMABUF-enabled fabric not found, retrying without");
        flags &= ~FI_HMEM;
        continue;
      }

      NFR_LOG_DEBUG("Unable to find suitable fabric: %s (%d)",
                    fi_strerror(-ret), ret);
      hints->src_addr = 0;
      hints->src_addrlen = 0;
      hints->dest_addr = 0;
      hints->dest_addrlen = 0;
      fi_freeinfo(hints);
      goto free_struct;
    }
    break;
  }
  
  assert(info);

  // Try all of the available fabrics
  int flag = 0;
  for (struct fi_info * tmp = info; tmp; tmp = tmp->next)
  {
    ret = fi_fabric(tmp->fabric_attr, &res->fabric, &res);
    if (ret < 0)
      continue;
    
    ret = fi_domain(res->fabric, info, &res->domain, &res);
    if (ret < 0)
    {
      fi_close(&res->fabric->fid);
      continue;
    }

    res->info = fi_dupinfo(tmp);
    if (!res->info)
    {
      ret = -ENOMEM;
      fi_freeinfo(info);
      goto free_fabric_domain;
    }
    flag = 1;
    break;
  }

  fi_freeinfo(info);
  if (!flag)
  {
    ret = -ENOENT;
    goto free_fabric_domain;
  }

  NFR_LOG_DEBUG("Using provider %s (%s)", res->info->fabric_attr->prov_name,
                res->info->fabric_attr->name);

  struct fi_eq_attr eqAttr;
  memset(&eqAttr, 0, sizeof(eqAttr));
  eqAttr.wait_obj = FI_WAIT_UNSPEC;
  ret = fi_eq_open(res->fabric, &eqAttr, &res->eq, &res);
  if (ret < 0)
    goto free_res_info;

  struct fi_cq_attr cqAttr;
  memset(&cqAttr, 0, sizeof(cqAttr));
  cqAttr.format = FI_CQ_FORMAT_DATA;
  cqAttr.size   = NETFR_TOTAL_CONTEXT_COUNT;
  ret = fi_cq_open(res->domain, &cqAttr, &res->cq, &res);
  if (ret < 0)
    goto free_eq;

  for (int i = 0; i < NETFR_MAX_MEM_REGIONS; ++i)
  {
    res->memRegions[i].state = MEM_STATE_EMPTY;
  }

  *result = res;
  return 0;
  
// free_cq:
  fi_close(&res->cq->fid);
free_eq:
  fi_close(&res->eq->fid);
free_res_info:
  fi_freeinfo(res->info);
free_fabric_domain:
  fi_close(&res->domain->fid);
  fi_close(&res->fabric->fid);
free_info:
  fi_freeinfo(info);
free_struct:
  free(res);
  return ret;
}

/* Common init function. Sets up the local resources but no active endpoints
   here. */
int nfr_ResourceOpen(const struct NFRInitOpts * opts,
                     struct NFRResource ** result)
{
  nfr_SetEnv("FI_UNIVERSE_SIZE", "2", 0);
  
  NFR_LOG_DEBUG("Opening resources");
  for (int i = 0; i < NETFR_NUM_CHANNELS; ++i)
  {
    int ret = nfr_ResourceOpenSingle(opts, i, result + i);
    if (ret < 0)
    {
      NFR_LOG_DEBUG("Failed to open resource %d: %s (%d)", i, fi_strerror(-ret),
                    ret);
      for (int j = 0; j < i; ++j)
        nfr_ResourceClose(result[j]);
      return ret;
    }
  }

  return 0;
}

void nfr_ResourceClose(struct NFRResource * t)
{
  if (!t)
    return;
  nfr_CommBufClose(&t->commBuf);
  if (t->info)
    fi_freeinfo(t->info);
  if (t->ep)
    fi_close(&t->ep->fid);
  if (t->pep)
    fi_close(&t->pep->fid);
  if (t->cq)
    fi_close(&t->cq->fid);
  if (t->eq)
    fi_close(&t->eq->fid);
  if (t->domain)
    fi_close(&t->domain->fid);
  if (t->fabric)
    fi_close(&t->fabric->fid);
  free(t);
}

int nfr_CommBufOpen(struct NFRResource * res, 
                    const struct NFRCommBufInfo * hints)
{
  assert(res);
  assert(hints->txSlots);
  assert(hints->rxSlots);
  assert(hints->writeSlots);
  assert(hints->ackSlots);
  assert(hints->slotSize);

  if (res->commBuf.ctx)
  {
    NFR_LOG_DEBUG("Recreating communication buffer");
    nfr_CommBufClose(&res->commBuf);
  }
  else
  {
    NFR_LOG_DEBUG("Creating communication buffer");
  }

  memcpy(&res->commBuf.info, hints, sizeof(*hints));
  uint32_t msgSlotCount = hints->txSlots + hints->rxSlots + hints->writeSlots
                        + hints->ackSlots;
  uint64_t totalSize = NETFR_MESSAGE_MAX_SIZE
                     * (hints->txSlots + hints->rxSlots + hints->ackSlots 
                        + hints->writeSlots);
  
  res->commBuf.memRegion = nfr_RdmaAlloc(res, totalSize, FI_READ | FI_WRITE,
                                         MEM_STATE_RESERVED);
  if (!res->commBuf.memRegion)
  {
    NFR_LOG_DEBUG("Failed to allocate memory for communication buffer");
    return -ENOMEM;
  }

  res->commBuf.ctx = calloc(msgSlotCount, sizeof(*res->commBuf.ctx));
  if (!res->commBuf.ctx)
  {
    nfrFreeMemory(&res->commBuf.memRegion);
    return -ENOMEM;
  }

  struct NFRDataSlot * slots = res->commBuf.memRegion->addr;
  for (int i = 0; i < NFR_TOTAL_SLOTS(*hints); ++i)
  {
    slots[i].channelSerial   = 0;
    slots[i].msgSerial       = 0;
    res->commBuf.ctx[i].slot = (struct NFRDataSlot *) \
                         ((uint8_t *) slots + i * NETFR_MESSAGE_MAX_SIZE);
    res->commBuf.ctx[i].parentResource = res;
    res->commBuf.ctx[i].state = CTX_STATE_AVAILABLE;
  }

  return 0;
}

void nfr_CommBufClose(struct NFRCommBuf * buf)
{
  assert(buf);
  if (!buf)
    return;

  free((buf)->ctx);
  buf->ctx = 0;
  if ((buf)->memRegion)
  {
    nfrFreeMemory(&((buf)->memRegion));
    (buf)->memRegion = 0;
  }
}