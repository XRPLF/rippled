//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#include <ripple/basics/safe_cast.h>
#include <ripple/conditions/Condition.h>
#include <ripple/conditions/Fulfillment.h>
#include <ripple/conditions/impl/PreimageSha256.h>
#include <ripple/conditions/impl/utils.h>
#include <boost/optional.hpp>
#include <boost/regex.hpp>
#include <type_traits>
#include <vector>

namespace ripple {
namespace cryptoconditions {

bool
match(Fulfillment const& f, Condition const& c)
{
    // Fast check: the fulfillment's type must match the
    // conditions's type:
    if (f.type() != c.type)
        return false;

    // Derive the condition from the given fulfillment
    // and ensure that it matches the given condition.
    return c == f.condition();
}

bool
validate(Fulfillment const& f, Condition const& c, Slice m)
{
    return match(f, c) && f.validate(m);
}

bool
validate(Fulfillment const& f, Condition const& c)
{
    return validate(f, c, {});
}

std::unique_ptr<Fulfillment>
Fulfillment::deserialize(Slice s, std::error_code& ec)
{
    // Per the RFC, in a fulfillment we choose a type based
    // on the tag of the item we contain:
    //
    // Fulfillment ::= CHOICE {
    //     preimageSha256   [0] PreimageFulfillment ,
    //     prefixSha256     [1] PrefixFulfillment,
    //     thresholdSha256  [2] ThresholdFulfillment,
    //     rsaSha256        [3] RsaSha256Fulfillment,
    //     ed25519Sha256    [4] Ed25519Sha512Fulfillment
    // }

    if (s.empty())
    {
        ec = error::buffer_empty;
        return nullptr;
    }

    using namespace der;

    auto const p = parsePreamble(s, ec);
    if (ec)
        return nullptr;

    // All fulfillments are context-specific, constructed types
    if (!isConstructed(p) || !isContextSpecific(p))
    {
        ec = error::malformed_encoding;
        return nullptr;
    }

    if (p.length > s.size())
    {
        ec = error::buffer_underfull;
        return {};
    }

    if (p.length < s.size())
    {
        ec = error::buffer_overfull;
        return {};
    }

    if (p.length > maxSerializedFulfillment)
    {
        ec = error::large_size;
        return {};
    }

    std::unique_ptr<Fulfillment> f;

    using TagType = decltype(p.tag);
    switch (p.tag)
    {
        case safe_cast<TagType>(Type::preimageSha256):
            f = PreimageSha256::deserialize(Slice(s.data(), p.length), ec);
            if (ec)
                return {};
            s += p.length;
            break;

        case safe_cast<TagType>(Type::prefixSha256):
            ec = error::unsupported_type;
            return {};
            break;

        case safe_cast<TagType>(Type::thresholdSha256):
            ec = error::unsupported_type;
            return {};
            break;

        case safe_cast<TagType>(Type::rsaSha256):
            ec = error::unsupported_type;
            return {};
            break;

        case safe_cast<TagType>(Type::ed25519Sha256):
            ec = error::unsupported_type;
            return {};

        default:
            ec = error::unknown_type;
            return {};
    }

    if (!s.empty())
    {
        ec = error::trailing_garbage;
        return {};
    }

    return f;
}

}  // namespace cryptoconditions
}  // namespace ripple
