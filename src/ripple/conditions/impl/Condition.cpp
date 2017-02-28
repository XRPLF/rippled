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

#include <ripple/basics/contract.h>
#include <ripple/conditions/Condition.h>
#include <ripple/conditions/Fulfillment.h>
#include <ripple/conditions/impl/utils.h>
#include <boost/regex.hpp>
#include <boost/optional.hpp>
#include <vector>
#include <iostream>

namespace ripple {
namespace cryptoconditions {

namespace detail {
// The binary encoding of conditions differs based on their
// type.  All types define at least a fingerprint and cost
// sub-field.  Some types, such as the compound condition
// types, define additional sub-fields that are required to
// convey essential properties of the cryptocondition (such
// as the sub-types used by sub-conditions in the case of
// the compound types).
//
// Conditions are encoded as follows:
//
//    Condition ::= CHOICE {
//      preimageSha256   [0] SimpleSha256Condition,
//      prefixSha256     [1] CompoundSha256Condition,
//      thresholdSha256  [2] CompoundSha256Condition,
//      rsaSha256        [3] SimpleSha256Condition,
//      ed25519Sha256    [4] SimpleSha256Condition
//    }
//
//    SimpleSha256Condition ::= SEQUENCE {
//      fingerprint          OCTET STRING (SIZE(32)),
//      cost                 INTEGER (0..4294967295)
//    }
//
//    CompoundSha256Condition ::= SEQUENCE {
//      fingerprint          OCTET STRING (SIZE(32)),
//      cost                 INTEGER (0..4294967295),
//      subtypes             ConditionTypes
//    }
//
//    ConditionTypes ::= BIT STRING {
//      preImageSha256   (0),
//      prefixSha256     (1),
//      thresholdSha256  (2),
//      rsaSha256        (3),
//      ed25519Sha256    (4)
//    }

std::unique_ptr<Condition>
loadSimpleSha256(Type type, Slice s, std::error_code& ec)
{
    using namespace der;

    auto p = parsePreamble(s, ec);

    if (ec)
        return {};

    if (p.tag != 0)
    {
        ec = error::unexpected_tag;
        return {};
    }

    Buffer b = parseOctetString(s, p.length, ec);

    if (ec)
        return {};

    p = parsePreamble(s, ec);

    if (ec)
        return {};

    if (p.tag != 1)
    {
        ec = error::unexpected_tag;
        return {};
    }

    auto cost = parseInteger<std::uint32_t>(s, p.length, ec);

    if (ec)
        return {};

    if (!s.empty())
    {
        ec = error::trailing_garbage;
        return {};
    }

    ec = {};
    return std::make_unique<Condition>(type, cost, std::move(b));
}

}

std::unique_ptr<Condition>
Condition::deserialize(Slice s, std::error_code& ec)
{
    // Per the RFC, in a condition we choose a type based
    // on the tag of the item we contain:
    //
    // Condition ::= CHOICE {
    //     preimageSha256   [0] SimpleSha256Condition,
    //     prefixSha256     [1] CompoundSha256Condition,
    //     thresholdSha256  [2] CompoundSha256Condition,
    //     rsaSha256        [3] SimpleSha256Condition,
    //     ed25519Sha256    [4] SimpleSha256Condition
    // }
    if (s.empty())
    {
        ec = error::generic;
        return {};
    }

    using namespace der;

    auto const p = parsePreamble(s, ec);
    if (ec)
        return {};

    // All fulfillments are context-specific, constructed
    // types
    if (!isConstructed(p) || !isContextSpecific(p))
    {
        ec = error::generic;
        return {};
    }

    if (p.length > s.size())
    {
        ec = error::generic;
        return {};
    }

    std::unique_ptr<Condition> c;

    switch (p.tag)
    {
    case 0: // PreimageSha256
        c = detail::loadSimpleSha256(
            Type::preimageSha256,
            Slice(s.data(), p.length), ec);
        if (!ec)
            s += p.length;
        break;

    case 1: // PrefixSha256
        ec = error::unsupported_type;
        return {};

    case 2: // ThresholdSha256
        ec = error::unsupported_type;
        return {};

    case 3: // RsaSha256
        ec = error::unsupported_type;
        return {};

    case 4: // Ed25519Sha256
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

    return c;
}

}
}
