// SPDX-License-Identifier: GPL-2.0-or-later
// NetFR Reference Implementation - Client Resource Allocator
// Copyright (c) 2023 Tim Dettmar

#include "tcm_conn.h"
#include "tcm_fabric.h"
#include "tcm_exception.h"

#include "nfr_client.hpp"
#include "nfr_protocol.h"
#include "nfr_util.hpp"

using std::make_shared;
using std::shared_ptr;

static void clientFrameChSetup(tcm_client_dynamic_param * p,
                               shared_ptr<tcm_endpoint> * ep_out,
                               fi_addr_t *                peer_out) {
  assert(p);
  assert(p->fabric_out);
  assert(p->peer_out != FI_ADDR_UNSPEC);
  assert(p->ep_out);
  assert(ep_out);
  assert(peer_out);

  std::shared_ptr<tcm_endpoint> ep = 0;
  sockaddr_storage              tmp;
  size_t                        tmp_size = sizeof(tmp);

  int  ret;
  auto mem = tcm_mem(p->fabric_out, tcm_get_page_size());

  // Get current endpoint details

  ret = p->ep_out->get_name(&tmp, &tmp_size);
  if (ret < 0)
    throw tcm_exception(-ret, __FILE__, __LINE__,
                        "Failed to get endpoint name for main channel");

  // Create new endpoint with dynamic port

  nfr_internal::setPort(SA_CAST(&tmp), 0);
  tcm_time timeout(p->timeout_ms, 1);
  ep = make_shared<tcm_endpoint>(p->fabric_out, SA_CAST(&tmp), &timeout);

  tmp_size = sizeof(tmp);
  ret      = ep->get_name(&tmp, &tmp_size);
  if (ret < 0)
    throw tcm_exception(-ret, __FILE__, __LINE__,
                        "Failed to get endpoint name for frame channel");

  // Send setup message

  NFRConnSetup * cs = reinterpret_cast<NFRConnSetup *>(*mem);
  NFRHeaderEncode(NFR_MSG_CONN_SETUP, cs);
  cs->direction  = 0;
  cs->framePort  = nfr_internal::getPort(SA_CAST(&tmp));
  cs->sysTimeout = p->timeout_ms;

  ret = p->ep_out->ssend(mem, p->peer_out, 0, sizeof(NFRConnSetup));
  if (ret < 0)
    throw tcm_exception(-ret, __FILE__, __LINE__,
                        "Failed to send connection setup message");

  // Wait for response

  ret = p->ep_out->srecv(mem, p->peer_out, 0, sizeof(NFRConnSetup));
  if (ret < 0)
    throw tcm_exception(-ret, __FILE__, __LINE__,
                        "Failed to receive connection setup message");

  // Add peer

  tmp_size = sizeof(tmp);
  ret      = p->fabric_out->lookup_peer(p->peer_out, SA_CAST(&tmp), &tmp_size);
  if (ret < 0)
    throw tcm_exception(-ret, __FILE__, __LINE__,
                        "Failed to look up fabric peer");

  if (memcmp(cs->header.magic, NETFR_MAGIC, 8) != 0 ||
      cs->header.type != NFR_MSG_CONN_SETUP || cs->direction != NFR_DIR_SERVER)
    throw tcm_exception(EPROTO, __FILE__, __LINE__,
                        "Peer sent invalid connection setup message");

  if (cs->framePort == 0)
    throw tcm_exception(EPROTO, __FILE__, __LINE__,
                        "Peer frame port unspecified");

  nfr_internal::setPort(SA_CAST(&tmp), cs->framePort);
  *peer_out = p->fabric_out->add_peer(SA_CAST(&tmp));
  if (*peer_out == FI_ADDR_UNSPEC)
    throw tcm_exception(errno, __FILE__, __LINE__,
                        "Could not register peer address");

  // Send response on frame channel

  ret = ep->ssend(mem, *peer_out, 0, sizeof(NETFR_MAGIC) - 1);
  if (ret < 0)
    throw tcm_exception(-ret, __FILE__, __LINE__,
                        "Could not send connection setup response");

  *ep_out = ep;
}

int NFRClientCreate(NFRClientOpts & opts, NFRClientResource & out) {
  try {
    if (!opts.build_ver)
      throw tcm_exception(EINVAL, __FILE__, __LINE__,
                          "Required build version string missing");

    ssize_t                     ret;
    std::vector<tcm_conn_hints> conn_h;
    sockaddr_in6                src, dst;

    size_t size = sizeof(src);
    ret         = tcm_internal::pton(opts.src_addr, 0, SA_CAST(&src), &size);
    if (ret < 0)
      throw tcm_exception(-ret, __FILE__, __LINE__,
                          "Failed to convert source address");
    size = sizeof(dst);
    ret =
        tcm_internal::pton(opts.dst_addr, opts.dst_port, SA_CAST(&dst), &size);
    if (ret < 0)
      throw tcm_exception(-ret, __FILE__, __LINE__,
                          "Failed to convert destination address");
    if (SA_CAST(&src)->sa_family != SA_CAST(&dst)->sa_family)
      throw tcm_exception(EINVAL, __FILE__, __LINE__,
                          "Mismatched address families");

    auto beacon = tcm_beacon();

    nfr_internal::FabricInfo hints;
    hints.setSrc(SA_CAST(&src));
    hints.setDst(SA_CAST(&dst));
    hints.setProvider(opts.transport);
    hints.fii->fabric_attr->api_version = opts.api_version;

    NFRPrvData nprv;
    memset(&nprv, 0, sizeof(nprv));
    memcpy(nprv.magic, NETFR_MAGIC, sizeof(nprv.magic));
    nfr_internal::copyFixedStr(nprv.build_ver, opts.build_ver, 32);

    tcm_prv_data prv;
    prv.data      = (void *) &nprv;
    prv.size      = sizeof(nprv);
    prv.validator = nfr_internal::fabricValidatePrv;
    prv.params    = strdup(opts.build_ver);
    if (opts.build_ver && !prv.params)
      throw tcm_exception(ENOMEM, __FILE__, __LINE__,
                          "Build version allocation failed");

    tcm_conn_hints h;
    h.addr  = (void *) &src;
    h.flags = 0;
    h.hints = hints.fii;
    conn_h.push_back(h);

    tcm_client_dynamic_param p;
    p.prv_data   = &prv;
    p.beacon     = &beacon;
    p.hints      = &conn_h;
    p.peer       = SA_CAST(&dst);
    p.timeout_ms = opts.timeout_ms;
    p.exit_flag  = 0;

    ret = tcm_client_dynamic(&p);
    free(prv.params);
    if (ret < 0) {
      return ret;
    }

    clientFrameChSetup(&p, &out.ep_frame, &out.peer_frame);
    out.fabric   = p.fabric_out;
    out.ep_msg   = p.ep_out;
    out.peer_msg = p.peer_out;
  } catch (tcm_exception & e) {
    std::string desc = e.full_desc();
    tcm__log_error("%s", desc.c_str());
    return -e.return_code();
  }
  return 0;
}