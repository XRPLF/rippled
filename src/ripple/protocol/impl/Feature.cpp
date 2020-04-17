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
        "HardenedValidations"};
    return supported;
}

//------------------------------------------------------------------------------

boost::optional<uint256>
getRegisteredFeature(std::string const& name)
{
    return featureCollections.getRegisteredFeature(name);
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

uint256 const
    featureTickets                  = *getRegisteredFeature("Tickets"),
    featureOwnerPaysFee             = *getRegisteredFeature("OwnerPaysFee"),
    featureFlow                     = *getRegisteredFeature("Flow"),
    featureCompareTakerFlowCross    = *getRegisteredFeature("CompareTakerFlowCross"),
    featureFlowCross                = *getRegisteredFeature("FlowCross"),
    featureCryptoConditionsSuite    = *getRegisteredFeature("CryptoConditionsSuite"),
    fix1513                         = *getRegisteredFeature("fix1513"),
    featureDepositAuth              = *getRegisteredFeature("DepositAuth"),
    featureChecks                   = *getRegisteredFeature("Checks"),
    fix1571                         = *getRegisteredFeature("fix1571"),
    fix1543                         = *getRegisteredFeature("fix1543"),
    fix1623                         = *getRegisteredFeature("fix1623"),
    featureDepositPreauth           = *getRegisteredFeature("DepositPreauth"),
    fix1515                         = *getRegisteredFeature("fix1515"),
    fix1578                         = *getRegisteredFeature("fix1578"),
    featureMultiSignReserve         = *getRegisteredFeature("MultiSignReserve"),
    fixTakerDryOfferRemoval         = *getRegisteredFeature("fixTakerDryOfferRemoval"),
    fixMasterKeyAsRegularKey        = *getRegisteredFeature("fixMasterKeyAsRegularKey"),
    fixCheckThreading               = *getRegisteredFeature("fixCheckThreading"),
    fixPayChanRecipientOwnerDir     = *getRegisteredFeature("fixPayChanRecipientOwnerDir"),
    featureDeletableAccounts        = *getRegisteredFeature("DeletableAccounts"),
    fixQualityUpperBound            = *getRegisteredFeature("fixQualityUpperBound"),
    featureRequireFullyCanonicalSig = *getRegisteredFeature("RequireFullyCanonicalSig"),
    fix1781                         = *getRegisteredFeature("fix1781"),
    featureHardenedValidations      = *getRegisteredFeature("HardenedValidations");

// The following amendments have been active for at least two years. Their
// pre-amendment code has been removed and the identifiers are deprecated.
[[deprecated("The referenced amendment has been retired"), maybe_unused]]
uint256 const
    retiredPayChan           = *getRegisteredFeature("PayChan"),
    retiredCryptoConditions  = *getRegisteredFeature("CryptoConditions"),
    retiredTickSize          = *getRegisteredFeature("TickSize"),
    retiredFix1368           = *getRegisteredFeature("fix1368"),
    retiredEscrow            = *getRegisteredFeature("Escrow"),
    retiredFix1373           = *getRegisteredFeature("fix1373"),
    retiredEnforceInvariants = *getRegisteredFeature("EnforceInvariants"),
    retiredSortedDirectories = *getRegisteredFeature("SortedDirectories"),
    retiredFix1201           = *getRegisteredFeature("fix1201"),
    retiredFix1512           = *getRegisteredFeature("fix1512"),
    retiredFix1523           = *getRegisteredFeature("fix1523"),
    retiredFix1528           = *getRegisteredFeature("fix1528");

// clang-format on

}  // namespace ripple
