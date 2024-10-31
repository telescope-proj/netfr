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

#include "host/nfr_host.h"
#include "common/nfr_protocol.h"
#include "common/nfr_constants.h"
#include "host/nfr_host_callback.h"
#include "common/nfr.h"

int nfr_HostCreatePassiveEndpoint(struct NFRResource * tr)
{
  int ret;

  assert(tr);
  assert(tr->info);
  assert(tr->fabric);

  const struct sockaddr_in * sai = \
    (const struct sockaddr_in *) tr->info->src_addr;
  if (!sai)
  {
    NFR_LOG_ERROR("Invalid source address");
    return -EINVAL;
  }

  const char * addr = inet_ntoa(sai->sin_addr);
  int port = ntohs(sai->sin_port);

  NFR_LOG_DEBUG("Creating PEP for resource %p; af: %d addr: %s:%d",
                tr, sai->sin_family, addr, port);

  ret = fi_passive_ep(tr->fabric, tr->info, &tr->pep, tr);
  if (ret < 0)
  {
    NFR_LOG_ERROR("Failed to create PEP: %s (%d)", 
                  fi_strerror(-ret), ret);
    return ret;
  }
  
  ret = fi_pep_bind(tr->pep, &tr->eq->fid, 0);
  if (ret < 0)
  {
    NFR_LOG_ERROR("Failed to bind passive EP to event queue: %s (%d)", 
                  fi_strerror(-ret), ret);
    goto close_pep;
  }

  ret = fi_listen(tr->pep);
  if (ret < 0)
  {
    NFR_LOG_ERROR("Failed to listen on passive EP: %s (%d)", 
                  fi_strerror(-ret), ret);
    goto close_pep;
  }

  NFR_LOG_DEBUG("PEP created for resource %p", tr);
  return ret;

close_pep:
  fi_close(&tr->pep->fid);
  return ret;
}

int nfr_HostChannelProcess(struct NFRHostChannel * ch,
                           struct NFRCompQueueEntry * cqe)
{
  assert(ch);
  assert(ch->res);
  assert(ch->parent);
  assert(ch->res->parentTopLevel);
  assert(cqe);

  int ret = 0;
  int totalComp = 0;
  struct NFRResource * res = ch->res;

  // Process all completed operations
  ret = nfr_ResourceCQProcess(res, cqe);
  if (ret < 0)
  {
    if (ret == -FI_EAVAIL && cqe->isError)
    {
      return NFR_PRINT_CQ_ERROR(NFR_LOG_LEVEL_ERROR, ch, &cqe->entry.err);
    }
    return ret;
  }

  // Post receives if buffers available
  struct NFR_CallbackInfo cbInfo = {0};
  cbInfo.callback = nfr_HostProcessInternalRx;
  cbInfo.uData[0] = ch;
  ret = nfr_ResourceConsumeRxSlots(res, &cbInfo);
  if (ret < 0)
    return ret;
  
  return totalComp;
}

int nfrHostReadData(struct NFRHost * host, int channelID, void * data,
                    uint32_t * maxLength)
{
  assert(host);
  assert(data);
  assert(maxLength);
  assert(channelID >= 0 && channelID < NETFR_NUM_CHANNELS);

  if (!host || !data || !maxLength)
    return -EINVAL;

  if (channelID < 0 || channelID >= NETFR_NUM_CHANNELS)
    return -EINVAL;

  if (*maxLength == 0)
    return -EINVAL;
  
  struct NFRHostChannel * hc = host->channels + channelID;
  struct NFRResource * res = hc->res;
  ASSERT_COMM_BUF_READY(res->commBuf);
  struct NFRCommBuf * cb = &res->commBuf;
  
  for (int i = NFR_RX_SLOT_BASE(cb->info); 
       i < NFR_RX_SLOT_BASE(cb->info) + cb->info.rxSlots; ++i)
  {
    if (cb->ctx[i].state == CTX_STATE_HAS_DATA)
    {
      struct NFRMsgClientData * msg = (struct NFRMsgClientData *) \
        cb->ctx[i].slot->data;
      if (msg->length > NETFR_MESSAGE_MAX_PAYLOAD_SIZE)
      {
        assert(!"Invalid message length");
        NFR_RESET_CONTEXT(cb->ctx + i);
        return -EBADMSG;
      }
      
      if (msg->length > *maxLength)
      {
        NFR_RESET_CONTEXT(cb->ctx + i);
        return -ENOBUFS;
      }

      memcpy(data, msg->data, msg->length);
      *maxLength = msg->length;

      NFR_RESET_CONTEXT(cb->ctx + i);

      // Send the acknowledgement
      struct NFRFabricContext * ctx = nfr_ContextGet(res, NFR_OP_ACK, 0);
      assert(ctx);

      struct NFRMsgClientDataAck * ack = (struct NFRMsgClientDataAck *) \
        ctx->slot->data;

      nfr_SetHeader(&ack->header, NFR_MSG_CLIENT_DATA_ACK);

      struct NFR_CallbackInfo cbInfo = {0};
      cbInfo.callback = nfr_HostProcessInternalTx;

      struct NFR_TransferInfo ti = {0};
      ti.opType           = NFR_OP_SEND;
      ti.context          = ctx;
      ti.length           = sizeof(*ack);
      ti.cbInfo           = &cbInfo;

      ssize_t ret = nfr_PostTransfer(res, &ti);
      if (ret < 0)
      {
        NFR_RESET_CONTEXT(ctx);
        return ret;
      }
    }
  }

  return -EAGAIN;
}

