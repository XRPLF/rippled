//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2014 Ripple Labs Inc.

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

#ifndef RIPPLE_CRYPTO_EC_KEY_H_INCLUDED
#define RIPPLE_CRYPTO_EC_KEY_H_INCLUDED

#include <ripple/basics/base_uint.h>
#include <cstdint>

namespace ripple {
namespace openssl {

class ec_key
{
public:
    using pointer_t = struct opaque_EC_KEY*;

    ec_key() : ptr(nullptr)
    {
    }

    ec_key(pointer_t raw) : ptr(raw)
    {
    }

    ~ec_key()
    {
        destroy();
    }

    bool
    valid() const
    {
        return ptr != nullptr;
    }

    pointer_t
    get() const
    {
        return ptr;
    }

    ec_key(const ec_key&);

    pointer_t
    release()
    {
        pointer_t released = ptr;

        ptr = nullptr;

        return released;
    }

private:
    pointer_t ptr;

    void
    destroy();

    ec_key&
    operator=(const ec_key&) = delete;
};

}  // namespace openssl
}  // namespace ripple

#endif
