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

#ifndef NETFR_PRIVATE_HOST_INT_H
#define NETFR_PRIVATE_HOST_INT_H

#include <stdint.h>
#include <stdatomic.h>
#include <stdalign.h>

#include "netfr/netfr_host.h"
#include "netfr/netfr_constants.h"
#include "common/nfr_resource.h"

struct NFRHostChannel
{
  // The lock must be held when accessing anything in this structure
  _Atomic(uint32_t)         lock;
  uint32_t                  msgSerial;
  uint32_t                  writeSerial;
  uint32_t                  channelSerial;
  struct NFRHost          * parent;
  struct NFRResource      * res;
  struct NFRMemory        * mem;
  struct NFRRemoteMemory    clientRegions[NETFR_MAX_MEM_REGIONS];
};

struct NFRHost
{
  struct NFRHostChannel channels[NETFR_NUM_CHANNELS];
};

#endif