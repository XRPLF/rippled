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

#ifndef RIPPLE_CONDITIONS_CONDITION_H
#define RIPPLE_CONDITIONS_CONDITION_H

#include <ripple/basics/Buffer.h>
#include <ripple/basics/Slice.h>
#include <ripple/conditions/impl/utils.h>
#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace ripple {
namespace cryptoconditions {

enum class Type
    : std::uint8_t
{
    preimageSha256 = 0,
    prefixSha256 = 1,
    thresholdSha256 = 2,
    rsaSha256 = 3,
    ed25519Sha256 = 4
};

class Condition
{
public:
    /** Load a condition from its binary form

        @param s The buffer containing the fulfillment to load.
        @param ec Set to the error, if any occurred.

        The binary format for a condition is specified in the
        cryptoconditions RFC. See:

        https://tools.ietf.org/html/draft-thomas-crypto-conditions-02#section-7.2
    */
    static
    std::unique_ptr<Condition>
    deserialize(Slice s, std::error_code& ec);

public:
    Type type;

    /** An identifier for this condition.

        This fingerprint is meant to be unique only with
        respect to other conditions of the same type.
    */
    Buffer fingerprint;

    /** The cost associated with this condition. */
    std::uint32_t cost;

    /** For compound conditions, set of conditions includes */
    std::set<Type> subtypes;

    Condition(Type t, std::uint32_t c, Slice fp)
        : type(t)
        , fingerprint(fp)
        , cost(c)
    {
    }

    Condition(Type t, std::uint32_t c, Buffer&& fp)
        : type(t)
        , fingerprint(std::move(fp))
        , cost(c)
    {
    }

    ~Condition() = default;

    Condition(Condition const&) = default;
    Condition(Condition&&) = default;

    Condition() = delete;
};

inline
bool
operator== (Condition const& lhs, Condition const& rhs)
{
    return
        lhs.type == rhs.type &&
            lhs.cost == rhs.cost &&
                lhs.subtypes == rhs.subtypes &&
                    lhs.fingerprint == rhs.fingerprint;
}

inline
bool
operator!= (Condition const& lhs, Condition const& rhs)
{
    return !(lhs == rhs);
}

}

}

#endif
