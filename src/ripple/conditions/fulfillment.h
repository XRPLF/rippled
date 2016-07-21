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

#ifndef RIPPLE_CONDITIONS_FULFILLMENT_H
#define RIPPLE_CONDITIONS_FULFILLMENT_H

#include <ripple/basics/Buffer.h>
#include <ripple/basics/Slice.h>
#include <ripple/conditions/condition.h>
#include <ripple/conditions/impl/utils.h>
#include <boost/optional.hpp>

namespace ripple {
namespace cryptoconditions {

constexpr std::size_t maxFulfillmentLength = 65535;

struct fulfillment_t
{
public:
    virtual ~fulfillment_t() = default;

    fulfillment_t () = default;

    /** Returns the fulfillment's payload */
    virtual
    Buffer
    payload() const = 0;

    /** Generates the condition */
    virtual
    condition_t
    condition() const = 0;

    /** Returns the type */
    virtual
    std::uint16_t
    type () const = 0;

    /** Returns the features suites required */
    virtual
    std::uint32_t
    requires () const = 0;

    /** Validates a fulfillment that was loaded. */
    virtual
    bool
    validate (Slice const& data) const = 0;
};

inline
bool
operator== (fulfillment_t const& lhs, fulfillment_t const& rhs)
{
    return
        lhs.type() == rhs.type() &&
        lhs.payload() == rhs.payload();
}

inline
bool
operator!= (fulfillment_t const& lhs, fulfillment_t const& rhs)
{
    return !(lhs == rhs);
}

/** Load a serialized condition either from its string or binary form */
/** @{ */
std::unique_ptr<fulfillment_t>
load_fulfillment (std::string s);

std::unique_ptr<fulfillment_t>
load_fulfillment (Slice const& s);
/** @} */

// Convert a fulfillment to its string form
std::string
to_string (fulfillment_t const& f);

// Convert a fulfillment to its binary form
Buffer
to_blob (fulfillment_t const& f);

/** Verify if a fulfillment satisfies the given condition.

    @param f The fulfillment
    @param c The condition
    @param m The message; note that the message is not
             relevant for some conditions (e.g. hashlocks)
             and a fulfillment will successfully satisfy its
             condition for any given message.
*/
bool
validate (
    fulfillment_t const& f,
    condition_t const& c,
    Slice const& m);

/** Verify if the given message satisfies the fulfillment.

    @param f The fulfillment
    @param m The message; note that the message is not
             relevant for some conditions (e.g. hashlocks)
             and a fulfillment will successfully satisfy its
             condition for any given message.
*/
bool
validate (
    fulfillment_t const& f,
    Slice const& m);

}
}

#endif
