// SPDX-License-Identifier: GPL-2.0-or-later
// NetFR Reference Implementation - Internal Utilities
// Copyright (c) 2023 Tim Dettmar

#ifndef NFR_INTERNAL_UTIL_HPP_
#define NFR_INTERNAL_UTIL_HPP_

#include "tcm_conn.h"

#include "nfr_protocol.h"

#define SA_CAST(x) reinterpret_cast<sockaddr *>(x)
#define FILENAME(x) strrchr("/" __FILE__, '/')

namespace nfr_internal {

uint16_t getPort(sockaddr * sa);
void     setPort(sockaddr * sa, uint16_t port);
int      fabricValidatePrv(tcm_prv_data * self, void * data, size_t size);

/* Ephemeral Libfabric fi_info wrapper. This essentially allows the fi_info
   struct to be treated like a stack variable (fi_info structs must be allocated
   by libfabric functions, which place them on the heap), when it only needs to
   be used to initialize something and then can be immediately discarded. */
class FabricInfo {

  void copyAddr(sockaddr * addr, void ** dest, size_t * dest_size);

public:
  fi_info * fii;

  FabricInfo();
  FabricInfo(fi_info * info);

  void setSrc(sockaddr * addr);
  void setDst(sockaddr * addr);
  void setProvider(const char * prov);

  ~FabricInfo();
};

/* Copy a string into a fixed-size buffer, filling unused bytes with 0 and
 * always null-terminating dst, even if the string would be truncated. */
static inline void copyFixedStr(char * dst, const char * src, size_t n) {
  assert(dst);
  assert(src);
  assert(n);
  strncpy(dst, src, n - 1);
  dst[n - 1] = '\0';
}

} // namespace nfr_internal

static inline void NFRHeaderEncode(uint8_t type, void * out) {
  struct NFRHeader * h = (struct NFRHeader *) out;
  memcpy(h->magic, NETFR_MAGIC, sizeof(h->magic));
  memset(h->_pad, 0, sizeof(h->_pad));
  h->type = type;
}

static inline struct NFRHeader NFRHeaderCreate(uint8_t type) {
  struct NFRHeader h;
  NFRHeaderEncode(type, &h);
  return h;
}

static inline bool nfrHeaderVerify(void * buffer, uint32_t size) {
  if (!buffer)
    return false;
  if (!size)
    return false;
  struct NFRHeader * hdr = (NFRHeader *) buffer;
  if (memcmp(hdr->magic, NETFR_MAGIC, 8) != 0)
    return false;
  if (hdr->type <= NFR_MSG_INVALID || hdr->type >= NFR_MSG_MAX)
    return false;
  if (hdr->_pad[0] != 0 || hdr->_pad[1] != 0 || hdr->_pad[2] != 0)
    return false;
  return true;
}

#endif // NFR_INTERNAL_UTIL_HPP_