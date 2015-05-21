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

#ifndef RIPPLE_BASICS_CHECKLIBRARYVERSIONSIMPL_H_INCLUDED
#define RIPPLE_BASICS_CHECKLIBRARYVERSIONSIMPL_H_INCLUDED

#include <ripple/basics/CheckLibraryVersions.h>
#include <boost/version.hpp>
#include <openssl/opensslv.h>

namespace ripple {
namespace version {

/** Both Boost and OpenSSL have integral version numbers. */
using VersionNumber = unsigned long long;

/** Minimal required boost version. */
extern const char boostMinimal[];

/** Minimal required OpenSSL version. */
extern const char openSSLMinimal[];

std::string boostVersion(VersionNumber boostVersion = BOOST_VERSION);
std::string openSSLVersion(
    VersionNumber openSSLVersion = OPENSSL_VERSION_NUMBER);

void checkVersion(std::string name, std::string required, std::string actual);
void checkBoost(std::string version = boostVersion());
void checkOpenSSL(std::string version = openSSLVersion());

}  // namespace version
}  // namespace ripple

#endif
