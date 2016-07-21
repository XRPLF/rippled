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
// NIKB-TODO: These should be `enum class : std::uint16_t`
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

struct condition_t
{
    std::uint16_t type;
    std::uint16_t fulfillment_length;
    std::uint32_t requires;
    std::array<std::uint8_t, 32> fingerprint;

    // Can this be deleted?
    condition_t () = default;

    condition_t (condition_t const&) = default;
    condition_t (condition_t&&) = default;
};

inline
bool
operator== (condition_t const& lhs, condition_t const& rhs)
{
    return
        lhs.type == rhs.type &&
        lhs.requires == rhs.requires &&
        lhs.fulfillment_length == rhs.fulfillment_length &&
        lhs.fingerprint == rhs.fingerprint;
}

inline
bool
operator!= (condition_t const& lhs, condition_t const& rhs)
{
    return !(lhs == rhs);
}

inline
bool
validate (condition_t const& c)
{
    if (c.fulfillment_length > 65535)
        throw std::length_error ("The maximum fulfillment length supported is 65535");

    if (c.type != condition_hashlock &&
            c.type != condition_prefix_sha256 &&
                c.type != condition_threshold_sha256 &&
                    c.type != condition_rsa_sha256 &&
                        c.type != condition_ed25519)
        Throw<std::logic_error>("Unknown fulfillment type");

    // More parameter validation here:
    //  Check if features are known/supported:
    //  Check if the fingerprint contains only valid data?

    return true;
}

// Parse a condition from its string form
boost::optional<condition_t>
load_condition(std::string const& s);

// Convert a condition to its string form
std::string
to_string (condition_t const& c);

// Convert a condition to its binary form
std::vector<std::uint8_t>
to_blob (condition_t const& c);

}

}

#endif
