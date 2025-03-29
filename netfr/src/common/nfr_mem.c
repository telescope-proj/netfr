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

#include "common/nfr_mem.h"

void * nfr_MemAllocAlign(uint64_t size, uint64_t alignment)
{
#ifdef _WIN32
  return _aligned_malloc(size, alignment);
#else
  return aligned_alloc(alignment, size);
#endif
}

void nfr_MemFreeAlign(void * ptr)
{
#ifdef _WIN32
  _aligned_free(ptr);
#else
  free(ptr);
#endif
}

PNFRMemory nfr_RdmaAttach(struct NFRResource * res, void * addr, uint64_t size,
                          uint64_t acs, uint8_t memType,
                          uint8_t initialState)
{
  assert(res);
  assert(size > 0);

  struct NFRMemory * mem = 0;

  for (int i = 0; i < NETFR_MAX_MEM_REGIONS; ++i)
  {
    if (res->memRegions[i].state == MEM_STATE_EMPTY)
    {
      NFR_LOG_DEBUG("Allocating memory region %d from resource %p", i, res);
      assert(!res->memRegions[i].addr);
      mem = res->memRegions + i;
      break;
    }
  }

  if (!mem)
  {
    assert(!"Memory region limit reached");
    errno = memType ? ENOSPC : ENOMEM;
    return NULL;
  }

  mem->parentResource = res;
  mem->size           = size;
  mem->memType        = memType;

  if (!addr)
  {
    uint64_t ps = nfr_GetPageSize();
    mem->addr = nfr_MemAllocAlign(mem->size, ps);
    if (!mem->addr)
    {
      NFR_LOG_DEBUG("Failed to allocate aligned memory");
      errno = ENOMEM;
      goto free_mem_struct;
    }
  }
  else
  {
    mem->addr = addr;
  }

  ssize_t ret;
  ret = fi_mr_reg(res->domain, mem->addr, mem->size, acs, 0, 0, 0, &mem->mr,
                  mem);
  if (ret == -FI_ENOKEY)
  {
    for (int i = 0; i < 8; ++i)
    {
      ret = fi_mr_reg(res->domain, mem->addr, mem->size, acs, 0,
                      ++res->rkeyCounter, 0, &mem->mr, res);
      if (ret == 0)
        break;
      if (ret != -FI_ENOKEY)
        goto free_mem_aligned;
    }
  }

  if (ret == 0)
  {
    NFR_LOG_DEBUG("Registered %lu byte memory %p with key %lu", mem->size,
                  mem->addr, fi_mr_key(mem->mr));
    mem->state = initialState;
    return mem;
  }

free_mem_aligned:
  if (nfr_MemIsExternal(memType))
    nfr_MemFreeAlign(mem->addr);
free_mem_struct:
  if (nfr_MemIsExternal(memType))
    free(mem);
  return NULL;
}

#ifdef __linux__

/* This is to support externally allocated DMABUFs in the future, but it's not
 * known if this works or not yet. Of course, it would be nice if we could just
 * DMA directly from host GPU to client GPU. Bye latency! */
PNFRMemory nfr_RdmaAttachDMABUF(struct NFRResource * res, void * buf, 
                                uint64_t size, int fd)
{
#ifdef NETFR_ENABLE_DMABUF_REGISTRATION
  uint32_t ver = fi_version();
  if (ver >= FI_VERSION(1, 20))
  {
    struct fi_mr_dmabuf dmaAttr = {0};
    dmaAttr.fd        = dmaFd;
    dmaAttr.offset    = 0;
    dmaAttr.len       = size;
    dmaAttr.base_addr = addr;

    struct fi_mr_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.dmabuf         = &dmaAttr;
    attr.iov_count      = 1;
    attr.access         = acs;
    attr.offset         = 0;
    attr.requested_key  = 0;
    attr.context        = res;
    attr.auth_key       = 0;
    attr.auth_key_size  = 0;
    attr.iface          = FI_HMEM_SYSTEM;
    attr.hmem_data      = 0;
    
    int ret = fi_mr_regattr(res->domain, &attr, FI_MR_DMABUF, &mem->mr);
    if (ret < 0)
    {
      NFR_LOG_DEBUG("Failed to register DMABUF: %s (%d)", fi_strerror(-ret), ret);
      return ret;
    }
  }
  else
  {
    NFR_LOG_ERROR("Libfabric %d.%d does not support DMABUF registrations, "
                  "version 1.20 or later is required",
                  FI_MAJOR(ver), FI_MINOR(ver));
    return -ENOSYS;
  }
#else
  return -ENOSYS;
#endif
}

