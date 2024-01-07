// SPDX-License-Identifier: GPL-2.0-or-later
// NetFR - Core Protocol Headers
// Copyright (c) 2023 Tim Dettmar

// This header contains the core NetFR protocol structures and can be included
// in your application without compiling the NetFR library. However, this header
// contains no logic. It can be implemented using the NetFR reference
// implementation or a custom implementation according to the NetFR protocol
// specification.

// This header requires two nonstandard C extensions:
// - #pragma pack
// - Zero-length arrays

#ifndef NFR_PROTOCOL_H_
#define NFR_PROTOCOL_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4200)
#endif

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#define NETFR_VERSION 1
#define NETFR_MAGIC "NetFR---"

typedef uint8_t  NFRFrameType;
typedef uint8_t  NFRFrameRotation;
typedef uint64_t NFROffset;
typedef uint8_t  NFRInfoType;
typedef uint8_t  NFRFieldFormatType;

enum NFRDirection {
  NFR_DIR_CLIENT,
  NFR_DIR_SERVER
};

enum NFRBufferType {
  NFR_BUF_INVALID,
  NFR_BUF_FRAME,
  NFR_BUF_CURSOR_DATA,
  NFR_BUF_MAX
};

enum NFRPrimType {
  NFR_PT_INVALID,
  NFR_PT_UINT8  = 1,  // uint8_t
  NFR_PT_UINT16 = 2,  // uint16_t
  NFR_PT_UINT32 = 3,  // uint32_t
  NFR_PT_UINT64 = 4,  // uint64_t
  NFR_PT_INT8   = 5,  // int8_t
  NFR_PT_INT16  = 6,  // int16_t
  NFR_PT_INT32  = 7,  // int32_t
  NFR_PT_INT64  = 8,  // int64_t
  NFR_PT_BYTE   = 9,  // Arbitrary byte stream
  NFR_PT_CHAR   = 10, // char[]
  NFR_PT_UTF8   = 12, // reserved
  NFR_PT_UTF16  = 13, // reserved
  NFR_PT_UTF32  = 14, // reserved
  NFR_PT_MAX
};

static const uint8_t nfrPrimLUT[][2] = {
    {NFR_PT_INVALID, 0}, {NFR_PT_UINT8, 1},  {NFR_PT_UINT16, 2},
    {NFR_PT_UINT32, 4},  {NFR_PT_UINT64, 8}, {NFR_PT_INT8, 1},
    {NFR_PT_INT16, 2},   {NFR_PT_INT32, 4},  {NFR_PT_INT64, 8},
    {NFR_PT_BYTE, 0},    {NFR_PT_CHAR, 0},   {NFR_PT_UTF8, 0},
    {NFR_PT_UTF16, 0},   {NFR_PT_UTF32, 0},  {NFR_PT_MAX, 0}};

/* Get the size of a fixed-size primitive data type */
static inline uint8_t nfrResolvePrimType(NFRPrimType t) {
  if (t == NFR_PT_INVALID)
    return 0;
  for (int i = 0; nfrPrimLUT[i][0] != NFR_PT_MAX; ++i) {
    if (t == nfrPrimLUT[i][0])
      return nfrPrimLUT[i][1];
  }
  return 0;
}

enum NFRFieldType {
  NFR_F_INVALID,

  // Basic data (required)

  NFR_F_FEATURE_FLAGS = 1, // see NFRFeature
  NFR_F_UUID          = 2, // 16-byte UUID
  NFR_F_NAME          = 3, // System name

  // Extended metadata (optional)

  NFR_F_EXT_PROXIED = 32,   // Proxy status
  NFR_F_EXT_CPU_SOCKETS,    // Num CPU sockets
  NFR_F_EXT_CPU_CORES,      // Num CPU cores
  NFR_F_EXT_CPU_THREADS,    // Num CPU threads
  NFR_F_EXT_CAPTURE_METHOD, // Capture method string
  NFR_F_EXT_OS_ID,          // KVMFR OS ID
  NFR_F_EXT_OS_NAME,        // OS full name string
  NFR_F_EXT_CPU_MODEL,      // CPU model string
  NFR_F_EXT_LINK_RATE,      // Maximum link rate in bps

