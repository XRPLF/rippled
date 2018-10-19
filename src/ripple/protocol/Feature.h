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

// *NOTE*
//
// Features, or Amendments as they are called elsewhere, are enabled on the
// network at some specific time based on Validator voting.  Features are
// enabled using run-time conditionals based on the state of the amendment.
// There is value in retaining that conditional code for some time after
// the amendment is enabled to make it simple to replay old transactions.
// However, once an Amendment has been enabled for, say, more than two years
// then retaining that conditional code has less value since it is
// uncommon to replay such old transactions.
//
// Starting in January of 2020 Amendment conditionals from before January
// 2018 are being removed.  So replaying any ledger from before January
// 2018 needs to happen on an older version of the server code.  There's
// a log message in Application.cpp that warns about replaying old ledgers.
//
// At some point in the future someone may wish to remove Amendment
// conditional code for Amendments that were enabled after January 2018.
// When that happens then the log message in Application.cpp should be
// updated.

class FeatureCollections
{
    static constexpr char const* const featureNames[] = {
        "MultiSign",      // Unconditionally supported.
        "TrustSetAuth",   // Unconditionally supported.
        "FeeEscalation",  // Unconditionally supported.
        "OwnerPaysFee",
        "PayChan",
        "Flow",  // Unconditionally supported.
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
        "fix1528",
        "DepositAuth",
        "Checks",
        "fix1571",
        "fix1543",
        "fix1623",
        "DepositPreauth",
        "fix1515",
        "fix1578",
        "MultiSignReserve",
        "fixTakerDryOfferRemoval",
        "fixMasterKeyAsRegularKey",
        "fixCheckThreading",
        "fixPayChanRecipientOwnerDir",
        "DeletableAccounts",
        // fixQualityUpperBound should be activated before FlowCross
        "fixQualityUpperBound",
        "RequireFullyCanonicalSig",
        "fix1781",  // XRPEndpointSteps should be included in the circular
                    // payment check
        "HardenedValidations",
        "fixAmendmentMajorityCalc",  // Fix Amendment majority calculation
        "NegativeUNL",
        "TicketBatch"};

    std::vector<uint256> features;
    boost::container::flat_map<uint256, std::size_t> featureToIndex;
    boost::container::flat_map<std::string, uint256> nameToFeature;

public:
    FeatureCollections();

