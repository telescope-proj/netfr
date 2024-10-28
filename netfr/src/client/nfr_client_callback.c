#include "client/nfr_client_callback.h"

#include "common/nfr_protocol.h"

void nfr_ClientProcessInternalTx(struct NFRFabricContext * ctx)
{
  ASSERT_CONTEXT_VALID(ctx);
  
  struct NFRHeader * hdr = (struct NFRHeader *) ctx->slot->data;
  NFR_LOG_DEBUG("Processing txctx %p -> type %d", ctx, hdr->type);
  
  assert(ctx->state == CTX_STATE_WAITING || ctx->state == CTX_STATE_ACK_ONLY);
  NFR_RESET_CONTEXT(ctx);
}

void nfr_ClientProcessInternalRx(struct NFRFabricContext * ctx)
{
  NFR_LOG_DEBUG("Processing rxctx %p", ctx);
  ASSERT_CONTEXT_VALID(ctx);
  
  NFR_CAST_UDATA(struct NFRClientChannel *, chan, ctx, 0);
  
  struct NFRClient * client = chan->parent;
  assert(client);
  assert(chan);

  struct NFRHeader * hdr = (struct NFRHeader *) ctx->slot->data;
  if (memcmp(hdr->magic, NETFR_MAGIC, 8) != 0 || hdr->version != NETFR_VERSION)
  {
    assert(!"Invalid message header");
    NFR_RESET_CONTEXT(ctx);
    return;
  }

  switch (hdr->type)
  {
    case NFR_MSG_BUFFER_UPDATE:
    {
      struct NFRMsgBufferUpdate * update = (struct NFRMsgBufferUpdate *) hdr;
      if (update->bufferIndex >= NETFR_MAX_MEM_REGIONS)
      {
        assert(!"Invalid buffer index");
        NFR_RESET_CONTEXT(ctx);
        return;
      }
      struct NFRMemory * mem = chan->res->memRegions + update->bufferIndex;
      if (update->payloadOffset + update->payloadSize > mem->size)
      {
        assert(!"Invalid buffer update");
        NFR_RESET_CONTEXT(ctx);
        return;
      }
      mem->state         = MEM_STATE_HAS_DATA;
      mem->payloadOffset = update->payloadOffset;
      mem->payloadLength = update->payloadSize;
      mem->serial        = chan->memSerial++;
      NFR_RESET_CONTEXT(ctx);
      return;
    }
    case NFR_MSG_HOST_DATA:
    {
      ctx->state = CTX_STATE_HAS_DATA;
      ctx->slot->serial = chan->rxSerial++;
      return;
    }
    case NFR_MSG_CLIENT_DATA_ACK:
    {
      NFR_RESET_CONTEXT(ctx);
      ++chan->res->txCredits;
      return;
    }
    default:
    {
      assert(!"Invalid message type");
      return;
    }
  }
}