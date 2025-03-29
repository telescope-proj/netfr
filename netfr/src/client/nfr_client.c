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
#include "common/nfr_log.h"
#include "common/nfr_mem.h"
#include "common/nfr.h"

#include "client/nfr_client_callback.h"
#include "client/nfr_client.h"


/**
 * @brief Initiate the connection to the server. This is a non-blocking function
 *        that immediately returns; you must check the state of the EQ to see
 *        whether the connection was successful using nfr_CheckConnState.
 * 
 * @param res  Resource to use for the connection
 * @param tgt  Target address to connect to
 * @return     0 on success, negative error code on failure 
 */
int nfr_InitiateConnection(struct NFRResource * res, struct sockaddr_in * tgt)
{
  assert(res);
  assert(tgt);
  assert(res->connState == NFR_CONN_STATE_READY_TO_CONNECT);
  NFR_LOG_DEBUG("Initiating connection to %s:%d", 
                inet_ntoa(tgt->sin_addr), ntohs(tgt->sin_port));

  struct fi_info * info = fi_dupinfo(res->info);
  if (info->dest_addr)
    free(info->dest_addr);
  info->dest_addr    = tgt;
  info->dest_addrlen = sizeof(*tgt);
  int ret = fi_endpoint(res->domain, info, &res->ep, res);
  info->dest_addr    = 0;
  info->dest_addrlen = 0;
  fi_freeinfo(info);
  if (ret < 0)
  {
    NFR_LOG_DEBUG("Failed to create EP: %s (%d)", fi_strerror(-ret), ret);
    return ret;
  }

  ret = fi_ep_bind(res->ep, &res->eq->fid, 0);
  if (ret < 0)
  {
    NFR_LOG_DEBUG("Failed to bind EP to EQ: %s (%d)", fi_strerror(-ret), ret);
    goto closeEP;
  }

  ret = fi_ep_bind(res->ep, &res->cq->fid, FI_SEND | FI_RECV);
  if (ret < 0)
  {
    NFR_LOG_DEBUG("Failed to bind EP to CQ: %s (%d)", fi_strerror(-ret), ret);
    goto closeEP;
  }

  ret = fi_enable(res->ep);
  if (ret < 0)
  {
    NFR_LOG_DEBUG("Failed to enable EP: %s (%d)", fi_strerror(-ret), ret);
    goto closeEP;
  }

  size_t cmDataSize = 0, size = sizeof(cmDataSize);
  ret = fi_getopt(&res->ep->fid, FI_OPT_ENDPOINT, FI_OPT_CM_DATA_SIZE,
                  &cmDataSize, &size);
  if (ret < 0)
  {
    NFR_LOG_DEBUG("Failed to get CM data size: %s (%d)", fi_strerror(-ret), ret);
    goto closeEP;
  }

  NFR_LOG_DEBUG("Max CM data size: %lu", cmDataSize);

  struct NFRMsgClientHello hello;
  nfr_SetHeader(&hello.header, NFR_MSG_CLIENT_HELLO);
  ret = fi_connect(res->ep, (void *) tgt, &hello, sizeof(hello));
  if (ret < 0)
  {
    NFR_LOG_DEBUG("Failed to connect: %s (%d)", fi_strerror(-ret), ret);
    goto closeEP;
  }

  res->connState = NFR_CONN_STATE_CONNECTING;
  return 0;

closeEP:
  fi_close(&res->ep->fid);
  res->ep = 0;
  return ret;
}

/**
 * @brief Check whether the system is connected yet.
 * 
 * @param res 
 * @return int 
 */
