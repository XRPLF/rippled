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

/** Amendments that this server supports, but doesn't enable by default */
std::vector<std::string> const&
detail::supportedAmendments ()
{
    // Commented out amendments will be supported in a future release (and
    // uncommented at that time).
    static std::vector<std::string> const supported
    {
//        { "C6970A8B603D8778783B61C0D445C23D1633CCFAEF0D43E7DBCD1521D34BD7C3 SHAMapV2" },
        { "4C97EBA926031A7CF7D7B36FDE3ED66DDA5421192D63DE53FFB46E43B9DC8373 MultiSign" },
//        { "C1B8D934087225F509BEB5A8EC24447854713EE447D277F69545ABFA0E0FD490 Tickets" },
        { "6781F8368C4771B83E8B821D88F580202BCB4228075297B19E4FDC5233F1EFDC TrustSetAuth" },
        { "42426C4D4F1009EE67080A9B7965B44656D7714D104A72F9B4369F97ABF044EE FeeEscalation" },
//        { "9178256A980A86CF3D70D0260A7DA6402AAFE43632FDBCB88037978404188871 OwnerPaysFee" },
        { "08DE7D96082187F6E6578530258C77FAABABE4C20474BDB82F04B021F1A68647 PayChan" },
        { "740352F2412A9909880C23A559FCECEDA3BE2126FED62FC7660D628A06927F11 Flow" },
        { "1562511F573A19AE9BD103B5D6B9E01B3B46805AEC5D3C4805C902B514399146 CryptoConditions" },
        { "532651B4FD58DF8922A49BA101AB3E996E5BFBF95A913B3E392504863E63B164 TickSize" },
        { "E2E6F2866106419B88C50045ACE96368558C345566AC8F2BDF5A5B5587F0E6FA fix1368" },
        { "07D43DCE529B15A10827E5E04943B496762F9A88E3268269D69C44BE49E21104 Escrow" },
        { "86E83A7D2ECE3AD5FA87AB2195AE015C950469ABF0B72EAACED318F74886AE90 CryptoConditionsSuite" },
        { "42EEA5E28A97824821D4EF97081FE36A54E9593C6E4F20CBAE098C69D2E072DC fix1373" },
        { "DC9CA96AEA1DCF83E527D1AFC916EFAF5D27388ECA4060A88817C1238CAEE0BF EnforceInvariants" },
        { "3012E8230864E95A58C60FD61430D7E1B4D3353195F2981DC12B0C7C0950FFAC FlowCross" },
        { "CC5ABAE4F3EC92E94A59B1908C2BE82D2228B6485C00AFF8F22DF930D89C194E SortedDirectories" },
        { "B4D44CC3111ADD964E846FC57760C8B50FFCD5A82C86A72756F6B058DDDF96AD fix1201" },
        { "6C92211186613F9647A89DFFBAB8F94C99D4C7E956D495270789128569177DA1 fix1512" },
        { "67A34F2CF55BFC0F93AACD5B281413176FEE195269FA6D95219A2DF738671172 fix1513" },
        { "B9E739B8296B4A1BB29BE990B17D66E21B62A300A909F25AC55C22D6C72E1F9D fix1523" },
        { "1D3463A5891F9E589C5AE839FFAC4A917CE96197098A1EF22304E1BC5B98A454 fix1528" }
    };
    return supported;
}

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
uint256 const fix1513 = *getRegisteredFeature("fix1513");
uint256 const fix1523 = *getRegisteredFeature("fix1523");
uint256 const fix1528 = *getRegisteredFeature("fix1528");

} // ripple
