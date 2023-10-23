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

#include <ripple/protocol/Feature.h>

#include <ripple/basics/Slice.h>
#include <ripple/basics/contract.h>
#include <ripple/protocol/digest.h>
#include <boost/container_hash/hash.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/key_extractors.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>
#include <cstring>

namespace ripple {

inline std::size_t
hash_value(ripple::uint256 const& feature)
{
    std::size_t seed = 0;
    using namespace boost;
    for (auto const& n : feature)
        hash_combine(seed, n);
    return seed;
}

namespace {

enum class Supported : bool { no = false, yes };

// *NOTE*
//
// Features, or Amendments as they are called elsewhere, are enabled on the
// network at some specific time based on Validator voting.  Features are
// enabled using run-time conditionals based on the state of the amendment.
// There is value in retaining that conditional code for some time after
// the amendment is enabled to make it simple to replay old transactions.
// However, once an amendment has been enabled for, say, more than two years
// then retaining that conditional code has less value since it is
// uncommon to replay such old transactions.
//
// Starting in January of 2020 Amendment conditionals from before January
// 2018 are being removed.  So replaying any ledger from before January
// 2018 needs to happen on an older version of the server code.  There's
// a log message in Application.cpp that warns about replaying old ledgers.
//
// At some point in the future someone may wish to remove amendment
// conditional code for amendments that were enabled after January 2018.
// When that happens then the log message in Application.cpp should be
// updated.
//
// Generally, amendments which introduce new features should be set as
// "VoteBehavior::DefaultNo" whereas in rare cases, amendments that fix
// critical bugs should be set as "VoteBehavior::DefaultYes", if off-chain
// consensus is reached amongst reviewers, validator operators, and other
// participants.

class FeatureCollections
{
    struct Feature
    {
        std::string name;
        uint256 feature;

        Feature() = delete;
        explicit Feature(std::string const& name_, uint256 const& feature_)
            : name(name_), feature(feature_)
        {
        }

        // These structs are used by the `features` multi_index_container to
        // provide access to the features collection by size_t index, string
        // name, and uint256 feature identifier
        struct byIndex
        {
        };
        struct byName
        {
        };
        struct byFeature
        {
        };
    };

    // Intermediate types to help with readability
    template <class tag, typename Type, Type Feature::*PtrToMember>
    using feature_hashed_unique = boost::multi_index::hashed_unique<
        boost::multi_index::tag<tag>,
        boost::multi_index::member<Feature, Type, PtrToMember>>;

    // Intermediate types to help with readability
    using feature_indexing = boost::multi_index::indexed_by<
        boost::multi_index::random_access<
            boost::multi_index::tag<Feature::byIndex>>,
        feature_hashed_unique<Feature::byFeature, uint256, &Feature::feature>,
        feature_hashed_unique<Feature::byName, std::string, &Feature::name>>;

    // This multi_index_container provides access to the features collection by
    // name, index, and uint256 feature identifier
    boost::multi_index::multi_index_container<Feature, feature_indexing>
        features;
    std::map<std::string, VoteBehavior> supported;
    std::size_t upVotes = 0;
    std::size_t downVotes = 0;
    mutable std::atomic<bool> readOnly = false;

    // These helper functions provide access to the features collection by name,
    // index, and uint256 feature identifier, so the details of
    // multi_index_container can be hidden
    Feature const&
    getByIndex(size_t i) const
    {
        if (i >= features.size())
            LogicError("Invalid FeatureBitset index");
        const auto& sequence = features.get<Feature::byIndex>();
        return sequence[i];
    }
    size_t
    getIndex(Feature const& feature) const
    {
        const auto& sequence = features.get<Feature::byIndex>();
        auto const it_to = sequence.iterator_to(feature);
        return it_to - sequence.begin();
    }
    Feature const*
    getByFeature(uint256 const& feature) const
    {
        const auto& feature_index = features.get<Feature::byFeature>();
        auto const feature_it = feature_index.find(feature);
        return feature_it == feature_index.end() ? nullptr : &*feature_it;
    }
    Feature const*
    getByName(std::string const& name) const
    {
        const auto& name_index = features.get<Feature::byName>();
        auto const name_it = name_index.find(name);
        return name_it == name_index.end() ? nullptr : &*name_it;
    }

public:
    FeatureCollections();

    std::optional<uint256>
    getRegisteredFeature(std::string const& name) const;

    uint256
    registerFeature(
        std::string const& name,
        Supported support,
        VoteBehavior vote);

