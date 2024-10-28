#ifndef NETFR_PRIVATE_CLIENT_CALLBACK_H
#define NETFR_PRIVATE_CLIENT_CALLBACK_H

#include "common/nfr_constants.h"
#include "common/nfr_resource.h"
#include "common/nfr_protocol.h"

#include "netfr/netfr_client.h"
#include "netfr/netfr_constants.h"

#include "client/nfr_client.h"

void nfr_ClientProcessInternalTx(struct NFRFabricContext * ctx);

void nfr_ClientProcessInternalRx(struct NFRFabricContext * ctx);

#endif