  NFR_F_MAX = 255
};

static inline const char * nfrFieldTypeStr(uint8_t type) {
  switch (type) {
    case NFR_F_FEATURE_FLAGS: return "FEATURE_FLAGS";
    case NFR_F_UUID: return "UUID";
    case NFR_F_NAME: return "NAME";
    case NFR_F_EXT_PROXIED: return "EXT_PROXIED";
    case NFR_F_EXT_CPU_SOCKETS: return "EXT_CPU_SOCKETS";
    case NFR_F_EXT_CPU_CORES: return "EXT_CPU_CORES";
    case NFR_F_EXT_CPU_THREADS: return "EXT_CPU_THREADS";
    case NFR_F_EXT_CAPTURE_METHOD: return "EXT_CAPTURE_METHOD";
    case NFR_F_EXT_OS_ID: return "EXT_OS_ID";
    case NFR_F_EXT_OS_NAME: return "EXT_OS_NAME";
    case NFR_F_EXT_CPU_MODEL: return "EXT_CPU_MODEL";
    case NFR_F_EXT_LINK_RATE: return "EXT_LINK_RATE";
    default: return "?";
  }
}

static const uint8_t nfrFieldLUT[][2] = {
    {NFR_F_FEATURE_FLAGS, NFR_PT_UINT8},
    {NFR_F_UUID, NFR_PT_BYTE},
    {NFR_F_NAME, NFR_PT_CHAR},
    {NFR_F_EXT_PROXIED, NFR_PT_UINT8},
    {NFR_F_EXT_CPU_SOCKETS, NFR_PT_UINT16},
    {NFR_F_EXT_CPU_CORES, NFR_PT_UINT16},
    {NFR_F_EXT_CPU_THREADS, NFR_PT_UINT16},
    {NFR_F_EXT_CAPTURE_METHOD, NFR_PT_CHAR},
    {NFR_F_EXT_OS_ID, NFR_PT_UINT8},
    {NFR_F_EXT_OS_NAME, NFR_PT_CHAR},
    {NFR_F_EXT_CPU_MODEL, NFR_PT_CHAR},
    {NFR_F_EXT_LINK_RATE, NFR_PT_UINT64},
    {NFR_F_INVALID, NFR_PT_INVALID}};

/* Resolve a field type into its underlying data type */
static inline NFRPrimType nfrResolveFieldType(NFRFieldType t) {
  for (int i = 0; nfrFieldLUT[i][0] != NFR_F_INVALID; ++i) {
    if (t == nfrFieldLUT[i][0])
      return (NFRPrimType) nfrFieldLUT[i][1];
  }
  return NFR_PT_INVALID;
}

enum NFRFeature {
  NFR_FEATURE_FRAME  = (1 << 0), // Frame transmission support
  NFR_FEATURE_CURSOR = (1 << 1), // Cursor position/texture data support
  NFR_FEATURE_CURSOR_POSITION = (1 << 2), // Cursor repositioning support
  NFR_FEATURE_EXT_METADATA    = (1 << 3)  // Extended metadata
};

enum NFRMessageType {
  NFR_MSG_INVALID,
  NFR_MSG_CONN_SETUP,
  NFR_MSG_HOST_METADATA,
  NFR_MSG_CLIENT_FRAME_BUF,
  NFR_MSG_CLIENT_CURSOR_BUF,
  NFR_MSG_CLIENT_ACK,
  NFR_MSG_FRAME_METADATA,
  NFR_MSG_CURSOR_METADATA,
  NFR_MSG_CURSOR_ALIGN,
  NFR_MSG_STATE,
  NFR_MSG_MAX
};

enum NFRStates {
  NFR_STATE_INVALID,
  NFR_STATE_KA,         // Keep alive
  NFR_STATE_DISCONNECT, // Disconnect
  NFR_STATE_PAUSE,      // Temporary pause request (e.g. when no signal)
  NFR_STATE_RESUME,     // Resume relay
  NFR_STATE_MAX
};