    static constexpr std::size_t
    numFeatures()
    {
        return sizeof(featureNames) / sizeof(featureNames[0]);
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
supportedAmendments();

}  // namespace detail

boost::optional<uint256>
getRegisteredFeature(std::string const& name);

size_t
featureToBitsetIndex(uint256 const& f);

uint256
bitsetIndexToFeature(size_t i);

class FeatureBitset
    : private std::bitset<detail::FeatureCollections::numFeatures()>
{
    using base = std::bitset<detail::FeatureCollections::numFeatures()>;

    template <class... Fs>
    void
    initFromFeatures(uint256 const& f, Fs&&... fs)
    {
        set(f);
        if constexpr (sizeof...(fs) > 0)
            initFromFeatures(std::forward<Fs>(fs)...);
    }

public:
    using base::bitset;
    using base::operator==;
    using base::operator!=;

    using base::all;
    using base::any;
    using base::count;
    using base::flip;
    using base::none;
    using base::reset;
    using base::set;
    using base::size;
    using base::test;
    using base::operator[];
    using base::to_string;
    using base::to_ullong;
    using base::to_ulong;

    FeatureBitset() = default;

    explicit FeatureBitset(base const& b) : base(b)
    {
    }

    template <class... Fs>
    explicit FeatureBitset(uint256 const& f, Fs&&... fs)
    {
        initFromFeatures(f, std::forward<Fs>(fs)...);
    }

    template <class Col>
    explicit FeatureBitset(Col const& fs)
    {
        for (auto const& f : fs)
            set(featureToBitsetIndex(f));
    }

    auto
    operator[](uint256 const& f)
    {
        return base::operator[](featureToBitsetIndex(f));
    }

    auto
    operator[](uint256 const& f) const
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

    FeatureBitset&
    operator&=(FeatureBitset const& rhs)
    {
        base::operator&=(rhs);
        return *this;
    }

    FeatureBitset&
    operator|=(FeatureBitset const& rhs)
    {
        base::operator|=(rhs);
        return *this;
    }

    FeatureBitset
    operator~() const
    {
        return FeatureBitset{base::operator~()};
    }

    friend FeatureBitset
    operator&(FeatureBitset const& lhs, FeatureBitset const& rhs)
    {
        return FeatureBitset{
            static_cast<base const&>(lhs) & static_cast<base const&>(rhs)};
    }

    friend FeatureBitset
    operator&(FeatureBitset const& lhs, uint256 const& rhs)
    {
        return lhs & FeatureBitset{rhs};
    }

    friend FeatureBitset
    operator&(uint256 const& lhs, FeatureBitset const& rhs)
    {
        return FeatureBitset{lhs} & rhs;
    }

    friend FeatureBitset
    operator|(FeatureBitset const& lhs, FeatureBitset const& rhs)
    {
        return FeatureBitset{
            static_cast<base const&>(lhs) | static_cast<base const&>(rhs)};
    }

    friend FeatureBitset
    operator|(FeatureBitset const& lhs, uint256 const& rhs)
    {
        return lhs | FeatureBitset{rhs};
    }

    friend FeatureBitset
    operator|(uint256 const& lhs, FeatureBitset const& rhs)
    {
        return FeatureBitset{lhs} | rhs;
    }

    friend FeatureBitset
    operator^(FeatureBitset const& lhs, FeatureBitset const& rhs)
    {
        return FeatureBitset{
            static_cast<base const&>(lhs) ^ static_cast<base const&>(rhs)};
    }

    friend FeatureBitset
    operator^(FeatureBitset const& lhs, uint256 const& rhs)
    {
        return lhs ^ FeatureBitset { rhs };
    }

    friend FeatureBitset
    operator^(uint256 const& lhs, FeatureBitset const& rhs)
    {
        return FeatureBitset{lhs} ^ rhs;
    }

    // set difference
    friend FeatureBitset
    operator-(FeatureBitset const& lhs, FeatureBitset const& rhs)
    {
        return lhs & ~rhs;
    }

    friend FeatureBitset
    operator-(FeatureBitset const& lhs, uint256 const& rhs)
    {
        return lhs - FeatureBitset{rhs};
    }

    friend FeatureBitset
    operator-(uint256 const& lhs, FeatureBitset const& rhs)
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

extern uint256 const featureOwnerPaysFee;
extern uint256 const featureFlow;
extern uint256 const featureCompareTakerFlowCross;
extern uint256 const featureFlowCross;
extern uint256 const featureCryptoConditionsSuite;
extern uint256 const fix1513;
extern uint256 const featureDepositAuth;
extern uint256 const featureChecks;
extern uint256 const fix1571;
extern uint256 const fix1543;
extern uint256 const fix1623;
extern uint256 const featureDepositPreauth;
extern uint256 const fix1515;
extern uint256 const fix1578;
extern uint256 const featureMultiSignReserve;
extern uint256 const fixTakerDryOfferRemoval;
extern uint256 const fixMasterKeyAsRegularKey;
extern uint256 const fixCheckThreading;
extern uint256 const fixPayChanRecipientOwnerDir;
extern uint256 const featureDeletableAccounts;
extern uint256 const fixQualityUpperBound;
extern uint256 const featureRequireFullyCanonicalSig;
extern uint256 const fix1781;
extern uint256 const featureHardenedValidations;
extern uint256 const fixAmendmentMajorityCalc;
extern uint256 const featureNegativeUNL;
extern uint256 const featureTicketBatch;

}  // namespace ripple

#endif
