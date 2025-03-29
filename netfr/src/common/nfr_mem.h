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

#ifndef NETFR_PRIVATE_MEM_H
#define NETFR_PRIVATE_MEM_H

#ifdef _WIN32
  #include <sysinfoapi.h>
#else
  #include <unistd.h>
#endif

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __linux__
  #include <linux/udmabuf.h>
  #include <sys/types.h>
  #include <sys/ioctl.h>
  #include <sys/mman.h>
  #include <fcntl.h>
#else
  #ifdef NETFR_ENABLE_DMABUF_REGISTRATION
    #error "DMABUF registration is not supported on this platform"
  #endif
#endif

#include "netfr/netfr.h"
#include "common/nfr_resource.h"
#include "common/nfr_resource_types.h"
#include "common/nfr_log.h"

/**
 * @brief Allocate an aligned memory buffer.
 * 
 * @param size        Size of the memory buffer
 * 
 * @param alignment   Alignment of the memory buffer
 * 
 * @return            Pointer to the allocated memory buffer, or 0 on failure 
 */
void * nfr_MemAllocAlign(uint64_t size, uint64_t alignment);

/**
 * @brief Free a memory buffer allocated with nfr_MemAllocAlign.
 *
 * For portability, this function should **always** be used to free memory
 * buffers allocated with nfr_MemAllocAlign. Specifically, on Windows,
 * _aligned_free must be used for aligned memory. On other platforms, the
 * standard free function is used.
 *
 * @param ptr Pointer to the memory buffer to free
 */
void nfr_MemFreeAlign(void * ptr);

/**
 * @brief Attach an existing memory buffer to a fabric resource.
 * 
 * This function does not support the use of DMABUFs, except when the DMABUF
 * page mappings are stable and reside in host memory. In other words, KVMFR
 * memory regions are supported, but not GPU memory regions.
 * 
 * @param res   Fabric resource to attach memory to
 * @param addr  Address of the memory buffer
 * @param size  Size of the memory buffer
 * @param acs   Access control flags
 * @param externalMem  Whether the memory buffer is externally allocated. This
 *                     flag is used to determine whether the memory buffer
 *                     should be freed when the memory region is closed.
 * @param initialState Initial usability state of the memory region
 * 
 * @return PNFRMemory handle on success, 0 on failure
 */
PNFRMemory nfr_RdmaAttach(struct NFRResource * res, void * addr, uint64_t size,
                          uint64_t acs, uint8_t externalMem,
                          uint8_t initialState);
                           

/**
 * @brief Allocate a pinned memory region for use with DMABUF and RDMA.
 * 
 * @param res   Fabric resource to attach memory to
 * @param size  Address of the memory buffer
 * @param acs   Access control flags
 * 
 * @return PNFRMemory handle on success, 0 on failure
 */
PNFRMemory nfr_RdmaAllocDMABUF(struct NFRResource * res, uint64_t size,
                               uint64_t acs);

/**
 * @brief Allocate RDMA-enabled host memory. Registration is handled internally.
 * 
 * For efficiency, this function will round up the memory allocation size to the
 * system page size. This fact will be reflected in the size field of the memory
 * region.
 * 
 * @param res   Fabric resource to attach memory to
 * 
 * @param size  Size of the memory region
 * 
 * @param acs   Access control flags
 * 
 * @param initialState Initial usability state of the memory region
 * 
 * @return PNFRMemory 
 */
inline static PNFRMemory nfr_RdmaAlloc(struct NFRResource * res, uint64_t size,
                                       uint64_t acs, uint8_t initialState)
{
  return nfr_RdmaAttach(res, 0, size, acs, 0, initialState);
}

/**
 * @brief Get the system page size.
 * 
 * @return The system page size in bytes
 */
inline static uint64_t nfr_GetPageSize(void)
{
#ifdef _WIN32
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return si.dwPageSize;
#else
  return sysconf(_SC_PAGESIZE);
#endif
}

#endif