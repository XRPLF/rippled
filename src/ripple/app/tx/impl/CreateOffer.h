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

#ifndef RIPPLE_TX_CREATEOFFER_H_INCLUDED
#define RIPPLE_TX_CREATEOFFER_H_INCLUDED

#include <ripple/app/tx/impl/OfferStream.h>
#include <ripple/app/tx/impl/Taker.h>
#include <ripple/app/tx/impl/Transactor.h>
#include <utility>

namespace ripple {

class PaymentSandbox;
class Sandbox;

/** Transactor specialized for creating offers in the ledger. */
class CreateOffer : public Transactor
{
public:
    /** Construct a Transactor subclass that creates an offer in the ledger. */
    explicit CreateOffer(ApplyContext& ctx)
        : Transactor(ctx), stepCounter_(1000, j_)
    {
    }

    /** Override default behavior provided by Transactor base class. */
    static XRPAmount
    calculateMaxSpend(STTx const& tx);

    /** Enforce constraints beyond those of the Transactor base class. */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Enforce constraints beyond those of the Transactor base class. */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Gather information beyond what the Transactor base class gathers. */
    void
    preCompute() override;

    /** Precondition: fee collection is likely.  Attempt to create the offer. */
    TER
    doApply() override;

private:
    std::pair<TER, bool>
    applyGuts(Sandbox& view, Sandbox& view_cancel);

    // Determine if we are authorized to hold the asset we want to get.
    static TER
    checkAcceptAsset(
        ReadView const& view,
        ApplyFlags const flags,
        AccountID const id,
        beast::Journal const j,
        Issue const& issue);

    bool
    dry_offer(ApplyView& view, Offer const& offer);

    static std::pair<bool, Quality>
    select_path(
        bool have_direct,
        OfferStream const& direct,
        bool have_bridge,
        OfferStream const& leg1,
        OfferStream const& leg2);

    std::pair<TER, Amounts>
    bridged_cross(
        Taker& taker,
        ApplyView& view,
        ApplyView& view_cancel,
        NetClock::time_point const when);

    std::pair<TER, Amounts>
    direct_cross(
        Taker& taker,
        ApplyView& view,
        ApplyView& view_cancel,
        NetClock::time_point const when);

    // Step through the stream for as long as possible, skipping any offers
    // that are from the taker or which cross the taker's threshold.
    // Return false if the is no offer in the book, true otherwise.
    static bool
    step_account(OfferStream& stream, Taker const& taker);

    // True if the number of offers that have been crossed
    // exceeds the limit.
    bool
    reachedOfferCrossingLimit(Taker const& taker) const;

    // Fill offer as much as possible by consuming offers already on the books,
    // and adjusting account balances accordingly.
    //
    // Charges fees on top to taker.
    std::pair<TER, Amounts>
    takerCross(Sandbox& sb, Sandbox& sbCancel, Amounts const& takerAmount);

    // Use the payment flow code to perform offer crossing.
    std::pair<TER, Amounts>
    flowCross(
        PaymentSandbox& psb,
        PaymentSandbox& psbCancel,
        Amounts const& takerAmount);

    // Temporary
    // This is a central location that invokes both versions of cross
    // so the results can be compared.  Eventually this layer will be
    // removed once flowCross is determined to be stable.
    std::pair<TER, Amounts>
    cross(Sandbox& sb, Sandbox& sbCancel, Amounts const& takerAmount);

    static std::string
    format_amount(STAmount const& amount);

private:
    // What kind of offer we are placing
    CrossType cross_type_;

    // The number of steps to take through order books while crossing
    OfferStream::StepCounter stepCounter_;
};

}  // namespace ripple

#endif
