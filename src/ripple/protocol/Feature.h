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
 *    sha512half value and name (matching exactly the featureName here) to the
 *    supportedAmendments in Amendments.cpp.
 *
 */

namespace ripple {

namespace detail {

class FeatureCollections
{
    static constexpr char const* const featureNames[] =
        {"MultiSign",
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
         "fix1528"};

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

} // detail

boost::optional<uint256>
getRegisteredFeature (std::string const& name);

using FeatureBitset = std::bitset<detail::FeatureCollections::numFeatures()>;

size_t
featureToBitsetIndex(uint256 const& f);

uint256
bitsetIndexToFeature(size_t i);

template <class F>
void
foreachFeature(FeatureBitset bs, F&& f)
{
    for (size_t i = 0; i < bs.size(); ++i)
        if (bs[i])
            f(bitsetIndexToFeature(i));
}

template <class Col>
FeatureBitset
makeFeatureBitset(Col&& fs)
{
    FeatureBitset result;
    for (auto const& f : fs)
        result.set(featureToBitsetIndex(f));
    return result;
}

template <class Col>
FeatureBitset
addFeatures(FeatureBitset bs, Col&& fs)
{
    for (auto const& f : fs)
        bs.set(featureToBitsetIndex(f));
    return bs;
}

template <class Col>
FeatureBitset
removeFeatures(FeatureBitset bs, Col&& fs)
{
    for (auto const& f : fs)
        bs.reset(featureToBitsetIndex(f));
    return bs;
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
extern uint256 const fix1528;

} // ripple

#endif
