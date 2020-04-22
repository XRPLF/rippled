//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_CRYPTO_SHA2_H_INCLUDED
#define BEAST_CRYPTO_SHA2_H_INCLUDED

#include <ripple/beast/crypto/detail/mac_facade.h>
#include <ripple/beast/crypto/detail/sha2_context.h>

namespace beast {

using sha256_hasher = detail::mac_facade<detail::sha256_context, false>;

// secure version
using sha256_hasher_s = detail::mac_facade<detail::sha256_context, true>;

using sha512_hasher = detail::mac_facade<detail::sha512_context, false>;

// secure version
using sha512_hasher_s = detail::mac_facade<detail::sha512_context, true>;

}  // namespace beast

#endif
