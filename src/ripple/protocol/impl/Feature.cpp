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

#include <ripple/basics/contract.h>
#include <ripple/protocol/Feature.h>
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
        h(name, std::strlen(name));
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
detail::supportedAmendments()
{
    // clang-format off
    // Commented out amendments will be supported in a future release (and
    // uncommented at that time).
    //
    // There are also unconditionally supported amendments in the list.
    // Those are amendments that were enabled some time ago and the
    // amendment conditional code has been removed.
    //
    // ** WARNING **
    // Unconditionally supported amendments need to remain in the list.
    // Removing them will cause servers to become amendment blocked.
    static std::vector<std::string> const supported{
        "MultiSign",      // Unconditionally supported.
//        "Tickets",
        "TrustSetAuth",   // Unconditionally supported.
        "FeeEscalation",  // Unconditionally supported.
//        "OwnerPaysFee",
        "PayChan",
        "Flow",
        "CryptoConditions",
        "TickSize",
        "fix1368",
        "Escrow",
        "CryptoConditionsSuite",
        "fix1373",
        "EnforceInvariants",
        "FlowCross",
        "SortedDirectories",
        "fix1201",
        "fix1512",
        "fix1513",
        "fix1523",
        "fix1528",
        "DepositAuth",
        "Checks",
        "fix1571",
        "fix1543",
        "fix1623",
        "DepositPreauth",
        // Use liquidity from strands that consume max offers, but mark as dry
        "fix1515",
        "fix1578",
        "MultiSignReserve",
        "fixTakerDryOfferRemoval",
        "fixMasterKeyAsRegularKey",
        "fixCheckThreading",
        "fixPayChanRecipientOwnerDir",
        "DeletableAccounts",
        "fixQualityUpperBound",
        "RequireFullyCanonicalSig",
        "fix1781",
        "Retire2017Amendments",
    };
    // clang-format on
    return supported;
}

//------------------------------------------------------------------------------

boost::optional<uint256>
getRegisteredFeature(std::string const& name)
{
    return featureCollections.getRegisteredFeature(name);
}

// Used for static initialization.  It's a LogicError if the named feature
// is missing
static uint256
getMandatoryFeature(std::string const& name)
{
    boost::optional<uint256> const optFeatureId = getRegisteredFeature(name);
    if (!optFeatureId)
    {
        LogicError(
            std::string("Requested feature \"") + name +
            "\" is not registered in FeatureCollections::featureName.");
    }
    return *optFeatureId;
}

size_t
featureToBitsetIndex(uint256 const& f)
{
    return featureCollections.featureToBitsetIndex(f);
}

uint256
bitsetIndexToFeature(size_t i)
{
    return featureCollections.bitsetIndexToFeature(i);
}

