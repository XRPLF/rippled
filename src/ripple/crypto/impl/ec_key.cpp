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

// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2011 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include <ripple/basics/contract.h>
#include <ripple/crypto/impl/ec_key.h>
#include <openssl/ec.h>

namespace ripple {
namespace openssl {

static inline EC_KEY*
get_EC_KEY(const ec_key& that)
{
    return (EC_KEY*)that.get();
}

ec_key::ec_key(const ec_key& that)
{
    if (that.ptr == nullptr)
    {
        ptr = nullptr;
        return;
    }

    ptr = (pointer_t)EC_KEY_dup(get_EC_KEY(that));

    if (ptr == nullptr)
        Throw<std::runtime_error>("ec_key::ec_key() : EC_KEY_dup failed");

    EC_KEY_set_conv_form(get_EC_KEY(*this), POINT_CONVERSION_COMPRESSED);
}

void
ec_key::destroy()
{
    if (ptr != nullptr)
    {
        EC_KEY_free(get_EC_KEY(*this));
        ptr = nullptr;
    }
}

}  // namespace openssl
}  // namespace ripple
