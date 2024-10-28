#ifndef NETFR_PRIVATE_CLIENT_H
#define NETFR_PRIVATE_CLIENT_H

#include <stdint.h>
#include <stdatomic.h>
#include <stdalign.h>

struct NFRClient;

struct NFRClientChannel
{
  // The lock must be held when accessing anything in this structure
  _Atomic(uint32_t)    lock;
  struct NFRClient   * parent;
  struct NFRResource * res;        // Fabric resource
  uint32_t             rxSerial;   // Used for all standard NetFR messages except RDMA writes
  uint32_t             txSerial;   // Used for all standard NetFR messages except RDMA writes
  uint32_t             memSerial;  // Used for RDMA write confirmations
};

struct NFRClient
{
  struct NFRClientChannel channels[NETFR_NUM_CHANNELS];
  struct NFRInitOpts peerInfo;
};


#endif