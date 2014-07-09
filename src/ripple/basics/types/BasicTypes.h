//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_BASICTYPES_H
#define RIPPLE_BASICTYPES_H

#include <mutex>

namespace ripple {

typedef unsigned char uint8;
typedef std::uint16_t uint16;
typedef std::uint32_t uint32;
typedef std::uint64_t uint64;

typedef char int8;
typedef std::int16_t int16;
typedef std::int32_t int32;
typedef std::int64_t int64;

/** Storage for linear binary data.
    Blocks of binary data appear often in various idioms and structures.
*/
typedef std::vector <uint8> Blob;

/** Synchronization primitives.
    This lets us switch between tracked and untracked mutexes.
*/
typedef std::mutex RippleMutex;
typedef std::recursive_mutex RippleRecursiveMutex;

typedef std::recursive_mutex DeprecatedRecursiveMutex;
typedef std::lock_guard<DeprecatedRecursiveMutex> DeprecatedScopedLock;

//------------------------------------------------------------------------------

/** A callback used to check for canceling an operation. */
typedef std::function <bool(void)> CancelCallback;

} // ripple

#endif
