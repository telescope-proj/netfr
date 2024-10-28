#ifndef NETFR_PRIVATE_CALLBACK_H
#define NETFR_PRIVATE_CALLBACK_H

#define NFR_CAST_UDATA(type, name, ctx, idx) \
  assert(ctx->cbInfo.uData[idx]); assert(idx < NFR_INTERNAL_CB_UDATA_COUNT); type name = ((type) ctx->cbInfo.uData[idx])

#define NFR_CAST_UDATA_NUM(type, name, ctx, idx) \
   assert(idx < NFR_INTERNAL_CB_UDATA_COUNT); type name = ((type) (uintptr_t) ctx->cbInfo.uData[idx])

#endif