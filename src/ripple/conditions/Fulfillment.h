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
#include <ripple/conditions/impl/Der.h>

#include <boost/optional.hpp>

#include <array>
#include <memory>
#include <system_error>

namespace ripple {
namespace cryptoconditions {

struct Fulfillment
{
    friend class ConditionsTestBase;
    friend class ThresholdSha256;
    friend class PrefixSha256;
public:
    /** The largest binary fulfillment we support.

        @note This value will be increased in the future, but it
              must never decrease, as that could cause fulfillments
              that were previously considered valid to no longer
              be allowed.
    */
    static constexpr std::size_t maxSerializedFulfillment = 4096;

    /** Load a fulfillment from its binary form

        @param s The slice containing the fulfillment to load.
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

protected:
    /** encode the contents used to calculate a fingerprint

        @note Most cryptoconditions (excepting preimage) calculate their
              fingerprints by encoding into a ans.1 DER format and hashing the
              contents of that encoding. This function encodes the contents that will be
              hashed. It does not encode the hash itself.
     */
    virtual
    void
    encodeFingerprint(der::Encoder&) const = 0;

    /** FOR TEST CODE ONLY return true if the fulfillment is equal to the given
        fulfillment. Non-test code should use operator==

        @note This uses an inefficient algorithm for comparison. Threshold is
              particular problematic. This should be used for TESTING ONLY!!!
    */
    virtual
    bool
    checkEqualForTesting(Fulfillment const& rhs) const = 0;

    /** FOR TEST CODE ONLY return true if the fulfillment depends on the message.

        @note Preimage does not depend on the message. So any fulfillment where
              all the "leaf" fulfillments are preimage would not depend on the
              message, all others would.
     */
    virtual
    bool
    validationDependsOnMessage() const = 0;
public:
    virtual ~Fulfillment() = default;

    /** Returns the fulfillment's fingerprint:
    
        The fingerprint is an octet string uniquely
        representing this fulfillment's condition
        with respect to other conditions of the
        same type.
   */
    virtual
    std::array<std::uint8_t, 32>
    fingerprint(std::error_code& ec) const = 0;

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

    /** Returns the subtypes that this fulfillment depends on.

        @note This never including the current type, even if the current type
        recursively depends on itself (i.e. a prefix that has a prefix as a
        subcondition will not include the prefix type as a subtype. @see {@link
        #selfAndSubtypes}
     */
    virtual
    std::bitset<5>
    subtypes() const = 0;

    /** Return the subtypes that this type depends on, including this type.

        @see {@link #subtypes}
     */
    std::bitset<5>
    selfAndSubtypes() const;

    /** Returns the condition associated with the given fulfillment.

        This process is completely deterministic. All implementations
        will, if compliant, produce the identical condition for the
        same fulfillment.
    */
    Condition
    condition(std::error_code& ec) const;

    /// serialize the fulfillment into the ASN.1 DER encoder
    virtual
    void
    encode(der::Encoder&) const = 0;

    /// deserialize from the ASN.1 decoder into this object
    virtual
    void
    decode(der::Decoder&) = 0;

    /// return the size in bytes of the content when encoded (does not include the size of the preamble)
    virtual
    std::uint64_t
    derEncodedLength(
        boost::optional<der::GroupType> const& parentGroupType,
        der::TagMode encoderTagMode,
        der::TraitsCache& traitsCache) const = 0;

    /** compare two fulfillments for sorting in a DER set

        @return <0 if less, 0 if equal, >0 if greater
    */
    virtual
    int
    compare(Fulfillment const& rhs, der::TraitsCache& traitsCache) const = 0;
};

/// compare two fulfillments for equality
inline
bool
operator== (Fulfillment const& lhs, Fulfillment const& rhs)
{
    std::error_code ec1, ec2;
    auto const result =
        lhs.selfAndSubtypes() == rhs.selfAndSubtypes() &&
            lhs.cost() == rhs.cost() &&
                lhs.fingerprint(ec1) == rhs.fingerprint(ec2);
    if (ec1 || ec2)
    {
        // can not compare if there is an error encoding the fingerprint
        return false;
    }
    return result;
}

/// compare two fulfillments for inequality
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


/** DerCoderTraits for std::unique_ptr<Fulfillment>

    std::unique_ptr<Fulfillment> will be coded in ASN.1 as a choice. The actual
    choice will depend on the concrete type of the Fulfillment (preimage,
    prefix, ect...)

    @see {@link #DerCoderTraits}
*/
namespace der {
template <>
struct DerCoderTraits<std::unique_ptr<Fulfillment>>
{
    constexpr static GroupType
    groupType()
    {
        return GroupType::choice;
    }
    constexpr static ClassId classId(){return ClassId::contextSpecific;}
    static boost::optional<std::uint8_t> const&
    tagNum()
    {
        static boost::optional<std::uint8_t> tn;
        return tn;
    }
    static std::uint8_t
    tagNum(std::unique_ptr<Fulfillment> const& f)
    {
        assert(f);
        return static_cast<std::uint8_t>(f->type());
    }
    constexpr static bool primitive(){return false;}

    static void
    encode(Encoder& encoder, std::unique_ptr<Fulfillment> const& f);

    static
    void
    decode(Decoder& decoder, std::unique_ptr<Fulfillment>& v);

    static
    std::uint64_t
    length(
        std::unique_ptr<Fulfillment> const& v,
        boost::optional<GroupType> const& parentGroupType,
        TagMode encoderTagMode, TraitsCache& traitsCache);

    static
    int
    compare(
        std::unique_ptr<Fulfillment> const& lhs,
        std::unique_ptr<Fulfillment> const& rhs,
        TraitsCache& traitsCache)
    {
        return lhs->compare(*rhs, traitsCache);
    }
};

} // der
} // cryptconditions
} // ripple

#endif
