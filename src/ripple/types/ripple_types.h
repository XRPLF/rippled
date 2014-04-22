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

#include "../json/ripple_json.h"

#include "../../beast/beast/Crypto.h"

#include "../../beast/modules/beast_core/system/BeforeBoost.h"
#include <boost/utility/base_from_member.hpp>
#include <boost/functional/hash.hpp>
#include <boost/unordered_set.hpp>

// For ByteOrder
#if BEAST_WIN32
// (nothing)
#elif __APPLE__
# include <libkern/OSByteOrder.h>
#elif defined(__FreeBSD__) || defined(__NetBSD__)
# include <sys/endian.h>
#elif defined(__OpenBSD__)
# include <sys/types.h>
#endif

#include "api/AgedHistory.h"
#  include "api/Blob.h"
# include "api/Base58.h"
#  include "api/ByteOrder.h"
#  include "api/strHex.h"
# include "api/UInt128.h"
# include "api/UInt256.h"
# include "api/UInt160.h"
# include "api/RandomNumbers.h"
#include "api/HashMaps.h"

# include "api/IdentifierType.h"
# include "api/IdentifierStorage.h"
# include "api/CryptoIdentifier.h"
#include "api/RippleAccountID.h"
#include "api/RippleAccountPublicKey.h"
#include "api/RippleAccountPrivateKey.h"
#include "api/RippleAssets.h"
#include "api/RipplePublicKey.h"
#include "api/RipplePrivateKey.h"
# include "api/SimpleIdentifier.h"
#include "api/RippleLedgerHash.h"
#include "api/RipplePublicKeyHash.h"

#endif
