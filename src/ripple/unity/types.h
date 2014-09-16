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

#ifndef RIPPLE_TYPES_H_INCLUDED
#define RIPPLE_TYPES_H_INCLUDED

#include <ripple/unity/json.h>

#include <beast/Crypto.h>

#include <boost/utility/base_from_member.hpp>
#include <boost/functional/hash.hpp>

// For ByteOrder
#if BEAST_WIN32
// (nothing)
#elif __APPLE__
#include <libkern/OSByteOrder.h>
#elif defined(__FreeBSD__) || defined(__NetBSD__)
#include <sys/endian.h>
#elif defined(__OpenBSD__)
#include <sys/types.h>
#endif

#include <ripple/types/AgedHistory.h>
#include <ripple/types/Blob.h>
#include <ripple/types/Base58.h>
#include <ripple/types/Book.h>
#include <ripple/types/ByteOrder.h>
#include <ripple/types/strHex.h>
#include <ripple/types/UInt160.h>
#include <ripple/types/RandomNumbers.h>
#include <ripple/types/HashMaps.h>

#include <ripple/types/IdentifierType.h>
#include <ripple/types/IdentifierStorage.h>
#include <ripple/types/CryptoIdentifier.h>
#include <ripple/types/RippleAccountID.h>
#include <ripple/types/RippleAccountPublicKey.h>
#include <ripple/types/RippleAccountPrivateKey.h>
#include <ripple/types/RipplePublicKey.h>
#include <ripple/types/RipplePrivateKey.h>
#include <ripple/types/SimpleIdentifier.h>
#include <ripple/types/RippleLedgerHash.h>
#include <ripple/types/RipplePublicKeyHash.h>

#endif
