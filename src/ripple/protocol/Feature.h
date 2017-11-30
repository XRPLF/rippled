//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_FEATURE_H_INCLUDED
#define RIPPLE_PROTOCOL_FEATURE_H_INCLUDED

#include <ripple/basics/base_uint.h>
#include <boost/container/flat_map.hpp>
#include <boost/optional.hpp>
#include <array>
#include <bitset>
#include <string>

/**
 * @page Feature How to add new features
 *
 * Steps required to add new features to the code:
 *
 * 1) add the new feature name to the featureNames array below
 * 2) add a uint256 declaration for the feature to the bottom of this file
 * 3) add a uint256 definition for the feature to the corresponding source
 *    file (Feature.cpp)
 * 4) if the feature is going to be supported in the near future, add its
 *    sha512half value and name (matching exactly the featureName here) to
 *    the supportedAmendments in Feature.cpp.
 *
 */

namespace ripple {

namespace detail {

class FeatureCollections
{
    static constexpr char const* const featureNames[] =
    {
        "MultiSign",
        "Tickets",
        "TrustSetAuth",
        "FeeEscalation",
        "OwnerPaysFee",
        "CompareFlowV1V2",
        "SHAMapV2",
        "PayChan",
        "Flow",
        "CompareTakerFlowCross",
        "FlowCross",
        "CryptoConditions",
        "TickSize",
        "fix1368",
        "Escrow",
        "CryptoConditionsSuite",
        "fix1373",
        "EnforceInvariants",
        "SortedDirectories",
        "fix1201",
        "fix1512",
        "fix1513",
        "fix1523",
        "fix1528"
    };

    std::vector<uint256> features;
    boost::container::flat_map<uint256, std::size_t> featureToIndex;
    boost::container::flat_map<std::string, uint256> nameToFeature;

public:
    FeatureCollections();

    static constexpr std::size_t numFeatures()
    {
        return sizeof (featureNames) / sizeof (featureNames[0]);
    }

    boost::optional<uint256>
    getRegisteredFeature(std::string const& name) const;

    std::size_t
    featureToBitsetIndex(uint256 const& f) const;

