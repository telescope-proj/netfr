// SPDX-License-Identifier: GPL-2.0-or-later
// NetFR Reference Implementation - Variable Message Handling
// Copyright (c) 2023 Tim Dettmar

#ifndef NFR_VMSG_HPP_
#define NFR_VMSG_HPP_

/* Classes to handle variable NetFR messages */

#include "nfr_protocol.h"
#include "tcm_exception.h"

#include <stdlib.h>

class NFRHostMetadataConstructor {

  uint8_t  can_realloc;
  void *   buffer;
  uint64_t length;
  uint64_t used;

  void extend_buffers(uint64_t delta);

public:
  NFRHostMetadataConstructor();
  NFRHostMetadataConstructor(void * buf, uint64_t length);
  void       addField(NFRFieldType type, const void * data);
  void       addField(NFRFieldType type, const void * data, uint16_t size);
  NFRField * getField(NFRFieldType type);
  uint64_t   getUsed();

  ~NFRHostMetadataConstructor();
};

#endif