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
  uint32_t                  rxSerial;
  uint32_t                  txSerial;
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