    uint256 const&
    bitsetIndexToFeature(size_t i) const;
};

/** Amendments that this server supports, but doesn't enable by default */
std::vector<std::string> const&
supportedAmendments ();

} // detail

boost::optional<uint256>
getRegisteredFeature (std::string const& name);

size_t
featureToBitsetIndex(uint256 const& f);

uint256
bitsetIndexToFeature(size_t i);

class FeatureBitset
    : private std::bitset<detail::FeatureCollections::numFeatures()>
{
    using base = std::bitset<detail::FeatureCollections::numFeatures()>;

    void initFromFeatures()
    {
    }

    template<class... Fs>
    void initFromFeatures(uint256 const& f, Fs&&... fs)
    {
        set(f);
        initFromFeatures(std::forward<Fs>(fs)...);
    }

public:
    using base::bitset;
    using base::operator==;
    using base::operator!=;
 
    using base::test;
    using base::all;
    using base::any;
    using base::none;
    using base::count;
    using base::size;
    using base::set;
    using base::reset;
    using base::flip;
    using base::operator[];
    using base::to_string;
    using base::to_ulong;
    using base::to_ullong;

    FeatureBitset() = default;

    explicit
    FeatureBitset(base const& b)
        : base(b)
    {
    }

    template<class... Fs>
    explicit
    FeatureBitset(uint256 const& f, Fs&&... fs)
    {
        initFromFeatures(f, std::forward<Fs>(fs)...);
    }

    template <class Col>
    explicit
    FeatureBitset(Col const& fs)
    {
        for (auto const& f : fs)
            set(featureToBitsetIndex(f));
    }

    auto operator[](uint256 const& f)
    {
        return base::operator[](featureToBitsetIndex(f));
    }

    auto operator[](uint256 const& f) const
    {
        return base::operator[](featureToBitsetIndex(f));
    }

    FeatureBitset&
    set(uint256 const& f, bool value = true)
    {
        base::set(featureToBitsetIndex(f), value);
        return *this;
    }

    FeatureBitset&
    reset(uint256 const& f)
    {
        base::reset(featureToBitsetIndex(f));
        return *this;
    }

    FeatureBitset&
    flip(uint256 const& f)
    {
        base::flip(featureToBitsetIndex(f));
        return *this;
    }

    FeatureBitset& operator&=(FeatureBitset const& rhs)
    {
        base::operator&=(rhs);
        return *this;
    }

    FeatureBitset& operator|=(FeatureBitset const& rhs)
    {
        base::operator|=(rhs);
        return *this;
    }

    FeatureBitset operator~() const
    {
        return FeatureBitset{base::operator~()};
    }

    friend
    FeatureBitset operator&(
        FeatureBitset const& lhs,
        FeatureBitset const& rhs)
    {
        return FeatureBitset{static_cast<base const&>(lhs) &
                             static_cast<base const&>(rhs)};
    }

    friend
    FeatureBitset operator&(
        FeatureBitset const& lhs,
        uint256 const& rhs)
    {
        return lhs & FeatureBitset{rhs};
    }

    friend
    FeatureBitset operator&(
        uint256 const& lhs,
        FeatureBitset const& rhs)
    {
        return FeatureBitset{lhs} & rhs;
    }

    friend
    FeatureBitset operator|(
        FeatureBitset const& lhs,
        FeatureBitset const& rhs)
    {
        return FeatureBitset{static_cast<base const&>(lhs) |
                             static_cast<base const&>(rhs)};
    }

    friend
    FeatureBitset operator|(
        FeatureBitset const& lhs,
        uint256 const& rhs)
    {
        return lhs | FeatureBitset{rhs};
    }

    friend
    FeatureBitset operator|(
        uint256 const& lhs,
        FeatureBitset const& rhs)
    {
        return FeatureBitset{lhs} | rhs;
    }

    friend
    FeatureBitset operator^(
        FeatureBitset const& lhs,
        FeatureBitset const& rhs)
    {
        return FeatureBitset{static_cast<base const&>(lhs) ^
                             static_cast<base const&>(rhs)};
    }

    friend
    FeatureBitset operator^(
        FeatureBitset const& lhs,
        uint256 const& rhs)
    {
        return lhs ^ FeatureBitset{rhs};
    }

    friend
    FeatureBitset operator^(
        uint256 const& lhs,
        FeatureBitset const& rhs)
    {
        return FeatureBitset{lhs} ^ rhs;
    }

    // set difference
    friend
    FeatureBitset operator-(
        FeatureBitset const& lhs,
        FeatureBitset const& rhs)
    {
        return lhs & ~rhs;
    }

    friend
    FeatureBitset operator-(
        FeatureBitset const& lhs,
        uint256 const& rhs)
    {
        return lhs - FeatureBitset{rhs};
    }

    friend
    FeatureBitset operator-(
        uint256 const& lhs,
        FeatureBitset const& rhs)
    {
        return FeatureBitset{lhs} - rhs;
    }
};

template <class F>
void
foreachFeature(FeatureBitset bs, F&& f)
{
    for (size_t i = 0; i < bs.size(); ++i)
        if (bs[i])
            f(bitsetIndexToFeature(i));
}

extern uint256 const featureMultiSign;
extern uint256 const featureTickets;
extern uint256 const featureTrustSetAuth;
extern uint256 const featureFeeEscalation;
extern uint256 const featureOwnerPaysFee;
extern uint256 const featureCompareFlowV1V2;
extern uint256 const featureSHAMapV2;
extern uint256 const featurePayChan;
extern uint256 const featureFlow;
extern uint256 const featureCompareTakerFlowCross;
extern uint256 const featureFlowCross;
extern uint256 const featureCryptoConditions;
extern uint256 const featureTickSize;
extern uint256 const fix1368;
extern uint256 const featureEscrow;
extern uint256 const featureCryptoConditionsSuite;
extern uint256 const fix1373;
extern uint256 const featureEnforceInvariants;
extern uint256 const featureSortedDirectories;
extern uint256 const fix1201;
extern uint256 const fix1512;
extern uint256 const fix1513;
extern uint256 const fix1523;
extern uint256 const fix1528;

} // ripple

#endif
