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

#ifndef RIPPLE_TYPES_RIPPLEPUBLICKEYHASH_H_INCLUDED
#define RIPPLE_TYPES_RIPPLEPUBLICKEYHASH_H_INCLUDED

#include <ripple/types/api/SimpleIdentifier.h>

namespace ripple {

/** Traits for the public key hash. */
class RipplePublicKeyHashTraits : public SimpleIdentifier <20>
{
public:
    template <typename Other>
    struct assign
    {
        void operator() (value_type& value, Other const& other)
        {
            value = other;
        }
    };
};

/** A container holding the 160-bit hash of the 257-bit public key. */
typedef IdentifierType <RipplePublicKeyHashTraits> RipplePublicKeyHash;

}

#endif

