// SPDX-License-Identifier: GPL-2.0-or-later
// NetFR Reference Implementation - Server Resource Allocator
// Copyright (c) 2023 Tim Dettmar

#include "tcm_conn.h"
#include "tcm_fabric.h"

#include "nfr_server.hpp"
#include "nfr_util.hpp"

using std::make_shared;
using std::shared_ptr;

static void serverFrameChSetup(tcm_accept_client_dynamic_param * p,
                               shared_ptr<tcm_endpoint> *        ep_out,
                               fi_addr_t *                       peer_out) {
  assert(p);
  assert(p->fabric_out);
  assert(p->fabric_peer_out != FI_ADDR_UNSPEC);
  assert(p->ep_out);
  assert(ep_out);
  assert(peer_out);

  std::shared_ptr<tcm_endpoint> ep = 0;
  int                           ret;
  auto mem = tcm_mem(p->fabric_out, tcm_get_page_size());

  // Receive setup message from client

  ret = p->ep_out->srecv(mem, p->fabric_peer_out, 0, sizeof(NFRConnSetup));
  if (ret < 0)
    throw tcm_exception(-ret, __FILE__, __LINE__, "");

  NFRConnSetup * cs = reinterpret_cast<NFRConnSetup *>(*mem);
  if (memcmp(cs->header.magic, NETFR_MAGIC, 8) != 0 ||
      cs->header.type != NFR_MSG_CONN_SETUP || cs->direction != NFR_DIR_CLIENT)
    throw tcm_exception(EPROTO, __FILE__, __LINE__,
                        "Peer sent invalid connection setup message");

  // Add the client-specified port as a new peer and create a new endpoint

  sockaddr_storage tmp;
  size_t           tmp_size = sizeof(tmp);
  ret =
      p->fabric_out->lookup_peer(p->fabric_peer_out, SA_CAST(&tmp), &tmp_size);
  if (ret < 0)
    throw tcm_exception(-ret, __FILE__, __LINE__, "Peer address lookup failed");

  if (nfr_internal::getPort(SA_CAST(&tmp)) == 0)
    throw tcm_exception(EINVAL, __FILE__, __LINE__, "Peer port number invalid");
  if (cs->framePort == 0)
    throw tcm_exception(EPROTO, __FILE__, __LINE__,
                        "Peer did not specify port number");

  nfr_internal::setPort(SA_CAST(&tmp), cs->framePort);

  *peer_out = p->fabric_out->add_peer(SA_CAST(&tmp));
  if (*peer_out == FI_ADDR_UNSPEC)
    throw tcm_exception(EADDRNOTAVAIL, __FILE__, __LINE__,
                        "Failed to add peer address");

  tmp_size = sizeof(tmp);
  ret      = p->ep_out->get_name(&tmp, &tmp_size);
  if (ret < 0)
    throw tcm_exception(-ret, __FILE__, __LINE__,
                        "Failed to get peer address on frame channel");

  nfr_internal::setPort(SA_CAST(&tmp), 0);

  tcm_time timeout(p->timeout_ms, 1);
  ep = make_shared<tcm_endpoint>(p->fabric_out, SA_CAST(&tmp), &timeout);

  tmp_size = sizeof(tmp);
  ret      = ep->get_name(&tmp, &tmp_size);
  if (ret < 0)
    throw tcm_exception(-ret, __FILE__, __LINE__,
                        "Failed to get local frame channel address");

  // Reply to the client with own port number

  cs->direction  = 1;
  cs->framePort  = nfr_internal::getPort(SA_CAST(&tmp));
  cs->sysTimeout = p->timeout_ms;

  ret = p->ep_out->ssend(mem, p->fabric_peer_out, 0, sizeof(NFRConnSetup));
  if (ret < 0)
    throw tcm_exception(-ret, __FILE__, __LINE__,
                        "Failed to send connection setup message");

  // Wait for response on frame channel

  ret = ep->srecv(mem, *peer_out, 0, sizeof(NETFR_MAGIC) - 1);
  if (ret < 0)
    throw tcm_exception(-ret, __FILE__, __LINE__,
                        "Failed to receive connection setup message");
  if (ret != sizeof(NETFR_MAGIC) - 1)
    throw tcm_exception(EPROTO, __FILE__, __LINE__,
                        "Peer sent invalid confirmation message (magic)");
  if (memcmp(*mem, NETFR_MAGIC, sizeof(NETFR_MAGIC) - 1) != 0)
    throw tcm_exception(EPROTO, __FILE__, __LINE__,
                        "Peer sent invalid confirmation message (data)");

  *ep_out = ep;
}

int NFRServerCreate(NFRServerOpts & opts, NFRServerResource & out) noexcept {
  try {
    if (!opts.build_ver)
      throw tcm_exception(EINVAL, __FILE__, __LINE__,
                          "Required build version string missing");

    ssize_t                     ret;
    std::vector<tcm_conn_hints> conn_h;
    sockaddr_in6                src;

    size_t size = sizeof(src);
    ret =
        tcm_internal::pton(opts.src_addr, opts.src_port, SA_CAST(&src), &size);
    if (ret < 0)
      throw tcm_exception(-ret, __FILE__, __LINE__,
                          "Failed to convert source address");

    auto beacon = tcm_beacon(SA_CAST(&src));
    nfr_internal::FabricInfo hints;
    nfr_internal::setPort(SA_CAST(&src), opts.data_port);
    hints.setSrc(SA_CAST(&src));
    hints.setProvider(opts.transport);
    hints.fii->fabric_attr->api_version = opts.api_version;

    NFRPrvData nprv;
    memset(&nprv, 0, sizeof(nprv));
    memcpy(nprv.magic, NETFR_MAGIC, sizeof(nprv.magic));
    nfr_internal::copyFixedStr(nprv.build_ver, opts.build_ver,
                               sizeof(nprv.build_ver));

    tcm_prv_data prv;
    prv.data      = (void *) &nprv;
    prv.size      = sizeof(nprv);
    prv.validator = nfr_internal::fabricValidatePrv;
    prv.params    = strdup(opts.build_ver);
    if (opts.build_ver && !prv.params)
      return -ENOMEM;

    tcm_conn_hints h;
    h.addr  = (void *) &src;
    h.flags = 0;
    h.hints = hints.fii;
    conn_h.push_back(h);

    tcm_accept_client_dynamic_param p;
    p.clear();
    p.prv_data   = &prv;
    p.beacon     = &beacon;
    p.hints      = &conn_h;
    p.timeout_ms = -1;

    ret = tcm_accept_client_dynamic(&p);
    free(prv.params);
    if (ret < 0)
      return ret;

    serverFrameChSetup(&p, &out.ep_frame, &out.peer_frame);
    out.fabric   = p.fabric_out;
    out.ep_msg   = p.ep_out;
    out.peer_msg = p.fabric_peer_out;
  } catch (tcm_exception & e) {
    std::string desc = e.full_desc();
    tcm__log_error("%s", desc.c_str());
    return -e.return_code();
  }
  return 0;
}