int nfr_CheckConnState(struct NFRResource * res)
{
  assert(res);
  
  struct NFRExtCMEntry entry;
  uint32_t event;
  int ret = (int) fi_eq_read(res->eq, &event, &entry, sizeof(entry), 0);
  if (ret == 0 || ret == -FI_EAGAIN)
  {
    return (res->connState == NFR_CONN_STATE_CONNECTED);
  }
  if (ret == -FI_EAVAIL)
  {
    struct fi_eq_err_entry err;
    ret = (int) fi_eq_readerr(res->eq, &err, 0);
    if (ret < 0)
    {
      return ret;
    }
    switch (err.err)
    {
      case FI_ECONNREFUSED:
        NFR_LOG_DEBUG("Connection refused");
        return -err.err;
      case FI_EINPROGRESS:
        return 0;
      default:
        NFR_LOG_DEBUG("Error event: %s (%d)\n", fi_strerror(err.err), err.err);
        return -err.err;
    }
  }
  if (ret < 0)
  {
    return ret;
  }

  switch (event)
  {
    case FI_CONNECTED:
      res->connState = NFR_CONN_STATE_CONNECTED;
      return 1;
    case FI_SHUTDOWN:
    {
      struct NFRClient * client = res->parentTopLevel;
      for (int i = 0; i < NETFR_NUM_CHANNELS; ++i)
      {
        if (client->channels[i].res == res)
        {
          NFR_LOG_DEBUG("Server disconnected from channel %d, closing EP", i);
          break;
        }
      }
      fi_close(&res->ep->fid);
      res->ep = 0;
      res->connState = NFR_CONN_STATE_DISCONNECTED;
      return -FI_ECONNRESET;
    }
    default:
      NFR_LOG_DEBUG("Unexpected event: %d", event);
      return -EIO;
  }

  struct NFRMsgServerHello * helloResp = (struct NFRMsgServerHello *) entry.data;
  if (memcmp(helloResp->header.magic, NETFR_MAGIC, 8) != 0
      || helloResp->header.version != NETFR_VERSION
      || helloResp->header.type != NFR_MSG_SERVER_HELLO)
  {
    NFR_LOG_WARNING("Server sent invalid hello message");
    ret = -FI_ECONNRESET;
    goto close_ep;
  }

  res->connState = NFR_CONN_STATE_CONNECTED;
  return 1;

close_ep:
  fi_close(&res->ep->fid);
  res->ep = 0;
  return ret;
}

PNFRMemory nfrClientAttachMemory(PNFRClient client, void * buffer,
                                 uint64_t size, uint8_t index)
{
  assert(client);
  assert(size);
  assert(index < NETFR_NUM_CHANNELS);

  struct NFRResource * res = client->channels[index].res;
  return nfr_RdmaAttach(res, buffer, size,
                        FI_READ | FI_WRITE | FI_REMOTE_WRITE, 
                        NFR_MEM_TYPE_USER_MANAGED,
                        MEM_STATE_AVAILABLE_UNSYNCED);
}

int nfr_ClientGetOldestBufUpdate(struct NFRClientChannel * ch,
                                 struct NFRClientEvent * evt)
{
  assert(ch);
  assert(evt);

  int haveData = 0;
  uint32_t sub = 0;
  uint32_t limSerial = 0;

  for (int i = 0; i < NETFR_MAX_MEM_REGIONS; ++i)
  {
    struct NFRMemory * mem = ch->res->memRegions + i;
    if (mem->state == MEM_STATE_HAS_DATA && mem->channelSerial > limSerial)
      limSerial = mem->channelSerial;
  }

  if (limSerial > ((uint32_t) -1) - 2048)
    sub = 4096;
  
  limSerial = 0;
  
  for (int i = 0; i < NETFR_MAX_MEM_REGIONS; ++i)
  {
    struct NFRMemory * mem = ch->res->memRegions + i;
    if (mem->addr && mem->size && mem->mr)
    {
      if (mem->state == MEM_STATE_HAS_DATA)
      {
        if (!haveData || mem->channelSerial - sub < limSerial - sub)
        {
          evt->type          = NFR_CLIENT_EVENT_MEM_WRITE;
          evt->channelIndex  = ch - ch->parent->channels;
          evt->serial        = mem->channelSerial;
          evt->memRegion     = mem;
          evt->payloadOffset = mem->payloadOffset;
          evt->payloadLength = mem->payloadLength;
          limSerial          = mem->channelSerial;
          haveData = 1;
        }
      }
    }
  }

  if (haveData)
    return 1;

  return 0;
}