#pragma pack(push, 1)

/* ------------------------------------------------------------ Initial Setup */

struct NFRPrvData {
  char magic[8];      // Magic number
  char build_ver[32]; // Build version
};

/* --------------------------------------------------- Common Message Headers */

struct NFRHeader {
  char    magic[8]; // Magic number
  uint8_t type;     // Message type
  uint8_t _pad[3];  // Padding
};

/* ------------------------------------------------------------------ Control */

struct NFRState {
  struct NFRHeader header;
  uint8_t          state;
};

/* ------------------------------------------------- Initial Connection Setup */

struct NFRConnSetup {
  struct NFRHeader header;     // Common header
  uint16_t         framePort;  // Port no. of frame channel (big endian!)
  uint16_t         sysTimeout; // Connection timeout in milliseconds
  uint8_t          direction;  // Connection direction (see NFRDirection)
};

/* -------------------------------------------------- Host System Information */

struct NFRHostMetadata {
  struct NFRHeader header;
  uint8_t          data[0]; // Fields
};

struct NFRField {
  uint8_t  type;
  uint16_t len;
  uint8_t  data[0];
};

/* ------------------------------------------------ Client Buffer Information */

struct NFRClientCursorBuf {
  struct NFRHeader header;
  uint64_t         base;       // Starting address or IOVA of cursor buffer
  uint64_t         maxlen;     // Buffer slot size
  uint64_t         key;        // Remote access key (rkey)
  uint8_t          _pad[3];    // Padding
  NFROffset        offsets[0]; // Start of cursor texture buffer offsets
};

struct NFRClientFrameBuf {
  struct NFRHeader header;
  uint64_t         base;       // Starting address or IOVA of frame buffer
  uint64_t         maxlen;     // Buffer slot size
  uint64_t         key;        // Remote access key (rkey)
  uint8_t          _pad[3];    // Padding
  NFROffset        offsets[0]; // Start of frame buffer offsets
};

struct NFRClientAck {
  struct NFRHeader header;
  uint8_t          type;       // Buffer type
  int8_t           indexes[0]; // Buffer indexes (frame, then cursor)
};

/* --------------------------------------------------- Client Cursor Messages */

struct NFRCursorAlign {
  struct NFRHeader header;
  int16_t          x, y;
};

/* ------------------------------------------------ Texture Metadata Messages */

struct NFRFrameMetadata {
  struct NFRHeader header;
  /* If this metadata message is not standalone (i.e., it is associated with a
   * frame that has been captured and written into a memory region), this must
   * be set to indicate which buffer this metadata corresponds with
   */
  int8_t           buffer;
  uint32_t         width;          // Width of the frame
  uint32_t         height;         // Height of the frame
  uint32_t         row_bytes;      // Bytes per row
  uint32_t         flags;          // Frame flags    (FRAME_FLAG_*)
  NFRFrameType     frame_type;     // Frame type     (KVMFRFrame)
  NFRFrameRotation frame_rotation; // Frame rotation (FrameRotation)
};

struct NFRCursorMetadata {
  struct NFRHeader header;
  int8_t   buffer; // Buffer location, if this message is associated w/ a cursor
                   // shape change
  int16_t  x, y;   // Position x, y
  int16_t  hx, hy; // Hotspot x,y
  uint32_t flags;  // KVMFR flags

  // Only set if buffer >= 0

  uint8_t  format;    // Texture format (CursorType)
  uint16_t width;     // Width of the texture
  uint16_t height;    // Height of the texture
  uint32_t row_bytes; // Row length in bytes
};

// Rough check that the struct packing is actually working

struct NFR_Test_ {
  uint8_t  a;
  uint16_t b;
  uint32_t c;
  uint64_t d;
};

static_assert(sizeof(struct NFR_Test_) == 15);

#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif // NFR_PROTOCOL_H_