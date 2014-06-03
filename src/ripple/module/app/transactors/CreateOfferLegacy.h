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

#ifndef RIPPLE_TX_CLASSIC_OFFERCREATE_H_INCLUDED
#define RIPPLE_TX_CLASSIC_OFFERCREATE_H_INCLUDED

#include <unordered_set>

namespace ripple {

class CreateOfferLegacy
    : public CreateOffer
{
public:
    CreateOfferLegacy (
        SerializedTransaction const& txn,
        TransactionEngineParams params,
        TransactionEngine* engine)
        : CreateOffer (
            txn,
            params,
            engine)
    {

    }

    TER doApply () override;

    virtual std::pair<TER, core::Amounts> crossOffers (
        core::LedgerView& view,
        core::Amounts const& taker_amount)
    {
        return std::make_pair (tesSUCCESS, core::Amounts ());
    }

private:
    bool isValidOffer (
        SLE::ref            sleOfferDir,
        uint160 const&      uOfferOwnerID,
        STAmount const&     saOfferPays,
        STAmount const&     saOfferGets,
        uint160 const&      uTakerAccountID,
        std::unordered_set<uint256, beast::hardened_hash<uint256>>&  usOfferUnfundedBecame,
        std::unordered_set<uint160, beast::hardened_hash<uint160>>&  usAccountTouched,
        STAmount&           saOfferFunds);

    bool applyOffer (
        const bool bSell,
        const std::uint32_t uTakerPaysRate, const std::uint32_t uOfferPaysRate,
        const STAmount& saOfferRate,
        const STAmount& saOfferFunds, const STAmount& saTakerFunds,
        const STAmount& saOfferPays, const STAmount& saOfferGets,
        const STAmount& saTakerPays, const STAmount& saTakerGets,
        STAmount& saTakerPaid, STAmount& saTakerGot,
        STAmount& saTakerIssuerFee, STAmount& saOfferIssuerFee);

    bool canCross (
        STAmount const& saTakerFunds,
        STAmount const& saSubTakerPays,
        STAmount const& saSubTakerGets,
        std::uint64_t uTipQuality,
        std::uint64_t uTakeQuality,
        bool isPassive,
        bool& isUnfunded,
        TER& terResult) const;
        
    TER takeOffers (
        bool const bOpenLedger,
        bool const bPassive,
        bool const bSell,
        uint256 const&      uBookBase,
        uint160 const&      uTakerAccountID,
        SLE::ref            sleTakerAccount,
        STAmount const&     saTakerPays,
        STAmount const&     saTakerGets,
        STAmount&           saTakerPaid,
        STAmount&           saTakerGot,
        bool&               bUnfunded);

    // Offers found unfunded.
    std::unordered_set<uint256, beast::hardened_hash<uint256>> usOfferUnfundedFound;

    typedef std::pair <uint256, uint256> missingOffer_t;
    std::set<missingOffer_t> usMissingOffers;
};

}

#endif
