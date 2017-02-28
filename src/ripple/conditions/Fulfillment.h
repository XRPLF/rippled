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
    /** The largest binary fulfillment we support.

        @note This value will be increased in the future, but it
              must never decrease, as that could cause fulfillments
              that were previously considered valid to no longer
              be allowed.
    */
    static constexpr std::size_t maxSerializedFulfillment = 256;

    /** Load a fulfillment from its binary form

        @param s The buffer containing the fulfillment to load.
        @param ec Set to the error, if any occurred.

        The binary format for a fulfillment is specified in the
        cryptoconditions RFC. See:

        https://tools.ietf.org/html/draft-thomas-crypto-conditions-02#section-7.3
    */
    static
    std::unique_ptr<Fulfillment>
    deserialize(
        Slice s,
        std::error_code& ec);

public:
    virtual ~Fulfillment() = default;

    /** Returns the fulfillment's fingerprint:
    
        The fingerprint is an octet string uniquely
        representing this fulfillment's condition
        with respect to other conditions of the
        same type.
   */
    virtual
    Buffer
    fingerprint() const = 0;

    /** Returns the type of this condition. */
    virtual
    Type
    type () const = 0;

    /** Validates a fulfillment. */
    virtual
    bool
    validate (Slice data) const = 0;

    /** Calculates the cost associated with this fulfillment. *

        The cost function is deterministic and depends on the
        type and properties of the condition and the fulfillment
        that the condition is generated from.
    */
    virtual
    std::uint32_t
    cost() const = 0;

    /** Returns the condition associated with the given fulfillment.

        This process is completely deterministic. All implementations
        will, if compliant, produce the identical condition for the
        same fulfillment.
    */
    virtual
    Condition
    condition() const = 0;
};

inline
bool
operator== (Fulfillment const& lhs, Fulfillment const& rhs)
{
    // FIXME: for compound conditions, need to also check subtypes
    return
        lhs.type() == rhs.type() &&
            lhs.cost() == rhs.cost() &&
                lhs.fingerprint() == rhs.fingerprint();
}

inline
bool
operator!= (Fulfillment const& lhs, Fulfillment const& rhs)
{
    return !(lhs == rhs);
}

/** Determine whether the given fulfillment and condition match */
bool
match (
    Fulfillment const& f,
    Condition const& c);

/** Verify if the given message satisfies the fulfillment.

    @param f The fulfillment
    @param c The condition
    @param m The message
    
    @note the message is not relevant for some conditions
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
validate (
    Fulfillment const& f,
    Condition const& c);

}
}

#endif
