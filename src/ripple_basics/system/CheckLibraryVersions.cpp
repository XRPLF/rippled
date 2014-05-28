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

#include <sstream>
#include <vector>

#include <boost/version.hpp>
#include <openssl/opensslv.h>

namespace ripple {
namespace version {

static const int BOOST_MINIMAL = 105500;
static_assert (BOOST_VERSION >= BOOST_MINIMAL,
    "Boost version 1.55.0 or later is required to compile rippled.");

#if defined(RIPPLE_UBUNTU) || defined(RIPPLE_DEBIAN)

static const int OPENSSL_MINIMAL = 0x1000106fL;
static_assert (OPENSSL_VERSION_NUMBER >= OPENSSL_MINIMAL,
    "openSSL version 1.0.1-f or later is required to compile rippled");

#else

static const int OPENSSL_MINIMAL = 0x10001070L;
static_assert (OPENSSL_VERSION_NUMBER >= OPENSSL_MINIMAL,
    "openSSL version 1.0.1-g or later is required to compile rippled");

#endif

#if defined(__GNUC__) && !defined(__clang__)

static const int GCC_MINIMAL = 40801;
auto constexpr GCC_VERSION_NUMBER =
    (__GNUC__ * 100 * 100) +
    (__GNUC_MINOR__ * 100) +
    __GNUC_PATCHLEVEL__;

static_assert (GCC_VERSION_NUMBER >= 40801,
    "GCC version 4.8.1 or later is required to compile rippled.");

#endif

#ifdef _MSC_VER

static const int MSVC_MINIMAL = 1800;
static_assert (_MSC_VER >= 1800,
     "Visual Studio 2013 or later is required to compile rippled.");

#endif

}  // namespace version
}  // namespace ripple