int nfrHostSendData(struct NFRHost * host, int channelID, void * data, 
                    uint32_t length)
{
  assert(host);
  assert(data);
  assert(channelID >= 0 && channelID < NETFR_NUM_CHANNELS);
  assert(length < NETFR_MESSAGE_MAX_PAYLOAD_SIZE);

  if (!host || !data || channelID < 0 || channelID >= NETFR_NUM_CHANNELS)
    return -EINVAL;

  if (length >= NETFR_MESSAGE_MAX_PAYLOAD_SIZE)
    return -ENOBUFS;

  struct NFRHostChannel * ch = host->channels + channelID;
  if (ch->res->txCredits < NETFR_RESERVED_CREDIT_COUNT)
  {
    NFR_LOG_DEBUG("No%scredits on channel %d", 
                  ch->res->txCredits < NETFR_RESERVED_CREDIT_COUNT ? " low-prio " : " ",
                  channelID);
    return -EAGAIN;
  }

  struct NFRResource * res = ch->res;
  ASSERT_COMM_BUF_READY(res->commBuf);

  struct NFRFabricContext * ctx = nfr_ContextGet(res, NFR_OP_SEND, 0);
  if (!ctx)
    return -EAGAIN;

  struct NFRMsgHostData * msg = (struct NFRMsgHostData *) ctx->slot->data;
  nfr_SetHeader(&msg->header, NFR_MSG_CLIENT_DATA);
  msg->length        = length;
  msg->channelSerial = ++ch->channelSerial;
  msg->msgSerial     = ++ch->msgSerial;
  memcpy(msg->data, data, length);

  struct NFR_CallbackInfo cbInfo = {0};
  cbInfo.callback = nfr_HostProcessInternalTx;

  struct NFR_TransferInfo ti = {0};
  ti.opType           = NFR_OP_SEND;
  ti.context          = ctx;
  ti.cbInfo           = &cbInfo;
  ti.length           = length + offsetof(struct NFRMsgHostData, data);

  ssize_t ret = nfr_PostTransfer(res, &ti);
  if (ret < 0)
  {
    NFR_RESET_CONTEXT(ctx);
    --ch->msgSerial;
    --ch->channelSerial;
    return ret;
  }
  
  --ch->res->txCredits;
  return ret;
}

