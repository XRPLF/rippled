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
#include <ripple/conditions/Condition.h>
#include <ripple/conditions/impl/utils.h>
#include <boost/optional.hpp>

namespace ripple {
namespace cryptoconditions {

struct Fulfillment
{
public:
    virtual ~Fulfillment() = default;

    Fulfillment () = default;

    /** Returns the size of the fulfillment's payload. */
    virtual
    std::size_t
    payloadSize() const = 0;

    /** Returns the fulfillment's payload */
    virtual
    Buffer
    payload() const = 0;

    /** Generates the condition */
    virtual
    Condition
    condition() const = 0;

    /** Returns the type */
    virtual
    std::uint16_t
    type () const = 0;

    /** Returns the features suites required.

        For any given fulfillment, the result includes all
        the feature suites that an implementation must
        support in order to be able to successfully parse
        the fulfillment.

        @note fulfillments of the same type may require
              different features.
    */
    virtual
    std::uint32_t
    features () const = 0;

    /** Determines if this fulfillment is well-formed */
    virtual
    bool
    ok () const = 0;

    /** Validates a fulfillment. */
    virtual
    bool
    validate (Slice data) const = 0;

    /** Parses the fulfillment's payload. */
    virtual
    bool
    parsePayload (Slice s) = 0;
};

inline
bool
operator== (Fulfillment const& lhs, Fulfillment const& rhs)
{
    return
        lhs.type() == rhs.type() &&
        lhs.ok() == rhs.ok() &&
        lhs.payload() == rhs.payload();
}

inline
bool
operator!= (Fulfillment const& lhs, Fulfillment const& rhs)
{
    return !(lhs == rhs);
}

/** Load a fulfillment from its string serialization.

    The format is specified in Section 2.5.1 of the
    cryptoconditions RFC:

        https://tools.ietf.org/html/draft-thomas-crypto-conditions-00#section-2.5.1
 */
std::unique_ptr<Fulfillment>
loadFulfillment (std::string const& s);

/** Load a fulfillment from its binary serialization.

    The format is specified in Section 2.5.2 of the
    cryptoconditions RFC:

        https://tools.ietf.org/html/draft-thomas-crypto-conditions-00#section-2.5.2
*/
std::unique_ptr<Fulfillment>
loadFulfillment (Slice s);

// Convert a fulfillment to its string form
std::string
to_string (Fulfillment const& f);

// Convert a fulfillment to its binary form
std::vector<std::uint8_t>
to_blob (Fulfillment const& f);

/** Determine whether a fulfillment fulfills a given condition */
bool
fulfills (
    Fulfillment const& f,
    Condition const& c);

/** Verify if the given message satisfies the fulfillment.

    @param f The fulfillment
    @param c The condition
    @param m The message; note that the message is not
             relevant for some conditions (e.g. hashlocks)
             and a fulfillment will successfully satisfy its
             condition for any given message.
*/
bool
validate (
    Fulfillment const& f,
    Condition const& c,
    Slice m);

/** Verify a cryptoconditional trigger.

    A cryptoconditional trigger is a cryptocondition with
    an empty message.

    When using such triggers, it is recommended that the
    trigger be of type preimage, prefix or threshold. If
    a signature type is used (i.e. Ed25519 or RSA-SHA256)
    then the Ed25519 or RSA keys should be single-use keys.

    @param f The fulfillment
    @param c The condition
*/
bool
validateTrigger (
    Fulfillment const& f,
    Condition const& c);

}
}

#endif