    /** Tell FeatureCollections when registration is complete. */
    bool
    registrationIsDone();

    std::size_t
    featureToBitsetIndex(uint256 const& f) const;

    uint256 const&
    bitsetIndexToFeature(size_t i) const;

    std::string
    featureToName(uint256 const& f) const;

    /** Amendments that this server supports.
    Whether they are enabled depends on the Rules defined in the validated
    ledger */
    std::map<std::string, VoteBehavior> const&
    supportedAmendments() const
    {
        return supported;
    }

    /** Amendments that this server WON'T vote for by default. */
    std::size_t
    numDownVotedAmendments() const
    {
        return downVotes;
    }

    /** Amendments that this server WILL vote for by default. */
    std::size_t
    numUpVotedAmendments() const
    {
        return upVotes;
    }
};

//------------------------------------------------------------------------------

FeatureCollections::FeatureCollections()
{
    features.reserve(ripple::detail::numFeatures);
}

std::optional<uint256>
FeatureCollections::getRegisteredFeature(std::string const& name) const
{
    assert(readOnly);
    Feature const* feature = getByName(name);
    if (feature)
        return feature->feature;
    return std::nullopt;
}

void
check(bool condition, const char* logicErrorMessage)
{
    if (!condition)
        LogicError(logicErrorMessage);
}

uint256
FeatureCollections::registerFeature(
    std::string const& name,
    Supported support,
    VoteBehavior vote)
{
    check(!readOnly, "Attempting to register a feature after startup.");
    check(
        support == Supported::yes || vote == VoteBehavior::DefaultNo,
        "Invalid feature parameters. Must be supported to be up-voted.");
    Feature const* i = getByName(name);
    if (!i)
    {
        // If this check fails, and you just added a feature, increase the
        // numFeatures value in Feature.h
        check(
            features.size() < detail::numFeatures,
            "More features defined than allocated. Adjust numFeatures in "
            "Feature.h.");

        auto const f = sha512Half(Slice(name.data(), name.size()));

        features.emplace_back(name, f);

        if (support == Supported::yes)
        {
            supported.emplace(name, vote);

            if (vote == VoteBehavior::DefaultYes)
                ++upVotes;
            else
                ++downVotes;
        }
        check(
            upVotes + downVotes == supported.size(),
            "Feature counting logic broke");
        check(
            supported.size() <= features.size(),
            "More supported features than defined features");
        return f;
    }
    else
        // Each feature should only be registered once
        LogicError("Duplicate feature registration");
}

/** Tell FeatureCollections when registration is complete. */
bool
FeatureCollections::registrationIsDone()
{
    readOnly = true;
    return true;
}

size_t
FeatureCollections::featureToBitsetIndex(uint256 const& f) const
{
    assert(readOnly);

    Feature const* feature = getByFeature(f);
    if (!feature)
        LogicError("Invalid Feature ID");

    return getIndex(*feature);
}

uint256 const&
FeatureCollections::bitsetIndexToFeature(size_t i) const
{
    assert(readOnly);
    Feature const& feature = getByIndex(i);
    return feature.feature;
}

std::string
FeatureCollections::featureToName(uint256 const& f) const
{
    assert(readOnly);
    Feature const* feature = getByFeature(f);
    return feature ? feature->name : to_string(f);
}

static FeatureCollections featureCollections;

}  // namespace

/** Amendments that this server supports.
   Whether they are enabled depends on the Rules defined in the validated
   ledger */
std::map<std::string, VoteBehavior> const&
detail::supportedAmendments()
{
    return featureCollections.supportedAmendments();
}

/** Amendments that this server won't vote for by default. */
std::size_t
detail::numDownVotedAmendments()
{
    return featureCollections.numDownVotedAmendments();
}

/** Amendments that this server will vote for by default. */
std::size_t
detail::numUpVotedAmendments()
{
    return featureCollections.numUpVotedAmendments();
}

//------------------------------------------------------------------------------

std::optional<uint256>
getRegisteredFeature(std::string const& name)
{
    return featureCollections.getRegisteredFeature(name);
}

uint256
registerFeature(std::string const& name, Supported support, VoteBehavior vote)
{
    return featureCollections.registerFeature(name, support, vote);
}

// Retired features are in the ledger and have no code controlled by the
// feature. They need to be supported, but do not need to be voted on.
uint256
retireFeature(std::string const& name)
{
    return registerFeature(name, Supported::yes, VoteBehavior::Obsolete);
}

/** Tell FeatureCollections when registration is complete. */
bool
registrationIsDone()
{
    return featureCollections.registrationIsDone();
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

std::string
featureToName(uint256 const& f)
{
    return featureCollections.featureToName(f);
}

#pragma push_macro("REGISTER_FEATURE")
#undef REGISTER_FEATURE

/**
Takes the name of a feature, whether it's supported, and the default vote. Will
register the feature, and create a variable whose name is "feature" plus the
feature name.
*/
#define REGISTER_FEATURE(fName, supported, votebehavior) \
    uint256 const feature##fName =                       \
        registerFeature(#fName, supported, votebehavior)

#pragma push_macro("REGISTER_FIX")
#undef REGISTER_FIX

/**
Takes the name of a feature, whether it's supported, and the default vote. Will
register the feature, and create a variable whose name is the unmodified feature
name.
*/
#define REGISTER_FIX(fName, supported, votebehavior) \
    uint256 const fName = registerFeature(#fName, supported, votebehavior)

// clang-format off

// All known amendments must be registered either here or below with the
// "retired" amendments
REGISTER_FEATURE(OwnerPaysFee,                  Supported::no,  VoteBehavior::DefaultNo);
REGISTER_FEATURE(Flow,                          Supported::yes, VoteBehavior::DefaultYes);
REGISTER_FEATURE(FlowCross,                     Supported::yes, VoteBehavior::DefaultYes);
REGISTER_FIX    (fix1513,                       Supported::yes, VoteBehavior::DefaultYes);
REGISTER_FEATURE(DepositAuth,                   Supported::yes, VoteBehavior::DefaultYes);
REGISTER_FEATURE(Checks,                        Supported::yes, VoteBehavior::DefaultYes);
REGISTER_FIX    (fix1571,                       Supported::yes, VoteBehavior::DefaultYes);
REGISTER_FIX    (fix1543,                       Supported::yes, VoteBehavior::DefaultYes);
REGISTER_FIX    (fix1623,                       Supported::yes, VoteBehavior::DefaultYes);
REGISTER_FEATURE(DepositPreauth,                Supported::yes, VoteBehavior::DefaultYes);
// Use liquidity from strands that consume max offers, but mark as dry
REGISTER_FIX    (fix1515,                       Supported::yes, VoteBehavior::DefaultYes);
REGISTER_FIX    (fix1578,                       Supported::yes, VoteBehavior::DefaultYes);
REGISTER_FEATURE(MultiSignReserve,              Supported::yes, VoteBehavior::DefaultYes);
REGISTER_FIX    (fixTakerDryOfferRemoval,       Supported::yes, VoteBehavior::DefaultYes);
REGISTER_FIX    (fixMasterKeyAsRegularKey,      Supported::yes, VoteBehavior::DefaultYes);
REGISTER_FIX    (fixCheckThreading,             Supported::yes, VoteBehavior::DefaultYes);
REGISTER_FIX    (fixPayChanRecipientOwnerDir,   Supported::yes, VoteBehavior::DefaultYes);
REGISTER_FEATURE(DeletableAccounts,             Supported::yes, VoteBehavior::DefaultYes);
// fixQualityUpperBound should be activated before FlowCross
REGISTER_FIX    (fixQualityUpperBound,          Supported::yes, VoteBehavior::DefaultYes);
REGISTER_FEATURE(RequireFullyCanonicalSig,      Supported::yes, VoteBehavior::DefaultYes);
// fix1781: XRPEndpointSteps should be included in the circular payment check
REGISTER_FIX    (fix1781,                       Supported::yes, VoteBehavior::DefaultYes);
REGISTER_FEATURE(HardenedValidations,           Supported::yes, VoteBehavior::DefaultYes);
REGISTER_FIX    (fixAmendmentMajorityCalc,      Supported::yes, VoteBehavior::DefaultYes);
REGISTER_FEATURE(NegativeUNL,                   Supported::yes, VoteBehavior::DefaultYes);
REGISTER_FEATURE(TicketBatch,                   Supported::yes, VoteBehavior::DefaultYes);
REGISTER_FEATURE(FlowSortStrands,               Supported::yes, VoteBehavior::DefaultYes);
REGISTER_FIX    (fixSTAmountCanonicalize,       Supported::yes, VoteBehavior::DefaultYes);
REGISTER_FIX    (fixRmSmallIncreasedQOffers,    Supported::yes, VoteBehavior::DefaultYes);
REGISTER_FEATURE(CheckCashMakesTrustLine,       Supported::yes, VoteBehavior::DefaultNo);
REGISTER_FEATURE(ExpandedSignerList,            Supported::yes, VoteBehavior::DefaultNo);
REGISTER_FEATURE(NonFungibleTokensV1_1,         Supported::yes, VoteBehavior::DefaultNo);
REGISTER_FIX    (fixTrustLinesToSelf,           Supported::yes, VoteBehavior::DefaultNo);
REGISTER_FIX    (fixRemoveNFTokenAutoTrustLine, Supported::yes, VoteBehavior::DefaultYes);
REGISTER_FEATURE(ImmediateOfferKilled,          Supported::yes, VoteBehavior::DefaultNo);
REGISTER_FEATURE(DisallowIncoming,              Supported::yes, VoteBehavior::DefaultNo);
REGISTER_FEATURE(XRPFees,                       Supported::yes, VoteBehavior::DefaultNo);
REGISTER_FIX    (fixUniversalNumber,            Supported::yes, VoteBehavior::DefaultNo);
REGISTER_FIX    (fixNonFungibleTokensV1_2,      Supported::yes, VoteBehavior::DefaultNo);
REGISTER_FIX    (fixNFTokenRemint,              Supported::yes, VoteBehavior::DefaultNo);
REGISTER_FIX    (fixReducedOffersV1,            Supported::yes, VoteBehavior::DefaultNo);
REGISTER_FEATURE(Clawback,                      Supported::yes, VoteBehavior::DefaultNo);
REGISTER_FEATURE(AMM,                           Supported::yes, VoteBehavior::DefaultNo);
REGISTER_FEATURE(XChainBridge,                  Supported::yes, VoteBehavior::DefaultNo);
REGISTER_FIX    (fixDisallowIncomingV1,         Supported::yes, VoteBehavior::DefaultNo);
REGISTER_FEATURE(DID,                           Supported::yes, VoteBehavior::DefaultNo);

// The following amendments are obsolete, but must remain supported
// because they could potentially get enabled.
//
// Obsolete features are (usually) not in the ledger, and may have code
// controlled by the feature. They need to be supported because at some
// time in the past, the feature was supported and votable, but never
// passed. So the feature needs to be supported in case it is ever
// enabled (added to the ledger).
//
// If a feature remains obsolete for long enough that no clients are able
// to vote for it, the feature can be removed (entirely?) from the code.
REGISTER_FEATURE(CryptoConditionsSuite, Supported::yes, VoteBehavior::Obsolete);
REGISTER_FEATURE(NonFungibleTokensV1,   Supported::yes, VoteBehavior::Obsolete);
REGISTER_FIX    (fixNFTokenDirV1,       Supported::yes, VoteBehavior::Obsolete);
REGISTER_FIX    (fixNFTokenNegOffer,    Supported::yes, VoteBehavior::Obsolete);

// The following amendments have been active for at least two years. Their
// pre-amendment code has been removed and the identifiers are deprecated.
// All known amendments and amendments that may appear in a validated
// ledger must be registered either here or above with the "active" amendments
[[deprecated("The referenced amendment has been retired"), maybe_unused]]
uint256 const
    retiredMultiSign         = retireFeature("MultiSign"),
    retiredTrustSetAuth      = retireFeature("TrustSetAuth"),
    retiredFeeEscalation     = retireFeature("FeeEscalation"),
    retiredPayChan           = retireFeature("PayChan"),
    retiredCryptoConditions  = retireFeature("CryptoConditions"),
    retiredTickSize          = retireFeature("TickSize"),
    retiredFix1368           = retireFeature("fix1368"),
    retiredEscrow            = retireFeature("Escrow"),
    retiredFix1373           = retireFeature("fix1373"),
    retiredEnforceInvariants = retireFeature("EnforceInvariants"),
    retiredSortedDirectories = retireFeature("SortedDirectories"),
    retiredFix1201           = retireFeature("fix1201"),
    retiredFix1512           = retireFeature("fix1512"),
    retiredFix1523           = retireFeature("fix1523"),
    retiredFix1528           = retireFeature("fix1528");

// clang-format on

#undef REGISTER_FIX
#pragma pop_macro("REGISTER_FIX")

#undef REGISTER_FEATURE
#pragma pop_macro("REGISTER_FEATURE")

// All of the features should now be registered, since variables in a cpp file
// are initialized from top to bottom.
//
// Use initialization of one final static variable to set
// featureCollections::readOnly.
[[maybe_unused]] static const bool readOnlySet =
    featureCollections.registrationIsDone();

}  // namespace ripple
