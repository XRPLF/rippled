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
#include <ripple/conditions/Types.h>
#include <ripple/conditions/impl/Der.h>

#include <boost/optional.hpp>

#include <array>
#include <bitset>
#include <system_error>
#include <tuple>

namespace ripple {
namespace cryptoconditions {

class Condition
{
public:
    /** The largest binary condition we support.

        @note This value will be increased in the future, but it
              must never decrease, as that could cause conditions
              that were previously considered valid to no longer
              be allowed.
    */
    static constexpr std::size_t maxSerializedCondition = 128;

    /** Load a condition from its binary form

        @param s The slice containing the condition to load.
        @param ec Set to the error, if any occurred.

        The binary format for a condition is specified in the
        cryptoconditions RFC. See:

        https://tools.ietf.org/html/draft-thomas-crypto-conditions-02#section-7.2
    */
    static
    Condition
    deserialize(Slice s, std::error_code& ec);

    static
    bool
    isCompoundCondition(Type t);
public:
    Type type;

    /** An identifier for this condition.

        This fingerprint is meant to be unique only with
        respect to other conditions of the same type.
    */
    std::array<std::uint8_t, 32> fingerprint;

    /** The cost associated with this condition. */
    std::uint32_t cost;

    /** For compound conditions, set of conditions includes */
    std::bitset<5> subtypes;

    Condition(
        Type t,
        std::uint32_t c,
        std::array<std::uint8_t, 32> const& fp,
        std::bitset<5> const& s = {})
        : type(t), fingerprint(fp), cost(c), subtypes(s)
    {
    }

    Condition(Condition const&) = default;
    Condition(Condition&&) = default;

    ~Condition() = default;


    // A default constructor is needed to serialize a vector on conditions - as
    // needed for the threshold condition.
    Condition() = default;

    /// Construct for DER serialization
    explicit
    Condition(der::Constructor const&);

    template<class F>
    void
    withTuple(F&& f, der::TraitsCache& traitsCache)
    {
        if (isCompoundCondition(type))
            f(std::tie(fingerprint, cost, subtypes));
        else
            f(std::tie(fingerprint, cost));
    }

    template<class F>
    void
    withTuple(F&& f, der::TraitsCache& traitsCache) const
    {
        const_cast<Condition*>(this)->withTuple(
            std::forward<F>(f), traitsCache);
    }

    /** Return the subtypes that this type depends on, including this type.

        @see {@link #subtypes}
     */
    std::bitset<5>
    selfAndSubtypes() const;
};

/// compare two conditions for equality
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

/// compare two conditions for inequality
inline
bool
operator!= (Condition const& lhs, Condition const& rhs)
{
    return !(lhs == rhs);
}

/** DerCoderTraits for Condition

    Condition will be coded in ASN.1 as a choice. The actual
    choice will depend on if the condition is a compound condition
    or not.

    @see {@link #DerCoderTraits}
*/
namespace der {
template <>
struct DerCoderTraits<Condition>
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
    tagNum(Condition const& f)
    {
        return static_cast<std::uint8_t>(f.type);
    }
    constexpr static bool primitive(){return false;}

    static void
    encode(Encoder& encoder, Condition const& c);

    static
    void
    decode(Decoder& decoder, Condition& v);

    static
    std::uint64_t
    length(
        Condition const& v,
        boost::optional<GroupType> const& parentGroupType,
        TagMode encoderTagMode,
        TraitsCache& traitsCache);

    static
    int
    compare(
        Condition const& lhs,
        Condition const& rhs,
        TraitsCache& traitsCache);
};
} // der
} // cryptoconditions
} // ripple

#endif
