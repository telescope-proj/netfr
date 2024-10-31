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

#ifndef NETFR_PROTOCOL_H
#define NETFR_PROTOCOL_H

#include <stdint.h>
#include <assert.h>
#include <string.h>

#include "netfr/netfr_constants.h"

enum NFRMessageType
{
  NFR_MSG_CLIENT_HELLO  = 1,
  NFR_MSG_SERVER_HELLO,
  NFR_MSG_BUFFER_HINT,
  NFR_MSG_BUFFER_UPDATE,
  NFR_MSG_BUFFER_STATE,
  NFR_MSG_CLIENT_DATA,
  NFR_MSG_CLIENT_DATA_ACK,
  NFR_MSG_HOST_DATA,
  NFR_MSG_HOST_DATA_ACK,
  NFR_MSG_MAX
};

enum NFRMessageStatus
{
  NFR_MSG_STATUS_INVALID = 0,
  NFR_MSG_STATUS_OK,
  NFR_MSG_STATUS_ERROR,
  NFR_MSG_STATUS_REJECTED,
  NFR_MSG_STATUS_MAX
};

#pragma pack(push, 1)

struct NFRHeader
{
  char     magic[8];
  uint8_t  version;
  uint8_t  type;
};

/* The size of the message padding needed to reach a 16-byte alignment. NetFR
   guarantees that user message payloads (ClientData/HostData) will be aligned
   to 16 bytes, which should be adequate for most user-defined structures and
   CPU architectures. */
#define NETFR_MESSAGE_PAD_SIZE (16 - sizeof(struct NFRHeader) % 16)

/* The maximum size of user messages, with the header and padding subtracted. */
#define NETFR_MESSAGE_MAX_PAYLOAD_SIZE (NETFR_MESSAGE_MAX_SIZE - 32)

inline static void nfr_SetHeader(struct NFRHeader * hdr, uint8_t mType)
{
  assert(mType < NFR_MSG_MAX);
  memcpy(hdr->magic, NETFR_MAGIC, 8);
  hdr->version = NETFR_VERSION;
  hdr->type    = mType;
}

/*
  Note: NFRMsgClientHello and NFRMsgServerHello are sent as part of the
  Libfabric CM connection handshake (i.e., it is placed into the CM param
  buffer). They should not be sent over the fabric itself.
*/

// NFRMsgClientHello: no payload

struct NFRMsgClientHello
{
  struct NFRHeader header;
};

// NFRMsgServerHello: no payload

struct NFRMsgServerHello
{
  struct NFRHeader header;
  uint8_t          status;
};

// NFRMsgBufferUpdate, server -> client

struct NFRMsgBufferUpdate
{
  struct NFRHeader header;
  uint8_t          bufferIndex;
  uint8_t          padding[NETFR_MESSAGE_PAD_SIZE - 1];
  uint32_t         payloadSize;
  uint32_t         payloadOffset;
  uint32_t         writeSerial;
  uint32_t         channelSerial;
};

// NFRMsgBufferState, client -> server

struct NFRMsgBufferState
{
  struct NFRHeader header;
  uint32_t         pageSize;
  uint64_t         addr;
  uint64_t         size;
  uint64_t         rkey;
  uint8_t          index;
};

// NFRMsgClientData, client -> server

struct NFR__MsgClientData
{
  struct NFRHeader header;
  uint32_t         length;
  uint32_t         msgSerial;
  uint32_t         channelSerial;
};

struct NFRMsgClientData
{
  struct NFRHeader header;
  uint32_t         length;
  uint32_t         msgSerial;
  uint32_t         channelSerial;
  uint8_t          padding[32 - sizeof(struct NFR__MsgClientData) % 32];
  uint8_t          data[NETFR_MESSAGE_MAX_PAYLOAD_SIZE];
};

// NFRMsgClientDataAck, server -> client

struct NFRMsgClientDataAck
{
  struct NFRHeader header;
};

// NFRMsgHostData, server -> client

struct NFR__MsgHostData
{
  struct NFRHeader header;
  uint32_t         length;
  uint32_t         msgSerial;
  uint32_t         channelSerial;
};

struct NFRMsgHostData
{
  struct NFRHeader header;
  uint32_t         length;
  uint32_t         msgSerial;
  uint32_t         channelSerial;
  uint8_t          padding[32 - sizeof(struct NFR__MsgHostData) % 32];
  uint8_t          data[NETFR_MESSAGE_MAX_PAYLOAD_SIZE];
};

// NFRMsgHostDataAck, client -> server

struct NFRMsgHostDataAck
{
  struct NFRHeader header;
};

#pragma pack(pop)

#endif