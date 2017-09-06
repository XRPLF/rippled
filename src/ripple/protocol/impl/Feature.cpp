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

#include <BeastConfig.h>
#include <ripple/protocol/Feature.h>
#include <ripple/basics/contract.h>
#include <ripple/protocol/digest.h>

#include <cstring>

namespace ripple {

//------------------------------------------------------------------------------

constexpr char const* const detail::FeatureCollections::featureNames[];

detail::FeatureCollections::FeatureCollections()
{
    features.reserve(numFeatures());
    featureToIndex.reserve(numFeatures());
    nameToFeature.reserve(numFeatures());

    for (std::size_t i = 0; i < numFeatures(); ++i)
    {
        auto const name = featureNames[i];
        sha512_half_hasher h;
        h (name, std::strlen (name));
        auto const f = static_cast<uint256>(h);

        features.push_back(f);
        featureToIndex[f] = i;
        nameToFeature[name] = f;
    }
}

boost::optional<uint256>
detail::FeatureCollections::getRegisteredFeature(std::string const& name) const
{
    auto const i = nameToFeature.find(name);
    if (i == nameToFeature.end())
        return boost::none;
    return i->second;
}

size_t
detail::FeatureCollections::featureToBitsetIndex(uint256 const& f) const
{
    auto const i = featureToIndex.find(f);
    if (i == featureToIndex.end())
        LogicError("Invalid Feature ID");
    return i->second;
}

uint256 const&
detail::FeatureCollections::bitsetIndexToFeature(size_t i) const
{
    if (i >= features.size())
        LogicError("Invalid FeatureBitset index");
    return features[i];
}

static detail::FeatureCollections const featureCollections;

//------------------------------------------------------------------------------

boost::optional<uint256>
getRegisteredFeature (std::string const& name)
{
    return featureCollections.getRegisteredFeature(name);
}

size_t featureToBitsetIndex(uint256 const& f)
{
    return featureCollections.featureToBitsetIndex(f);
}

uint256 bitsetIndexToFeature(size_t i)
{
    return featureCollections.bitsetIndexToFeature(i);
}

uint256 const featureMultiSign = *getRegisteredFeature("MultiSign");
uint256 const featureTickets = *getRegisteredFeature("Tickets");
uint256 const featureTrustSetAuth = *getRegisteredFeature("TrustSetAuth");
uint256 const featureFeeEscalation = *getRegisteredFeature("FeeEscalation");
uint256 const featureOwnerPaysFee = *getRegisteredFeature("OwnerPaysFee");
uint256 const featureCompareFlowV1V2 = *getRegisteredFeature("CompareFlowV1V2");
uint256 const featureSHAMapV2 = *getRegisteredFeature("SHAMapV2");
uint256 const featurePayChan = *getRegisteredFeature("PayChan");
uint256 const featureFlow = *getRegisteredFeature("Flow");
uint256 const featureCompareTakerFlowCross = *getRegisteredFeature("CompareTakerFlowCross");
uint256 const featureFlowCross = *getRegisteredFeature("FlowCross");
uint256 const featureCryptoConditions = *getRegisteredFeature("CryptoConditions");
uint256 const featureTickSize = *getRegisteredFeature("TickSize");
uint256 const fix1368 = *getRegisteredFeature("fix1368");
uint256 const featureEscrow = *getRegisteredFeature("Escrow");
uint256 const featureCryptoConditionsSuite = *getRegisteredFeature("CryptoConditionsSuite");
uint256 const fix1373 = *getRegisteredFeature("fix1373");
uint256 const featureEnforceInvariants = *getRegisteredFeature("EnforceInvariants");
uint256 const featureSortedDirectories = *getRegisteredFeature("SortedDirectories");
uint256 const fix1201 = *getRegisteredFeature("fix1201");
uint256 const fix1512 = *getRegisteredFeature("fix1512");
uint256 const fix1528 = *getRegisteredFeature("fix1528");

} // ripple
