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

#include <ripple/unity/types.h>
#include <ripple/unity/sslutil.h>

#ifdef BEAST_WIN32
#include <Winsock2.h> // for ByteOrder.cpp
// <Winsock2.h> defines min, max and does other stupid things
# ifdef max
# undef max
# endif
# ifdef min
# undef min
# endif
#endif

#include <set>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <boost/unordered_set.hpp>

#include <ripple/types/impl/Base58.cpp>
#include <ripple/types/impl/ByteOrder.cpp>
#include <ripple/types/impl/RandomNumbers.cpp>
#include <ripple/types/impl/strHex.cpp>
#include <ripple/types/impl/base_uint.cpp>
#include <ripple/types/impl/UInt160.cpp>
#include <ripple/types/impl/RippleIdentifierTests.cpp>
#include <ripple/types/impl/RippleAssets.cpp>

#include <ripple/common/tests/cross_offer.test.cpp>

