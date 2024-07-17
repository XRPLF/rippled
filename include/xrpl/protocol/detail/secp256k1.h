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

#ifndef RIPPLE_PROTOCOL_SECP256K1_H_INCLUDED
#define RIPPLE_PROTOCOL_SECP256K1_H_INCLUDED

#include <secp256k1.h>

namespace ripple {

template <class = void>
secp256k1_context const*
secp256k1Context()
{
    struct holder
    {
        secp256k1_context* impl;
        holder()
            : impl(secp256k1_context_create(
                  SECP256K1_CONTEXT_VERIFY | SECP256K1_CONTEXT_SIGN))
        {
        }

        ~holder()
        {
            secp256k1_context_destroy(impl);
        }
    };
    static holder const h;
    return h.impl;
}

}  // namespace ripple

#endif
