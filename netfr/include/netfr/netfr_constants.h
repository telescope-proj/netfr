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

#ifndef NETFR_CONSTANTS_H
#define NETFR_CONSTANTS_H

#ifdef __cplusplus
extern "C" {
#endif

#define NETFR_VERSION 1
#define NETFR_MAGIC   "NetFrame"

/* NetFR can store a limited amount of additional user data when performing its
   callbacks to user functions. This can eliminate the need for users to
   allocate their own context structures, instead using the uData array to store
   the necessary information. This constant defines how much data should be
   storable. */
#define NETFR_CALLBACK_USER_DATA_COUNT 8

/* Number of independent fabric channels. Currently defined are the primary and
   secondary low-latency channels. */
#define NETFR_NUM_CHANNELS 2

/* The total number of NetFR-managed memory regions that can be allocated. These
   are used specifically for RDMA write operations and are managed internally by
   the NetFR library. You can also allocate your own self-managed memory regions
   which do not count towards this limit. However, such regions cannot be used
   with the standard NetFR protocol functions. */
#define NETFR_MAX_MEM_REGIONS 32

/* The total number of context slots for the NetFR library. A context slot is
   used to store the state of a single operation, a pointer to an exclusively
   owned buffer, the callback to invoke upon its completion, as well as the user
   data to pass to the callback. */
#define NETFR_TOTAL_CONTEXT_COUNT 128

/* The number of slots reserved for metadata operations. These are used to
   perform infrequent exchanges of large amounts of user-defined metadata, which
   cannot fit into the standard message slots. */
#define NETFR_NUM_METADATA_CONTEXTS 1

/* The maximum amount of data which can be exchanged on connection setup via
   the Libfabric connection manager channel. */
#define NETFR_CM_MESSAGE_MAX_SIZE 16

/* The maximum size of a message including the header (not for RDMA buffers) */
#define NETFR_MESSAGE_MAX_SIZE 4096

/* The maximum size of a metadata message including the header */
#define NETFR_METADATA_MAX_SIZE (1 << 21)

/* The maximum size of a/an (R)DMA buffer is determined by the provider and
   hardware capabilities for the maximum buffer size that can be handled in a
   single work request. For RDMA, this is typically 1 GiB; we set a limit of 256
   MiB as this covers most use cases. */
#define NETFR_MAX_BUFFER_SIZE (1 << 28)

/* We want to align the message slots to cache lines, so we leave a bit of
   extra space for the non-protocol internal metadata to go */
#define NETFR_MESSAGE_SLOT_META_SIZE 8

#define NETFR_CREDIT_COUNT 60

#define NETFR_RESERVED_CREDIT_COUNT 8

enum
{
  NFR_LOG_LEVEL_TRACE,
  NFR_LOG_LEVEL_DEBUG,
  NFR_LOG_LEVEL_INFO,
  NFR_LOG_LEVEL_WARNING,
  NFR_LOG_LEVEL_ERROR,
  NFR_LOG_LEVEL_FATAL,
  NFR_LOG_LEVEL_OFF
};

#ifdef __cplusplus
}
#endif

#endif