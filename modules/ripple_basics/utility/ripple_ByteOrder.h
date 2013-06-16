//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_BYTEORDER_H
#define RIPPLE_BYTEORDER_H

// Routines for converting endianness

// Reference: http://www.mail-archive.com/licq-commits@googlegroups.com/msg02334.html

// VFALCO TODO use VFLIB_* platform macros instead of hard-coded compiler specific ones
#ifdef WIN32
extern uint64_t htobe64 (uint64_t value);
extern uint64_t be64toh (uint64_t value);
extern uint32_t htobe32 (uint32_t value);
extern uint32_t be32toh (uint32_t value);

#elif __APPLE__
#include <libkern/OSByteOrder.h>

#define htobe16(x) OSSwapHostToBigInt16(x)
#define htole16(x) OSSwapHostToLittleInt16(x)
#define be16toh(x) OSSwapBigToHostInt16(x)
#define le16toh(x) OSSwapLittleToHostInt16(x)

#define htobe32(x) OSSwapHostToBigInt32(x)
#define htole32(x) OSSwapHostToLittleInt32(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#define le32toh(x) OSSwapLittleToHostInt32(x)

#define htobe64(x) OSSwapHostToBigInt64(x)
#define htole64(x) OSSwapHostToLittleInt64(x)
#define be64toh(x) OSSwapBigToHostInt64(x)
#define le64toh(x) OSSwapLittleToHostInt64(x)

#elif defined(__FreeBSD__) || defined(__NetBSD__)
#include <sys/endian.h>

#elif defined(__OpenBSD__)
#include <sys/types.h>
#define be16toh(x) betoh16(x)
#define be32toh(x) betoh32(x)
#define be64toh(x) betoh64(x)

#endif

#endif

// vim:ts=4