// clang-format off
uint256 const featureTickets = getMandatoryFeature("Tickets");
uint256 const featureOwnerPaysFee = getMandatoryFeature("OwnerPaysFee");
uint256 const featurePayChan = getMandatoryFeature("PayChan");
uint256 const featureFlow = getMandatoryFeature("Flow");
uint256 const featureCompareTakerFlowCross = getMandatoryFeature("CompareTakerFlowCross");
uint256 const featureFlowCross = getMandatoryFeature("FlowCross");
uint256 const featureCryptoConditions = getMandatoryFeature("CryptoConditions");
uint256 const featureTickSize = getMandatoryFeature("TickSize");
uint256 const fix1368 = getMandatoryFeature("fix1368");
uint256 const featureEscrow = getMandatoryFeature("Escrow");
uint256 const featureCryptoConditionsSuite = getMandatoryFeature("CryptoConditionsSuite");
uint256 const fix1373 = getMandatoryFeature("fix1373");
uint256 const featureEnforceInvariants = getMandatoryFeature("EnforceInvariants");
uint256 const featureSortedDirectories = getMandatoryFeature("SortedDirectories");
uint256 const fix1201 = getMandatoryFeature("fix1201");
uint256 const fix1512 = getMandatoryFeature("fix1512");
uint256 const fix1513 = getMandatoryFeature("fix1513");
uint256 const fix1523 = getMandatoryFeature("fix1523");
uint256 const fix1528 = getMandatoryFeature("fix1528");
uint256 const featureDepositAuth = getMandatoryFeature("DepositAuth");
uint256 const featureChecks = getMandatoryFeature("Checks");
uint256 const fix1571 = getMandatoryFeature("fix1571");
uint256 const fix1543 = getMandatoryFeature("fix1543");
uint256 const fix1623 = getMandatoryFeature("fix1623");
uint256 const featureDepositPreauth = getMandatoryFeature("DepositPreauth");
uint256 const fix1515 = getMandatoryFeature("fix1515");
uint256 const fix1578 = getMandatoryFeature("fix1578");
uint256 const featureMultiSignReserve = getMandatoryFeature("MultiSignReserve");
uint256 const fixTakerDryOfferRemoval = getMandatoryFeature("fixTakerDryOfferRemoval");
uint256 const fixMasterKeyAsRegularKey = getMandatoryFeature("fixMasterKeyAsRegularKey");
uint256 const fixCheckThreading = getMandatoryFeature("fixCheckThreading");
uint256 const fixPayChanRecipientOwnerDir = getMandatoryFeature("fixPayChanRecipientOwnerDir");
uint256 const featureDeletableAccounts = getMandatoryFeature("DeletableAccounts");
uint256 const fixQualityUpperBound = getMandatoryFeature("fixQualityUpperBound");
uint256 const featureRequireFullyCanonicalSig = getMandatoryFeature("RequireFullyCanonicalSig");
uint256 const fix1781 = getMandatoryFeature("fix1781");
uint256 const featureRetire2017Amendments = getMandatoryFeature("Retire2017Amendments");

// The following amendments have been active for at least two years.
// Their pre-amendment code has been removed.
//
// The static retired amendments could hypothetically be moved so they are only
// inside the detail::retiringAmendments() implementation.  However doing so
// would postpone the discovery of any construction problems until the first
// call to retiringAmendments().  By leaving these definitions at file
// scope any run-time build problems will be revealed before main() is called.
static uint256 const retiredFeeEscalation = getMandatoryFeature("FeeEscalation");
static uint256 const retiredMultiSign = getMandatoryFeature("MultiSign");
static uint256 const retiredTrustSetAuth = getMandatoryFeature("TrustSetAuth");
static uint256 const retiredFlow = getMandatoryFeature("Flow");
static uint256 const retiredCryptoConditions = getMandatoryFeature("CryptoConditions");
static uint256 const retiredTickSize = getMandatoryFeature("TickSize");
static uint256 const retiredPayChan = getMandatoryFeature("PayChan");
static uint256 const retiredFix1368 = getMandatoryFeature("fix1368");
static uint256 const retiredEscrow = getMandatoryFeature("Escrow");
static uint256 const retiredFix1373 = getMandatoryFeature("fix1373");
static uint256 const retiredEnforceInvariants = getMandatoryFeature("EnforceInvariants");
static uint256 const retiredSortedDirectories = getMandatoryFeature("SortedDirectories");
static uint256 const retiredFix1528 = getMandatoryFeature("fix1528");
static uint256 const retiredFix1523 = getMandatoryFeature("fix1523");
static uint256 const retiredFix1512 = getMandatoryFeature("fix1512");
static uint256 const retiredFix1201 = getMandatoryFeature("fix1201");
// clang-format on

std::initializer_list<uint256> const&
detail::retiringAmendments()
{
    static std::initializer_list<uint256> const retiring{
        retiredFeeEscalation,
        retiredMultiSign,
        retiredTrustSetAuth,
        retiredFlow,
        retiredCryptoConditions,
        retiredTickSize,
        retiredPayChan,
        retiredFix1368,
        retiredEscrow,
        retiredFix1373,
        retiredEnforceInvariants,
        retiredSortedDirectories,
        retiredFix1528,
        retiredFix1523,
        retiredFix1512,
        retiredFix1201,
    };
    return retiring;
};

}  // namespace ripple
