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
#ifndef NETFR_CLIENT_H
#define NETFR_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "netfr/netfr.h"
#include <stdalign.h>

typedef struct NFRClient * PNFRClient;

enum
{
  NFR_CLIENT_EVENT_INVALID,
  /* The peer used RDMA writes to write directly into a buffer. Usually best
     used for large messages exceeding ``NETFR_MESSAGE_MAX_PAYLOAD_SIZE``. */
  NFR_CLIENT_EVENT_MEM_WRITE,
  /* The host sent a message using the standard ``nfrHostSendData`` function.
     This is ideal for small high-frequency messages or metadata updates. */
  NFR_CLIENT_EVENT_DATA,
  NFR_CLIENT_EVENT_MAX
};

struct NFRClientEvent
{
  /* The type of event received. */
  uint8_t type;

  /* The index of the channel this message was received on. */
  uint8_t channelIndex;

  /* The unique incrementing ID of the message. */
  uint32_t serial;

  /* Only valid for NFR_CLIENT_EVENT_MEM_WRITE. The memory region where the
     message data reside. */
  PNFRMemory memRegion;

  /* Only valid for NFR_CLIENT_EVENT_MEM_WRITE. The offset between the start of
     the memory region and the payload. The entire memory region is, however,
     available for the (local) user to read and write until releasing this
     region. */
  uint32_t payloadOffset;
  
  /* The size of the payload in the memory region or inline data, depending on
     the type of event */
  uint32_t payloadLength;
  
  /* If the event type is NFR_CLIENT_EVENT_DATA, this field will contain
     the message that was sent over the fabric. */
  alignas(16) char inlineData[NETFR_MESSAGE_MAX_SIZE];
};

/**
 * @brief Attach an existing memory region to the client. This will allow it to
 *        be used for RDMA writes.
 *
 * @warning This function does not support the use of DMABUFs or GPU memory
 *          regions, except when the DMABUF page mappings are stable and reside
 *          in host memory (e.g., KVMFR memory)
 *
 * @note For optimal performance, the memory region should be page-aligned. If
 *       huge pages are used, the memory region should be aligned to the huge
 *       page size, and the environment variable `RDMAV_HUGEPAGES_SAFE` must be
 *       set to `1`.
 *
 * @param client  Client handle
 * 
 * @param buffer  Memory region
 * 
 * @param size    Size of the memory region
 * 
 * @param index   The channel index to make the memory region available on
 *
 * @return PNFRMemory 
 */
PNFRMemory nfrClientAttachMemory(PNFRClient client, void * buffer,
                                 uint64_t size, uint8_t index);


/**
 * @brief Check for incoming messages and progress background operations.
 *
 * @param client  NetFR client handle
 *
 * @param index   Channel index to process, or -1 to process all channels. When
 *                processing all channels, the event available at the lowest
 *                index will be returned.
 *
 * @param evt     Output event
 *
 * @return        int 
 */
int nfrClientProcess(PNFRClient client, int index, struct NFRClientEvent * evt);

/**
 * @brief Initiate the connection to the server.
 *
 * This is a non-blocking function that immediately returns. It will take
 * multiple repeated calls until the connection is established (or fails).
 *
 * @param client Client handle created with nfrClientInit
 *
 * @return       0 on success
 *
 *               Negative on error
 *
 *               ``-EAGAIN`` if the connection process is still underway
 *
 *               ``-ECONNREFUSED`` if the connection was refused by the server
 */
int nfrClientSessionInit(PNFRClient client);

/**
 * @brief Initialize a client handle. 
 *
 * This does not yet connect to the server, but prepares all of the resources
 * necessary to do so. Use the nfrClientSessionInit function to initiate a
 * connection.
 *
 * @param opts      Network options
 * 
 * @param peerInfo  Server addressing information
 * 
 * @param result    Client handle output
 * 
 * @return          0 on success, negative on error 
 */
int nfrClientInit(const struct NFRInitOpts * opts, 
                  const struct NFRInitOpts * peerInfo, 
                  PNFRClient * result);

/**
 * @brief Close the fabric endpoint and free up its resources.
 * 
 * @param res The transport handle
 * 
 */
void nfrClientFree(PNFRClient * res);

/**
 * @brief Send arbitrary data to the host.
 *
 * @param client     Client handle
 *
 * @param channelID  Channel index
 *
 * @param data       Data to send
 *
 * @param length     Length of the data, limited to
 *                   ``NETFR_MESSAGE_MAX_PAYLOAD_SIZE``
 *
 * @return           0 on success, negative on error 
 */
int nfrClientSendData(struct NFRClient * client, int channelID, const void * data,
                      uint32_t length);

#ifdef __cplusplus
}
#endif

#endif