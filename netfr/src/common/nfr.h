#ifndef NETFR_PRIVATE_H
#define NETFR_PRIVATE_H

#include <stdio.h>

#include "netfr/netfr.h"
#include "common/nfr_constants.h"
#include "common/nfr_resource.h"
#include "common/nfr_mem.h"
#include "common/nfr_callback.h"

inline static int nfr_SetEnv(const char * name, const char * value, int overwrite)
{
#ifdef _WIN32
  if (!overwrite)
  {
    char tmp[128];
    if (GetEnvironmentVariable(name, tmp, sizeof(tmp)) > 0)
      return 0;
  }
  return SetEnvironmentVariable(name, value) > 0 ? 0 : -1;
#else
  return setenv(name, value, overwrite);
#endif
}

struct NFR_TransferWrite
{
  PNFRMemory                localMem;
  uint64_t                  localOffset;
  PNFRRemoteMemory          remoteMem;
  uint64_t                  remoteOffset;
  const struct NFR_CallbackInfo * writeCbInfo;
};

struct NFR_TransferInfo
{
  /*
    See NFR_OP_* in nfr_constants.h
  */
  uint8_t  opType;
  uint64_t length;
  /* Only used for copied sends */
  void     * data;
  /*  Context pointer.
      - Sends:        required
      - Copied Sends: ignored
      - Receives:     optional
      - Writes:       ignored
  */
  struct NFRFabricContext       * context;
  const struct NFR_CallbackInfo * cbInfo;
  struct NFR_TransferWrite writeOpts;
};

/**
 * @brief Lenient memcpy that checks for null pointers and zero length. Used
 *        internally for optional parameters where null can be expected, such as
 *        cbInfo.
 */
inline static void * nfr_MemCpyOptional(void * dest, const void * src, size_t n)
{
  if (!src || !dest || !n)
    return 0;
  return memcpy(dest, src, n);
}

ssize_t nfr_PostTransfer(struct NFRResource * res, struct NFR_TransferInfo * ti);

#endif