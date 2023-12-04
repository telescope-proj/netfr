// SPDX-License-Identifier: GPL-2.0-or-later
// NetFR Reference Implementation - Server Resource Allocator
// Copyright (c) 2023 Tim Dettmar

#include "nfr_protocol.h"
#include "tcm_fabric.h"

struct NFRServerOpts {
  const char * src_addr;    // Source address (beacon + fabric)
  const char * src_port;    // Source port (beacon)
  const char * transport;   // Libfabric transport name
  const char * build_ver;   // User-specified build version
  uint32_t     api_version; // Libfabric API version
  int          timeout_ms;
};

struct NFRServerResource {
  std::shared_ptr<tcm_fabric>   fabric;
  std::shared_ptr<tcm_endpoint> ep_frame;
  std::shared_ptr<tcm_endpoint> ep_msg;
  fi_addr_t                     peer_frame;
  fi_addr_t                     peer_msg;
};

int NFRServerCreate(NFRServerOpts & opts, NFRServerResource & out) noexcept;