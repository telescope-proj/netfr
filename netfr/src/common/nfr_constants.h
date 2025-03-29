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

#ifndef NETFR_PRIVATE_CONSTANTS_H
#define NETFR_PRIVATE_CONSTANTS_H

#include "netfr/netfr_constants.h"

#define NFR_INTERNAL_CB_UDATA_COUNT (NETFR_CALLBACK_USER_DATA_COUNT + 8)
#define NFR_USER_CB_INDEX           (NFR_INTERNAL_CB_UDATA_COUNT - NETFR_CALLBACK_USER_DATA_COUNT)

enum ContextState
{
  CTX_STATE_INVALID,

  /* Available for use */
  CTX_STATE_AVAILABLE,

  /* This context is specifically reserved for sending DataAck messages, which
     have no unique data and can thus be used by multiple send requests
     simultaneously */
  CTX_STATE_ACK_ONLY,

  /* Allocated but not yet transitioned to the wait state. Used only to detect
     bugs in the code where a context is reserved and a transmission error
     occurs without proper cleanup afterwards. */
  CTX_STATE_ALLOCATED,

  /* Data transfer request submitted using this context, waiting for it to
     complete. */
  CTX_STATE_WAITING,

  /* Data receive completed, but the data slot of this context still must be
  read using ``nfrHostReadData`` beofre it can be reused. */
  CTX_STATE_HAS_DATA,

  /* The operation associated with the context has been canceled. */
  CTX_STATE_CANCELED,
 
  CTX_STATE_MAX
};

enum MemoryState
{
  MEM_STATE_INVALID,
  /* Does not currently contain a memory region */
  MEM_STATE_EMPTY,
  /* Internal use only */
  MEM_STATE_RESERVED,
  /* Remote end has not yet been informed of the change */
  MEM_STATE_AVAILABLE_UNSYNCED,
  /* Ready to use for RDMA ops */
  MEM_STATE_AVAILABLE,
  /* Memory region is currently being used for an RDMA operation */
  MEM_STATE_BUSY,
  /* Memory region has data that needs to be read */
  MEM_STATE_HAS_DATA,
  
  MEM_STATE_MAX
};

enum NFROpType
{
  NFR_OP_NONE,
  NFR_OP_SEND      = (1 << 0), // Regular message send
  NFR_OP_SEND_COPY = (1 << 1), // Copy data from user-defined buffer to context
  NFR_OP_INJECT    = (1 << 2), // Send without consuming a context (limited size)
  NFR_OP_RECV      = (1 << 3), // Regular message receive
  NFR_OP_WRITE     = (1 << 4), // RDMA write
  NFR_OP_ACK       = (1 << 5), // Message acknowledgement
  NFR_OP_MAX
};

enum NFRChannelIndex
{
  NFR_CHANNEL_PRIMARY,
  NFR_CHANNEL_SECONDARY,
  NFR_CHANNEL_MAX
};

enum NFRRemoteMemoryState
{
  NFR_RMEM_NONE,          // This index is not in use
  NFR_RMEM_AVAILABLE,     // This index is ready to be used for writes
  NFR_RMEM_ALLOCATED,     // Allocated but not yet used in an operation (debug)
  NFR_RMEM_BUSY_LOCAL,    // Local NIC performing RDMA op on this memory
  NFR_RMEM_BUSY_REMOTE,   // Local RDMA op done, remote side did not ack yet
  NFR_RMEM_MAX
};

enum NFRConnState
{
  NFR_CONN_STATE_NONE,
  NFR_CONN_STATE_DISCONNECTED,
  NFR_CONN_STATE_READY_TO_CONNECT,
  NFR_CONN_STATE_CONNECTING,
  NFR_CONN_STATE_CONNECTED_NEED_RESOURCES,
  NFR_CONN_STATE_CONNECTED,
  NFR_CONN_STATE_MAX
};

#endif