/**
 * @brief Allocate host memory as a DMABUF usable by other devices.
 * 
 * @note  This function is only available on Linux, and requires the kernel
 *        to have support for the udmabuf driver.
 * 
 * @param res   Resource to perform memory registration against
 * 
 * @param size  Size of the memory region
 * 
 * @param acs   RDMA access flags
 * 
 * @return      Nonzero on success, 0 on failure. errno is set on failure.
 */
PNFRMemory nfr_RdmaAllocDMABUF(struct NFRResource * res, uint64_t size,
                               uint64_t acs)
{
  PNFRMemory mem = 0;
  for (int i = 0; i < NETFR_MAX_MEM_REGIONS; ++i)
  {
    if (res->memRegions[i].state == MEM_STATE_EMPTY)
    {
      NFR_LOG_DEBUG("Allocating memory region %d from resource %p", i, res);
      assert(!res->memRegions[i].addr);
      mem = res->memRegions + i;
      mem->state = MEM_STATE_INVALID;
      break;
    }
  }

  if (!mem)
  {
    assert(!"Memory region limit reached");
    errno = ENOSPC;
    return NULL;
  }

  int fd = memfd_create("netfr-dmabuf", MFD_CLOEXEC);
  if (fd < 0)
  {
    NFR_LOG_DEBUG("Failed to create memfd: %s (%d)", strerror(errno), errno);
    goto free_slot;
  }

  if (ftruncate(fd, size) < 0)
  {
    NFR_LOG_DEBUG("Failed to truncate memfd: %s (%d)", strerror(errno), errno);
    goto close_memfd;
  }

  if (fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK) < 0)
  {
    NFR_LOG_DEBUG("Failed to seal memfd: %s (%d)", strerror(errno), errno);
    goto close_memfd;
  }

  int udmaFd = open("/dev/udmabuf", O_RDWR);
  if (udmaFd < 0)
  {
    NFR_LOG_DEBUG("Failed to open /dev/udmabuf: %s (%d)", strerror(errno), errno);
    goto close_memfd;
  }

  struct udmabuf_create dmaBufAttr = {0};
  dmaBufAttr.memfd  = fd;
  dmaBufAttr.flags  = UDMABUF_FLAGS_CLOEXEC;
  dmaBufAttr.offset = 0;
  dmaBufAttr.size   = size;

  int dmaFd = ioctl(udmaFd, UDMABUF_CREATE, &dmaBufAttr);
  close(udmaFd);
  if (dmaFd < 0)
  {
    NFR_LOG_DEBUG("Failed to create udmabuf: %s (%d)", strerror(errno), errno);
    goto close_memfd;
  }

  void * addr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (addr == MAP_FAILED)
  {
    NFR_LOG_DEBUG("Failed to map memfd: %s (%d)", strerror(errno), errno);
    goto close_dmafd;
  }

  int ret = mlock(addr, size);
  if (ret < 0)
  {
    NFR_LOG_DEBUG("Failed to lock memory: %s (%d)", strerror(errno), errno);
    goto unmap_memfd;
  }

  ret = fi_mr_reg(res->domain, addr, size, acs, 0, 0, 0, &mem->mr, res);
  if (ret < 0)
  {
    if (ret == -FI_ENOKEY)
    {
      for (int i = 0; i < 32; ++i)
      {
        ret = fi_mr_reg(res->domain, addr, size, acs, 0, ++res->rkeyCounter,
                        0, &mem->mr, res);
        if (ret == -FI_ENOKEY)
          continue;
        break;
      }

      if (ret < 0)
      {
        NFR_LOG_DEBUG("Failed to register memory: %s (%d)", fi_strerror(-ret),
                      ret);
        errno = -ret;
        goto munlock_memfd;
      }
    }

    NFR_LOG_ERROR("Failed to register memory: %s (%d)", fi_strerror(-ret), ret);
  }

  mem->addr    = addr;
  mem->dmaFd   = dmaFd;
  mem->size    = size;
  mem->state   = MEM_STATE_AVAILABLE_UNSYNCED;
  mem->memType = NFR_MEM_TYPE_SYSTEM_MANAGED_DMABUF;
  return mem;

munlock_memfd:
  munlock(addr, size);
unmap_memfd:
  munmap(addr, size);
close_dmafd:
  close(dmaFd);
close_memfd:
  close(fd);
free_slot:
  mem->state = MEM_STATE_EMPTY;
  return NULL;
}
#else
PNFRMemory nfrRdmaAllocDMABUF(struct NFRResource * res, uint64_t size,
                              uint64_t acs)
{
  NFR_LOG_ERROR("DMABUFs not supported on this platform");
  return NULL;
}
#endif

void nfrAckBuffer(PNFRMemory mem)
{
  assert(mem);

  if (!mem)
    return;

  if (mem->state <= MEM_STATE_EMPTY)
  {
    assert(!"Unexpected memory state");
    return;
  }

  mem->state = MEM_STATE_AVAILABLE_UNSYNCED;
}