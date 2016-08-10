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

#include <ripple/conditions/impl/base64.h> // use Beast implementation
#include <ripple/conditions/impl/utils.h>
#include <boost/optional.hpp>
#include <array>
#include <cstdint>
#include <string>

namespace ripple {
namespace cryptoconditions {

// NIKB-TODO: These should move to a separate file:
std::uint16_t constexpr condition_hashlock         = 0;
std::uint16_t constexpr condition_prefix_sha256    = 1;
std::uint16_t constexpr condition_threshold_sha256 = 2;
std::uint16_t constexpr condition_rsa_sha256       = 3;
std::uint16_t constexpr condition_ed25519          = 4;

// NIKB-TODO: These should be `enum class : std::uint32_t`
std::uint32_t constexpr feature_sha256             = 1;
std::uint32_t constexpr feature_preimage           = 2;
std::uint32_t constexpr feature_prefix             = 4;
std::uint32_t constexpr feature_threshold          = 8;
std::uint32_t constexpr feature_rsa_pss            = 16;
std::uint32_t constexpr feature_ed25519            = 32;

/** The list of all feature suited defined in the RFC */
std::uint32_t constexpr definedFeatures =
    feature_sha256 |
    feature_preimage |
    feature_prefix |
    feature_threshold |
    feature_rsa_pss |
    feature_ed25519;

/** The largest fulfillment supported by this implementation.

    Fulfillments larger than this value cannot be processed
    and will not be generated.
*/
constexpr std::size_t maxSupportedFulfillmentLength = 65535;

struct Condition
{
    std::uint16_t type;

    /** The maximum length of a fulfillment for this condition.

        While it is possible for a fulfillment to be smaller
        it can never be bigger than this.
    */
    std::uint16_t maxFulfillmentLength;

    /** The features suites required to process a fulfillment. */
    std::uint32_t featureBitmask;

    /** An identifier for this condition.

        This fingerprint is meant to be unique only with
        respect to other conditions of the same type.
    */
    std::array<std::uint8_t, 32> fingerprint;

    // Can this be deleted?
    Condition () = default;

    Condition (Condition const&) = default;
    Condition (Condition&&) = default;
};

inline
bool
operator== (Condition const& lhs, Condition const& rhs)
{
    return
        lhs.type == rhs.type &&
        lhs.featureBitmask == rhs.featureBitmask &&
        lhs.maxFulfillmentLength == rhs.maxFulfillmentLength &&
        lhs.fingerprint == rhs.fingerprint;
}

inline
bool
operator!= (Condition const& lhs, Condition const& rhs)
{
    return !(lhs == rhs);
}

/** Determine if a given condition is valid.

    @note this function checks whether it understands the
          type of the condition, and if so, whether it meets
          the requirements mandated by the RFC.
*/
inline
bool
validate (Condition const& c)
{
    // This check can never trigger because of the range of
    // the maxFulfillmentLength type. It's here in case the
    // type is changed in the future.

    if (c.maxFulfillmentLength > maxSupportedFulfillmentLength)
        return false;

    if (c.type == condition_hashlock)
        return (c.featureBitmask == (feature_sha256 | feature_preimage));

    // A prefix condition contains a subfulfillment; it
    // requires all the features its child may require.
    if (c.type == condition_prefix_sha256)
    {
        auto const mask = (feature_sha256 | feature_prefix);

        // We need to have at least our own feature suites:
        auto const cf1 = c.featureBitmask & mask;

        if (cf1 != mask)
            return false;

        // And at least one more feature suite for our
        // subfulfillment (since you need to terminate a
        // chain of prefix conditions with a non-prefix)
        auto const cf2 = c.featureBitmask & ~mask;

        if (cf2 == 0)
            return false;

        return (cf2 & definedFeatures) == cf2;
    }

    if (c.type == condition_ed25519)
        return (c.featureBitmask == feature_ed25519);

    return false;
}

/** True if condition type is specified in the RFC.

    @note: this function may return true even if the type
           of condition isn't presently supported by this
           implementation.
*/
inline
bool
isCondition (std::uint16_t type)
{
    switch(type)
    {
    case condition_hashlock:
    case condition_prefix_sha256:
    case condition_threshold_sha256:
    case condition_rsa_sha256:
    case condition_ed25519:
        return true;

    default:
        return false;
    }
}

/** Load a serialized condition either from its string or binary form */
/** @{ */
boost::optional<Condition>
loadCondition(std::string const& s);

boost::optional<Condition>
loadCondition(Slice s);
/** @} */

// Convert a condition to its string form
std::string
to_string (Condition const& c);

// Convert a condition to its binary form
std::vector<std::uint8_t>
to_blob (Condition const& c);

}

}

#endif
