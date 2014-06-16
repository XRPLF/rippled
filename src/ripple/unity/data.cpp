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

#include <BeastConfig.h>

#include <ripple/unity/data.h>

//#include <cmath>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/format.hpp>
#include <boost/foreach.hpp>
#include <boost/functional/hash.hpp>
#include <boost/range/adaptor/copied.hpp>
#include <boost/regex.hpp>
#include <boost/thread/mutex.hpp>

#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/pem.h>
#include <openssl/hmac.h>
#include <openssl/err.h>

#include <ripple/unity/sslutil.h>
#include <ripple/module/rpc/ErrorCodes.h>
#include <ripple/common/jsonrpc_fields.h>

// VFALCO TODO fix these warnings!
#if BEAST_MSVC
#pragma warning (push)
#pragma warning (disable: 4018) // signed/unsigned mismatch
#endif

#ifdef min
#undef min
#endif

#include <ripple/module/data/protocol/STParsedJSON.cpp>

#include <ripple/module/data/crypto/CKey.h> // needs RippleAddress VFALCO TODO remove this dependency cycle
#include <ripple/module/data/crypto/RFC1751.h>

#include <ripple/module/data/crypto/CKey.cpp>
#include <ripple/module/data/crypto/CKeyDeterministic.cpp>
#include <ripple/module/data/crypto/CKeyECIES.cpp>
#include <ripple/module/data/crypto/Base58Data.cpp>
#include <ripple/module/data/crypto/RFC1751.cpp>

#include <ripple/module/data/protocol/BuildInfo.cpp>
#include <ripple/module/data/protocol/FieldNames.cpp>
#include <ripple/module/data/protocol/HashPrefix.cpp>
#include <ripple/module/data/protocol/LedgerFormats.cpp>
#include <ripple/module/data/protocol/RippleAddress.cpp>
#include <ripple/module/data/protocol/SerializedTypes.cpp>
#include <ripple/module/data/protocol/Serializer.cpp>
#include <ripple/module/data/protocol/SerializedObjectTemplate.cpp>
#include <ripple/module/data/protocol/SerializedObject.cpp>
#include <ripple/module/data/protocol/TER.cpp>
#include <ripple/module/data/protocol/TxFormats.cpp>

// These are for STAmount
static const std::uint64_t tenTo14 = 100000000000000ull;
static const std::uint64_t tenTo14m1 = tenTo14 - 1;
static const std::uint64_t tenTo17 = tenTo14 * 1000;
static const std::uint64_t tenTo17m1 = tenTo17 - 1;
#include <ripple/module/data/protocol/STAmount.cpp>
#include <ripple/module/data/protocol/STAmountRound.cpp>

#if BEAST_MSVC
#pragma warning (pop)
#endif
