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

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/tx/impl/OfferStream.h>
#include <ripple/app/tx/impl/Taker.h>
#include <ripple/app/tx/impl/Transactor.h>
#include <ripple/protocol/Quality.h>
#include <ripple/basics/Log.h>
#include <ripple/json/to_string.h>
#include <beast/utility/Journal.h>
#include <beast/utility/WrappedSink.h>
#include <memory>
#include <stdexcept>
#include <utility>

namespace ripple {

class CreateOffer
    : public Transactor
{
public:
    CreateOffer (ApplyContext& ctx)
        : Transactor(ctx)
        , stepCounter_ (1000, j_)
    {
    }

    static
    TER
    preflight (PreflightContext const& ctx);

    static
    TER
    preclaim(PreclaimContext const& ctx);

    void
    preCompute() override;

    std::pair<TER, bool>
    applyGuts (ApplyView& view, ApplyView& view_cancel);

    TER
    doApply() override;

private:
    /** Determine if we are authorized to hold the asset we want to get */
    static
    TER
    checkAcceptAsset(ReadView const& view,
        ApplyFlags const flags, AccountID const id,
            beast::Journal const j, Issue const& issue);

    bool
    dry_offer (ApplyView& view, Offer const& offer);

    static
    std::pair<bool, Quality>
    select_path (
        bool have_direct, OfferStream const& direct,
        bool have_bridge, OfferStream const& leg1, OfferStream const& leg2,
        STAmountCalcSwitchovers const& amountCalcSwitchovers);

    std::pair<TER, Amounts>
    bridged_cross (
        Taker& taker,
        ApplyView& view,
        ApplyView& view_cancel,
        Clock::time_point const when);

    std::pair<TER, Amounts>
    direct_cross (
        Taker& taker,
        ApplyView& view,
        ApplyView& view_cancel,
        Clock::time_point const when);

    // Step through the stream for as long as possible, skipping any offers
    // that are from the taker or which cross the taker's threshold.
    // Return false if the is no offer in the book, true otherwise.
    static
    bool
    step_account (OfferStream& stream, Taker const& taker, Logs& logs);

    // True if the number of offers that have been crossed
    // exceeds the limit.
    bool
    reachedOfferCrossingLimit (Taker const& taker) const;

    // Fill offer as much as possible by consuming offers already on the books,
    // and adjusting account balances accordingly.
    //
    // Charges fees on top to taker.
    std::pair<TER, Amounts>
    cross (
        ApplyView& view,
        ApplyView& cancel_view,
        Amounts const& taker_amount);

    static
    std::string
    format_amount (STAmount const& amount);

private:
    // What kind of offer we are placing
    CrossType cross_type_;

    // The number of steps to take through order books while crossing
    OfferStream::StepCounter stepCounter_;
};

}

#endif