int nfrHostProcess(struct NFRHost * host)
{
  assert(host);
  int ret;
  for (int i = 0; i < NETFR_NUM_CHANNELS; ++i)
  {
    struct NFRHostChannel * chan = host->channels + i;
    if (!chan->res)
      continue;

    struct NFRResource * res = chan->res;
    ASSERT_COMM_BUF_READY(res->commBuf);

    // Check for connection state updates
    assert(res->pep);
    uint32_t event;
    struct NFRExtCMEntry entry;
    ret = (int) fi_eq_read(res->eq, &event, &entry, sizeof(entry), 0);
    if (ret < 0 && ret != -FI_EAGAIN)
    {
      assert(!"Error in event queue");
      return ret;
    }
    if (ret > 0)
    {
      if (event == FI_CONNREQ)
      {
        struct NFRMsgServerHello hello;
        nfr_SetHeader(&hello.header, NFR_MSG_SERVER_HELLO);
        if (nfrHostClientsConnected(host, i))
        {
          NFR_LOG_DEBUG("Other client already connected, rejecting new request");
          hello.status = NFR_MSG_STATUS_REJECTED;
          errno = 0;
          int ret2 = fi_reject(res->pep, entry.info->handle, 
                               &hello, sizeof(hello));
          if (ret2 < 0)
          {
            NFR_LOG_ERROR("Failed to reject connection %d: %s (%d)",
                          errno, fi_strerror(-ret2), ret2);
            return ret2;
          }
          fi_freeinfo(entry.info);
          continue;
        }
        else
        {
          ret = fi_endpoint(res->domain, entry.info, &res->ep, res);
          if (ret < 0)
          {
            fi_freeinfo(entry.info);
            assert(!"Failed to create endpoint");
            return ret;
          }

          ret = fi_ep_bind(res->ep, &res->eq->fid, 0);
          if (ret < 0)
          {
            fi_freeinfo(entry.info);
            assert(!"Failed to bind endpoint");
            return ret;
          }

          ret = fi_ep_bind(res->ep, &res->cq->fid, FI_SEND | FI_RECV);
          if (ret < 0)
          {
            fi_freeinfo(entry.info);
            assert(!"Failed to bind endpoint");
            return ret;
          }

          ret = fi_enable(res->ep);
          if (ret < 0)
          {
            fi_freeinfo(entry.info);
            assert(!"Failed to enable endpoint");
            return ret;
          }

          hello.status = NFR_MSG_STATUS_OK;
          ret = fi_accept(res->ep, &hello, sizeof(hello));
          fi_freeinfo(entry.info);
          if (ret < 0)
          {
            fi_close(&res->ep->fid);
            assert(!"Failed to accept connection");
            return ret;
          }
        }
      }
      else if (event == FI_CONNECTED)
      {
        NFR_LOG_DEBUG("Client connected on channel %d", i);
      }
      else if (event == FI_SHUTDOWN)
      {
        NFR_LOG_DEBUG("Client disconnected on channel %d", i);
        fi_close(&res->ep->fid);
        res->ep = 0;
      }
      else
      {
        assert(!"Unexpected event");
        return -EINVAL;
      }
    }

    // If there is no client, don't do anything
    if (!res->ep)
      return -FI_ENOTCONN;

    // Process all items in the queue
    struct NFRCompQueueEntry cqe;
    int ret = nfr_HostChannelProcess(host->channels + i, &cqe);
    if (ret < 0)
    {
      if (cqe.isError)
      {
        assert(!"Error in completion queue");
        return ret;
      }
    }
  }
  return 0;
}

int nfrHostInit(const struct NFRInitOpts * opts, struct NFRHost ** result)
{
  if (!opts || !result)
    return -EINVAL;

  struct NFRResource * res[NETFR_NUM_CHANNELS];
  memset(res, 0, sizeof(res));
  int ret = nfr_ResourceOpen(opts, res);
  if (ret < 0)
    return ret;

  struct NFRHost * host = calloc(1, sizeof(*host));
  if (!host)
  {
    ret = -ENOMEM;
    goto closeResources;
  }

  for (int i = 0; i < NETFR_NUM_CHANNELS; ++i)
  {
    host->channels[i].res = res[i];
    host->channels[i].res->txCredits = NETFR_CREDIT_COUNT;
    host->channels[i].res->parentTopLevel = host;
    host->channels[i].parent = host;
    for (int j = 0; j < NETFR_MAX_MEM_REGIONS; ++j)
    {
      host->channels[i].clientRegions[j].parentResource = res[i];
      host->channels[i].clientRegions[j].index = j;
    }
  }

  for (int i = 0; i < NETFR_NUM_CHANNELS; ++i)
  {
    ret = nfr_HostCreatePassiveEndpoint(res[i]);
    if (ret < 0)
    {
      NFR_LOG_DEBUG("Passive endpoint creation failed on channel %d: %s (%d)\n",
                    i, fi_strerror(-ret), ret);
      goto closeResources;
    }

    struct NFRCommBufInfo info = nfr_GetDefaultCommBufInfo();
    host->channels[i].res = res[i];
    ret = nfr_CommBufOpen(res[i], &info);
    if (ret < 0)
    {
      NFR_LOG_DEBUG("Failed to open comm. buffer on channel %d: %s (%d)\n",
                    i, fi_strerror(-ret), ret);
      goto closeResources;
    }
  }

  *result = host;
  return 0;

closeResources:
  for (int i = 0; i < NETFR_NUM_CHANNELS; ++i)
  {
    nfr_ResourceClose(res[i]);
  }
  free(host);
  return ret;
}

int nfrHostClientsConnected(PNFRHost host, int index)
{
  assert(host);
  if (!host)
    return -EINVAL;

  if (index >= 0 || index < NETFR_NUM_CHANNELS)
  {
    if (index >= NETFR_NUM_CHANNELS)
      return -EINVAL;
    return !!host->channels[index].res->ep;
  }

  assert(!"Invalid index");
  return -EINVAL;
}

