// SPDX-License-Identifier: GPL-2.0-or-later
// NetFR Reference Implementation - Variable Message Handling
// Copyright (c) 2023 Tim Dettmar

#include "nfr_vmsg.hpp"
#include "nfr_util.hpp"

void NFRHostMetadataConstructor::extend_buffers(uint64_t delta) {
  if (!this->can_realloc)
    throw tcm_exception(ENOBUFS, __FILE__, __LINE__,
                        "User-provided buffer space exceeeded");
  void * tmp = realloc(this->buffer, length + delta);
  if (!tmp)
    throw tcm_exception(ENOMEM, __FILE__, __LINE__, "Could not expand buffer");
  this->buffer = tmp;
  this->length = length + delta;
}

NFRHostMetadataConstructor::NFRHostMetadataConstructor() {
  this->buffer = calloc(1, 128);
  if (!this->buffer)
    throw tcm_exception(ENOMEM, __FILE__, __LINE__,
                        "Metadata buffer alloc failed");
  NFRHostMetadata * mtd = static_cast<NFRHostMetadata *>(this->buffer);
  mtd->header           = NFRHeaderCreate(NFR_MSG_HOST_METADATA);
  this->length          = 128;
  this->used            = sizeof(*mtd);
  this->can_realloc     = 1;
}

NFRHostMetadataConstructor::NFRHostMetadataConstructor(void *   buf,
                                                       uint64_t length) {
  this->buffer          = buf;
  NFRHostMetadata * mtd = static_cast<NFRHostMetadata *>(this->buffer);
  if (length < sizeof(*mtd))
    throw tcm_exception(ENOBUFS, __FILE__, __LINE__,
                        "Buffer size smaller than minimum length");
  mtd->header       = NFRHeaderCreate(NFR_MSG_HOST_METADATA);
  this->length      = length;
  this->used        = sizeof(*mtd);
  this->can_realloc = 0;
}

void NFRHostMetadataConstructor::addField(NFRFieldType type,
                                          const void * data) {
  uint8_t size = nfrResolvePrimType(nfrResolveFieldType(type));
  if (size == 0)
    throw tcm_exception(EINVAL, __FILE__, __LINE__, "Invalid field type");
  this->addField(type, data, size);
}

void NFRHostMetadataConstructor::addField(NFRFieldType type, const void * data,
                                          uint16_t size) {
  if (this->used + size > this->length) {
    if (size < 128)
      this->extend_buffers(128);
    else
      this->extend_buffers(size);
  }
  tcm__log_trace("Adding field %s (%d), offset: %d, size: %d (%d) -> %d",
                 nfrFieldTypeStr(type), type, this->used, size,
                 size + sizeof(NFRField), this->used + size + sizeof(NFRField));
  NFRField * f = (NFRField *) ((uint8_t *) this->buffer + this->used);
  f->type      = (uint8_t) type;
  f->len       = size;
  memcpy(f->data, data, size);
  this->used += (size + sizeof(*f));
}

NFRField * NFRHostMetadataConstructor::getField(NFRFieldType type) {
  NFRField * f = (NFRField *) this->buffer;
  while ((uint64_t) ((uint8_t *) f - (uint8_t *) this->buffer) <=
         this->length) {
    if (f->type == type)
      return f;
    if (f->type <= NFR_F_INVALID || f->type >= NFR_F_MAX)
      return 0;
    uint16_t size =
        nfrResolvePrimType(nfrResolveFieldType((NFRFieldType) f->type));
    if (size == 0)
      size = f->len;
    uint64_t p = (uint64_t) ((uint8_t *) f - (uint8_t *) this->buffer);
    if ((uint64_t) size + p > this->length)
      return 0;
  }
  assert((uint64_t) ((uint8_t *) f - (uint8_t *) this->buffer) <= this->length);
  return 0;
}

uint64_t NFRHostMetadataConstructor::getUsed() { return this->used; }

NFRHostMetadataConstructor::~NFRHostMetadataConstructor() {
  if (this->can_realloc)
    free(this->buffer);
  this->buffer      = 0;
  this->can_realloc = 0;
  this->length      = 0;
  this->used        = 0;
}