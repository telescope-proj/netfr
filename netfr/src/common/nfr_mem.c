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
                          uint64_t acs, uint8_t externalMem,
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
    errno = externalMem ? ENOSPC : ENOMEM;
    return NULL;
  }

  mem->parentResource = res;
  mem->size           = size;
  mem->extMem         = externalMem;

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
  if (!externalMem)
    nfr_MemFreeAlign(mem->addr);
free_mem_struct:
  if (externalMem)
    free(mem);
  return NULL;
}

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