PNFRMemory nfrHostAttachMemory(PNFRHost host, void * buffer,
                               uint64_t size, uint8_t index)
{
  assert(host);
  assert(buffer);
  assert(size);
  assert(index < NETFR_NUM_CHANNELS);

  // We don't need to perform the sync as the host, so we immediately set the
  // state to available
  PNFRMemory mem = nfr_RdmaAttach(host->channels[index].res, buffer, size,
                                  FI_READ | FI_WRITE | FI_REMOTE_WRITE, 1,
                                  MEM_STATE_AVAILABLE);
  if (mem)
    mem->state = MEM_STATE_AVAILABLE;

  return mem;
}

int nfrHostWriteBuffer(PNFRMemory localMem, uint64_t localOffset,
                       uint64_t remoteOffset, uint64_t length,
                       struct NFRCallbackInfo * cbInfo)
{
  assert(localMem);
  assert(length);
  assert(localOffset + length <= localMem->size);

  struct NFRResource * res = localMem->parentResource;
  ASSERT_COMM_BUF_READY(res->commBuf);

  struct NFRHost * host = (struct NFRHost *) res->parentTopLevel;
  assert(host);

  struct NFRHostChannel * chan = 0;
  for (int i = 0; i < NETFR_NUM_CHANNELS; ++i)
  {
    if ((host->channels + i)->res == res)
    {
      chan = host->channels + i;
      break;
    }
  }

  if (!chan)
  {
    assert(!"Resource not found in host");
    return -EINVAL;
  }
  
  // The first pass checks for the smallest buffer which can hold the data
  uint32_t minBufSize = (uint32_t) -1;
  int minBufIndex = -1;
  for (int i = 0; i < NETFR_MAX_MEM_REGIONS; ++i)
  {
    if (chan->clientRegions[i].state == NFR_RMEM_AVAILABLE)
    {
      if (chan->clientRegions[i].size < length - remoteOffset)
        continue;
      if (chan->clientRegions[i].size < minBufSize)
      {
        minBufSize = chan->clientRegions[i].size;
        minBufIndex = i;
      }
    }
  }

  if (minBufIndex < 0)
  {
    NFR_LOG_TRACE("Could not find suitable RDMA write buffer");
    return -ENOBUFS;
  }

  chan->clientRegions[minBufIndex].state = NFR_RMEM_ALLOCATED;
  
  // Second pass sends the work request
  struct NFRRemoteMemory * remoteMem = chan->clientRegions + minBufIndex;
  
  struct NFR_CallbackInfo icbInfo;
  icbInfo.callback = nfr_HostProcessInternalWrite;
  icbInfo.uData[0] = chan;
  icbInfo.uData[1] = localMem;
  icbInfo.uData[2] = remoteMem;
  icbInfo.uData[3] = (void *) (uintptr_t) localOffset;
  icbInfo.uData[4] = (void *) (uintptr_t) remoteOffset;
  icbInfo.uData[5] = (void *) (uintptr_t) length;
  icbInfo.uData[6] = cbInfo->callback;
  memcpy(&icbInfo.uData[NFR_USER_CB_INDEX], cbInfo->uData,
         sizeof(cbInfo->uData));

  struct NFR_CallbackInfo scbInfo = {0};
  scbInfo.callback = nfr_HostProcessInternalTx;
 
  struct NFR_TransferInfo ti   = {0};
  ti.opType                    = NFR_OP_WRITE;
  ti.length                    = length;
  ti.cbInfo                    = &scbInfo;
  ti.writeOpts.localMem        = localMem;
  ti.writeOpts.localOffset     = localOffset;
  ti.writeOpts.remoteMem       = remoteMem;
  ti.writeOpts.remoteOffset    = remoteOffset;
  ti.writeOpts.writeCbInfo     = &icbInfo;

  ssize_t ret = nfr_PostTransfer(res, &ti);
  if (ret < 0)
    chan->clientRegions[minBufIndex].state = NFR_RMEM_AVAILABLE;

  NFR_LOG_DEBUG("Posted RDMA write from %p -> %p", localMem->addr,
                (void *) (uintptr_t) remoteMem->addr);
  return ret;
}

void nfrHostFree(PNFRHost * res)
{
  if (!res || !*res)
    return;

  struct NFRHost * host = *res;
  for (int i = 0; i < NETFR_NUM_CHANNELS; ++i)
  {
    if (host->channels[i].res)
    {
      nfr_CommBufClose(&host->channels[i].res->commBuf);
      nfr_ResourceClose(host->channels[i].res);
    }
  }

  free(host);
  *res = 0;
}