int nfr_ClientResyncBufs(PNFRClient client, uint8_t index)
{
  assert(client);
  assert(index < NETFR_NUM_CHANNELS);

  struct NFRClientChannel * ch = client->channels + index;
  struct NFRResource * res = ch->res;
  int nUpdated = 0;

  for (int i = 0; i < NETFR_MAX_MEM_REGIONS; ++i)
  {   
    if (res->memRegions[i].state == MEM_STATE_AVAILABLE_UNSYNCED
        && res->memRegions[i].memType != NFR_MEM_TYPE_INTERNAL)
    {
      NFR_LOG_DEBUG("Syncing buffer %d state", i);
      
      struct NFRMsgBufferState msg;
      nfr_SetHeader(&msg.header, NFR_MSG_BUFFER_STATE);
      msg.pageSize = 0;
      msg.addr     = (uintptr_t) res->memRegions[i].addr;
      msg.size     = res->memRegions[i].size;
      msg.rkey     = fi_mr_key(res->memRegions[i].mr);
      msg.index    = i;

      struct NFR_CallbackInfo cbInfo = {0};
      cbInfo.callback = nfr_ClientProcessInternalTx;

      struct NFR_TransferInfo ti = {0};
      ti.opType           = NFR_OP_SEND_COPY;
      ti.context          = 0;
      ti.data             = &msg;
      ti.cbInfo           = &cbInfo;
      ti.length           = sizeof(msg);

      ssize_t ret = nfr_PostTransfer(res, &ti);
      if (ret < 0)
      {
        if (ret == -EAGAIN)
          return nUpdated;
        return ret;
      }

      NFR_LOG_DEBUG("Buffer %d-%d state sync message sent", index, i);
      res->memRegions[i].state = MEM_STATE_AVAILABLE;
      ++nUpdated;
    }
  }

  return nUpdated;
}

int nfrClientProcess(PNFRClient client, int index, struct NFRClientEvent * evt)
{
  assert(client);
  assert(evt);
  assert(index < NETFR_NUM_CHANNELS);
  int ret;

  if (index < 0)
  {
    for (int i = 0; i < NETFR_NUM_CHANNELS; ++i)
    {
      ret = nfrClientProcess(client, i, evt);
      if (ret != 0)
      {
        if (ret < 0)
          NFR_LOG_DEBUG("Error processing channel %d: %s (%d)", i, fi_strerror(-ret), ret);
        return ret;
      }
    }
    return 0;
  }

  struct NFRClientChannel * ch = &client->channels[index];
  struct NFRResource * res = client->channels[index].res;
  assert(res);
  ASSERT_COMM_BUF_READY(ch->res->commBuf);
  
  ret = nfr_CheckConnState(res);
  if (ret < 0)
  {
    NFR_LOG_DEBUG("Connection error: %s (%d)",
                  fi_strerror(-ret), ret);
    return ret;
  }
  
  if (ch->res->connState != NFR_CONN_STATE_CONNECTED)
    return -ENOTCONN;
  
  // If any buffers have been freed or newly allocated, resync them
  ret = nfr_ClientResyncBufs(client, index);
  if (ret < 0)
    return ret;

  struct NFRCompQueueEntry cqe;
  memset(&cqe, 0, sizeof(cqe));
  
  // Process all completed operations
  ret = nfr_ResourceCQProcess(res, &cqe);
  if (ret < 0)
  {
    if (ret == -FI_EAVAIL && cqe.isError)
    {
      assert(ch->parent);
      return NFR_PRINT_CQ_ERROR(NFR_LOG_LEVEL_ERROR, ch, &cqe.entry.err);
    }
    return ret;
  }

  // Post receives if buffers available
  struct NFR_CallbackInfo cbInfo = {0};
  cbInfo.callback = nfr_ClientProcessInternalRx;
  cbInfo.uData[0] = ch;
  ret = nfr_ResourceConsumeRxSlots(res, &cbInfo);
  if (ret < 0)
    return ret;

  // Find the buffer updates first
  evt->serial = 0;
  int bufRet = nfr_ClientGetOldestBufUpdate(ch, evt);

  // Then the regular messages
  struct NFRFabricContext * ctx = 0;
  ret = nfr_ContextGetOldestMessage(ch->res, &ctx);
  
  // No message
  if (!ret && !bufRet)
    return 0;

  // Overflow compensation
  uint32_t sub = 0;
  uint32_t chSerial = ctx ? ctx->slot->channelSerial : 0;
  if (chSerial > ((uint32_t) -1) - 2048
      || evt->serial > ((uint32_t) -1) - 2048)
    sub = 4096;

  // Check for invalid serial set by the peer
  if (ret && bufRet)
  {
    assert(evt->serial != ctx->slot->channelSerial);
  }

  // Buffer update is the only event or the oldest one
  if (   (bufRet && !ret) 
      || (bufRet && ret 
          && (evt->serial - sub < ctx->slot->channelSerial - sub)))
    return 1;
  
  // Message is the only event or the oldest one
  if (   (ret && !bufRet)
      || (ret && bufRet && (ctx->slot->channelSerial - sub < evt->serial - sub)))
  {
    struct NFRMsgHostData * msg = (struct NFRMsgHostData *) ctx->slot->data;
    
    // Context manager should catch these
    assert(msg->length < NETFR_MESSAGE_MAX_PAYLOAD_SIZE);
    assert(msg->channelSerial == ctx->slot->channelSerial);
    assert(msg->msgSerial == ctx->slot->msgSerial);

    // Copy the message out of the context
    memset(evt, 0, offsetof(struct NFRClientEvent, inlineData));
    evt->type          = NFR_CLIENT_EVENT_DATA;
    evt->channelIndex  = index;
    evt->serial        = ctx->slot->channelSerial;
    evt->payloadLength = msg->length;
    evt->payloadOffset = 0;
    evt->udata         = msg->udata;
    memcpy(evt->inlineData, ctx->slot->data, msg->length);

    // Reuse the context to send the ack
    struct NFRMsgHostDataAck * ack = (struct NFRMsgHostDataAck *) ctx->slot->data;
    nfr_SetHeader(&ack->header, NFR_MSG_HOST_DATA_ACK);

    struct NFR_CallbackInfo cbInfo = {0};
    cbInfo.callback = nfr_ClientProcessInternalTx;
    
    struct NFR_TransferInfo ti = {0};
    ti.opType           = NFR_OP_SEND;
    ti.context          = ctx;
    ti.cbInfo           = &cbInfo;
    ti.length           = sizeof(*ack);

    ret = nfr_PostTransfer(res, &ti);
    if (ret < 0)
    {
      NFR_LOG_WARNING("Failed to send ack: %s (%d)", fi_strerror(-ret), ret);
      return ret;
    }

    NFR_LOG_TRACE("Sent ack for message %u", evt->serial);
    return 1;
  }

  assert(!"Unreachable code");
  abort();
}

