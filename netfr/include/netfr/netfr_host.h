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
#ifndef NETFR_HOST_H
#define NETFR_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "netfr/netfr.h"

/**
 * @brief Read a message from the host if available
 * 
 * @param host          Host handle
 * 
 * @param channelID     Channel index
 * 
 * @param data          Buffer where the data will be copied
 * 
 * @param maxLength     Length of the data buffer. The actual size of the
 *                      message will be stored here. If the message is larger
 *                      than the buffer, the function will return -ENOBUFS,
 *                      but maxLength will still be set.
 * 
 * @param udata         User data associated with the message
 * 
 * @return              0 on success, negative error code on failure
 */
int nfrHostReadData(struct NFRHost * host, int channelID, void * data,
                    uint32_t * maxLength, uint64_t * udata);

/**
 * @brief Send data to the client
 *
 * The maximum length of data which can be sent using this function is
 * defined as NETFR_MESSAGE_MAX_PAYLOAD_SIZE.
 *
 * @param host          Host handle
 *
 * @param channelID     Channel index
 *
 * @param data          Data buffer
 *
 * @param length        Length of the data buffer
 *
 * @return              0 on success, negative error code on failure                     
 */
int nfrHostSendData(PNFRHost host, int channelID, const void * data, 
                    uint32_t length, uint64_t udata);

/**
 * @brief Perform background processing tasks. 
 *
 * This function must be called as frequently as possible, as it will affect the
 * latency of the entire system. When used for cursor and frame data, this
 * function should be called every millisecond.
 *
 * @param host      Host handle
 *
 * @return          0 on success, negative error code on failure   
 * 
 *                  If no clients are currently connected, this function will
 *                  return ``-ENOTCONN``.
 */
int nfrHostProcess(PNFRHost host);

/**
 * @brief Initialize the necessary resources for a NetFR host.
 *
 * After calling this function, the host will be ready to accept connections
 * from clients.
 *
 * @param opts      Fabric initialization options
 * 
 * @param result    Host handle
 * 
 * @return          0 on success, negative error code on failure
 */
int nfrHostInit(const struct NFRInitOpts * opts, PNFRHost * result);

/**
 * @brief Check whether clients are connected to the host
 *
 * @param host  Host handle
 * 
 * @param index Channel index
 *
 * @return      The number of connected clients. Note that this version of NetFR
 *              only supports one client.
 */
int nfrHostClientsConnected(PNFRHost host, int index);

/**
 * @brief Perform an RDMA write operation to a suitable remote memory region.
 *
 * This function will copy the data from the referenced local memory buffer to
 * the smallest suitable remote memory buffer via a one-sided RDMA write
 * operation, and signal completion to the remote side automatically. To receive
 * completion notifications locally, the callback function in the callback info
 * structure must be set.
 *
 * @param localMem 
 * 
 * @param localOffset 
 * 
 * @param remoteOffset 
 * 
 * @param length 
 * 
 * @param cbInfo
 * 
 * @return int 
 */
int nfrHostWriteBuffer(PNFRMemory localMem, uint64_t localOffset,
                       uint64_t remoteOffset, uint64_t length,
                       struct NFRCallbackInfo * cbInfo);

/**
 * @brief Attach an existing memory buffer to a fabric resource.
 *
 * @warning This function does not support the use of DMABUFs or GPU memory
 *          regions, except when the DMABUF page mappings are stable and reside
 *          in host memory (e.g., KVMFR memory).
 *
 * @note For optimal performance, the memory region should be page-aligned. If
 *       huge pages are used, the memory region should be aligned to the huge
 *       page size, and the environment variable `RDMAV_HUGEPAGES_SAFE` must be
 *       set to `1`.
 *
 * @param host    Host handle
 * 
 * @param buffer  Memory buffer
 * 
 * @param size    Size of the memory buffer
 * 
 * @param index   The channel index to make the memory region available on
 */
PNFRMemory nfrHostAttachMemory(PNFRHost host, void * buffer,
                               uint64_t size, uint8_t index);


void nfrHostFree(PNFRHost * res);

#ifdef __cplusplus
}
#endif

#endif