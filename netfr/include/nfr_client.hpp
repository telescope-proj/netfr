// SPDX-License-Identifier: GPL-2.0-or-later
// NetFR Reference Implementation - Client Resource Allocator
// Copyright (c) 2023 Tim Dettmar

#include "tcm_fabric.h"

struct NFRClientOpts {
  const char *   dst_addr;    // Destination address (beacon)
  const char *   dst_port;    // Destination port (beacon)
  const char *   src_addr;    // Source address (fabric)
  const char *   transport;   // Libfabric transport name
  const char *   build_ver;   // User-specified build version
  uint16_t       data_port;   // General data port number (big endian)
  uint16_t       frame_port;  // Frame data port number (big endian)
  uint32_t       api_version; // Libfabric API version
  int            timeout_ms;  // Timeout in milliseconds
  int            interval_us; // Polling interval in microseconds
  volatile int * exit_flag;   // Termination flag
};

struct NFRClientResource {
  std::shared_ptr<tcm_fabric>   fabric;
  std::shared_ptr<tcm_endpoint> ep_frame;
  std::shared_ptr<tcm_endpoint> ep_msg;
  fi_addr_t                     peer_frame;
  fi_addr_t                     peer_msg;
};

int NFRClientCreate(NFRClientOpts & opts, NFRClientResource & out);