int nfrClientSendData(struct NFRClient * client, int channelID, 
                      const void * data, uint32_t length, uint64_t udata)
{
  assert(client);
  assert(data);
  assert(channelID >= 0 && channelID < NETFR_NUM_CHANNELS);
  assert(length < NETFR_MESSAGE_MAX_PAYLOAD_SIZE);

  if (!client || !data || channelID < 0 || channelID >= NETFR_NUM_CHANNELS)
    return -EINVAL;

  if (length > NETFR_MESSAGE_MAX_PAYLOAD_SIZE)
  {
    NFR_LOG_DEBUG("Data too large: %u", length);
    return -ENOBUFS;
  }

  struct NFRClientChannel * ch = client->channels + channelID;
  if (ch->res->txCredits < NETFR_RESERVED_CREDIT_COUNT)
  {
    NFR_LOG_DEBUG("No%scredits on channel %d", 
              ch->res->txCredits < NETFR_RESERVED_CREDIT_COUNT ? " low-prio " 
                                                               : " ",
              channelID);
    return -EAGAIN;
  }

  struct NFRResource * res = ch->res;
  ASSERT_COMM_BUF_READY(res->commBuf);

  struct NFRFabricContext * ctx = nfr_ContextGet(res, NFR_OP_SEND, 0);
  if (!ctx)
    return -EAGAIN;

  struct NFRMsgClientData * msg = (struct NFRMsgClientData *) ctx->slot->data;
  nfr_SetHeader(&msg->header, NFR_MSG_CLIENT_DATA);
  msg->length        = length;
  msg->msgSerial     = ++ch->msgSerial;
  msg->channelSerial = ++ch->channelSerial;
  msg->udata         = udata;
  memcpy(msg->data, data, length);

  struct NFR_CallbackInfo cbInfo = {0};
  cbInfo.callback = nfr_ClientProcessInternalTx;

  struct NFR_TransferInfo ti = {0};
  ti.opType           = NFR_OP_SEND;
  ti.context          = ctx;
  ti.cbInfo           = &cbInfo;
  ti.length           = length + offsetof(struct NFRMsgClientData, data);

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

int nfrClientConnect(struct NFRClient * client)
{
  assert(client);
  if (!client)
    return -EINVAL;

  // Initialize the connection
  int ret;
  int connOk = 0;
  for (int i = 0; i < NETFR_NUM_CHANNELS; ++i)
  {
    switch (client->channels[i].res->connState)
    {
      case NFR_CONN_STATE_READY_TO_CONNECT:
      {
        struct NFRResource * res = client->channels[i].res;
        assert(res);
        if (!res)
        {
          NFR_LOG_DEBUG("Resource not found");
          return -EINVAL;
        }

        ret = nfr_InitiateConnection(res, &client->peerInfo.addrs[i]);
        if (ret < 0)
        {
          NFR_LOG_DEBUG("Failed to initiate connection on channel %d: %s (%d)",
                        i, fi_strerror(-ret), ret);
          return ret;
        }
        break;
      }
      case NFR_CONN_STATE_CONNECTING:
      {
        ret = nfr_CheckConnState(client->channels[i].res);
        if (ret < 0)
        {
          NFR_LOG_DEBUG("Connection error: %s (%d)",
                        fi_strerror(-ret), ret);
          return ret;
        }
        break;
      }
      case NFR_CONN_STATE_CONNECTED:
      {
        ++connOk;
        break;
      }
    }
  }

  if (connOk == NETFR_NUM_CHANNELS)
    return 0;

  return -EAGAIN;
}

int nfrClientInit(const struct NFRInitOpts * opts, 
                  const struct NFRInitOpts * peerInfo, 
                  struct NFRClient ** result)
{
  if (!opts || !peerInfo || !result)
    return -EINVAL;

  struct NFRResource * res[2];
  memset(res, 0, sizeof(res));
  int ret = nfr_ResourceOpen(opts, res);
  if (ret < 0)
  {
    NFR_LOG_DEBUG("Failed to open resources: %d", ret);
    return ret;
  }

  struct NFRClient * client = calloc(1, sizeof(*client));
  if (!client)
  {
    NFR_LOG_DEBUG("Memory allocation failed");
    ret = -ENOMEM;
    goto closeResources;
  }

  for (int i = 0; i < NETFR_NUM_CHANNELS; ++i)
  {
    res[i]->parentTopLevel = client;
  }

  for (int i = 0; i < NETFR_NUM_CHANNELS; ++i)
  {
    client->channels[i].parent = client;
    client->channels[i].res = res[i];
    client->channels[i].res->txCredits = NETFR_CREDIT_COUNT;
    struct NFRCommBufInfo info = nfr_GetDefaultCommBufInfo();
    ret = nfr_CommBufOpen(res[i], &info);
    if (ret < 0)
    {
      NFR_LOG_DEBUG("Failed to open communication buffer: %s (%d)",
                    fi_strerror(-ret), ret);
      goto closeResources;
    }
    client->channels[i].res->connState = NFR_CONN_STATE_READY_TO_CONNECT;
  }

  memcpy(&client->peerInfo, peerInfo, sizeof(*peerInfo));
  *result = client;
  return 0;

closeResources:
  for (int i = 0; i < NETFR_NUM_CHANNELS; ++i)
  {
    nfr_ResourceClose(res[i]);
  }
  free(client);
  return ret;
}

void nfrClientFree(PNFRClient * res)
{
  if (!res || !*res)
    return;

  struct NFRClient * client = *res;
  for (int i = 0; i < NETFR_NUM_CHANNELS; ++i)
  {
    if (client->channels[i].res)
    {
      nfr_CommBufClose(&client->channels[i].res->commBuf);
      nfr_ResourceClose(client->channels[i].res);
    }
  }

  free(client);
  *res = 0;
}