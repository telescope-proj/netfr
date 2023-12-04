// SPDX-License-Identifier: GPL-2.0-or-later
// NetFR Reference Implementation - Internal Utilities
// Copyright (c) 2023 Tim Dettmar

#include "nfr_util.hpp"

namespace nfr_internal {

uint16_t getPort(sockaddr * sa) {
  switch (sa->sa_family) {
    case AF_INET: return reinterpret_cast<sockaddr_in *>(sa)->sin_port; break;
    case AF_INET6:
      return reinterpret_cast<sockaddr_in6 *>(sa)->sin6_port;
      break;
    default:
      printf("Unsupported address format %d", sa->sa_family);
      assert(false);
  }
}

void setPort(sockaddr * sa, uint16_t port) {
  switch (sa->sa_family) {
    case AF_INET: reinterpret_cast<sockaddr_in *>(sa)->sin_port = port; break;
    case AF_INET6:
      reinterpret_cast<sockaddr_in6 *>(sa)->sin6_port = port;
      break;
    default:
      printf("Unsupported address format %d", sa->sa_family);
      assert(false);
  }
}

int fabricValidatePrv(tcm_prv_data * self, void * data, size_t size) {
  if (size < sizeof(NFRPrvData))
    return TCM_PRV_INVALID;

  NFRPrvData * prv = (NFRPrvData *) data;
  if (memcmp(prv->magic, NETFR_MAGIC, 8) != 0)
    return TCM_PRV_INVALID;
  if (strncmp(prv->build_ver, static_cast<const char *>(self->params), 32) != 0)
    return TCM_PRV_INVALID_WITH_RESP;
  return TCM_PRV_VALID;
}

void FabricInfo::copyAddr(sockaddr * addr, void ** dest, size_t * dest_size) {
  assert(dest);
  assert(dest_size);
  if (*dest)
    free(*dest);
  *dest = 0;
  if (!addr)
    throw tcm_exception(EINVAL, __FILE__, __LINE__, "Address parameter empty");
  int sa_size = tcm_internal::get_sa_size(addr);
  if (sa_size <= 0)
    throw tcm_exception(EINVAL, __FILE__, __LINE__,
                        "Unsupported sockaddr format");
  uint32_t af = tcm_internal::sys_to_fabric_af(addr->sa_family);
  if (af == FI_FORMAT_UNSPEC)
    throw tcm_exception(EINVAL, __FILE__, __LINE__,
                        "Unsupported fabric address format");
  if (fii->addr_format != FI_FORMAT_UNSPEC && fii->addr_format != af)
    throw tcm_exception(EINVAL, __FILE__, __LINE__,
                        "Mismatched address format");
  fii->addr_format = af;
  *dest            = calloc(1, sa_size);
  if (!*dest)
    throw tcm_exception(ENOMEM, __FILE__, __LINE__,
                        "Failed to allocate destination address");
  memcpy(*dest, addr, sa_size);
  *dest_size = sa_size;
}

FabricInfo::FabricInfo() {
  fii = fi_allocinfo();
  if (!fii)
    throw ENOMEM;
}

FabricInfo::FabricInfo(fi_info * info) { fii = info; }

FabricInfo::~FabricInfo() {
  if (fii)
    fi_freeinfo(fii);
  fii = 0;
}

void FabricInfo::setSrc(sockaddr * addr) {
  copyAddr(addr, &fii->src_addr, &fii->src_addrlen);
}

void FabricInfo::setDst(sockaddr * addr) {
  copyAddr(addr, &fii->dest_addr, &fii->dest_addrlen);
}

void FabricInfo::setProvider(const char * prov) {
  if (fii->fabric_attr->prov_name)
    free(fii->fabric_attr->prov_name);
  if (prov)
    fii->fabric_attr->prov_name = strdup(prov);
  else
    fii->fabric_attr->prov_name = 0;
  if (prov && !fii->fabric_attr->prov_name)
    throw ENOMEM;
}

} // namespace nfr_internal