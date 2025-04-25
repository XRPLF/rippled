//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#include <xrpld/app/tx/detail/OptionSettle.h>
#include <xrpld/app/tx/detail/OptionUtils.h>
#include <xrpld/ledger/Sandbox.h>
#include <xrpld/ledger/View.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Option.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
OptionSettle::preflight(PreflightContext const& ctx)
{
    // First, check if the Options feature is enabled on the network
    if (!ctx.rules.enabled(featureOptions))
        return temDISABLED;

    // Perform standard preflight checks (like fee/sequence number)
    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    // Get the transaction flags
    std::uint32_t const flags = ctx.tx.getFlags();

    // Check for any invalid flags using the mask
    if (flags & tfOptionSettleMask)
    {
        JLOG(ctx.j.warn()) << "OptionSettle: Invalid flags set.";
        return temINVALID_FLAG;
    }

    // Verify that exactly one of the three action flags is set:
    // - tfExpire: Expire the option
    // - tfClose: Close the option
    // - tfExercise: Exercise the option
    if (std::popcount(flags & (tfExpire | tfClose | tfExercise)) != 1)
    {
        JLOG(ctx.j.trace()) << "OptionSettle: Invalid flags set.";
        return temINVALID_FLAG;
    }

    // Perform additional preflight checks
    return preflight2(ctx);
}

TER
OptionSettle::preclaim(PreclaimContext const& ctx)
{
    // Get the option ID from the transaction
    uint256 const optionID = ctx.tx.getFieldH256(sfOptionID);

    // Verify the option exists in the ledger
    if (!ctx.view.exists(ripple::keylet::unchecked(optionID)))
        return tecNO_ENTRY;

    // Get the option offer ID from the transaction
    uint256 const offerID = ctx.tx.getFieldH256(sfOptionOfferID);

    // Load the option offer from the ledger
    auto const sleOffer = ctx.view.read(keylet::unchecked(offerID));
    if (!sleOffer)
    {
        JLOG(ctx.j.trace()) << "OptionSettle: Option offer not found.";
        return tecNO_TARGET;
    }

    // Get the transaction flags
    auto const flags = ctx.tx.getFlags();

    // For exercising options, verify that the offer is not a sell offer
    // (only buy offers can be exercised)
    if (!(flags & (tfClose | tfExpire)) && (sleOffer->getFlags() & tfSell))
    {
        JLOG(ctx.j.trace()) << "OptionSettle: Option offer is a sell offer.";
        return tecNO_PERMISSION;
    }

    // Verify that the account submitting the transaction is the owner of the
    // offer
    if (sleOffer->getAccountID(sfOwner) != ctx.tx.getAccountID(sfAccount))
    {
        JLOG(ctx.j.trace())
            << "OptionSettle: Option offer not owned by account.";
        return tecNO_PERMISSION;
    }

    return tesSUCCESS;
}

TER
OptionSettle::doApply()
{
    // Create a sandbox view to apply changes
    Sandbox sb(&ctx_.view());

    // Get the account SLE of the transaction submitter
    auto sleAccount = sb.peek(keylet::account(account_));
    if (!sleAccount)
        return tecINTERNAL;

    // Get the option offer keylet and SLE
    auto offerKeylet =
        keylet::optionOffer(ctx_.tx.getFieldH256(sfOptionOfferID));
    auto sleOffer = sb.peek(offerKeylet);
    if (!sleOffer)
        return tecINTERNAL;

    // Get the option definition SLE
    auto sleOption =
        sb.read(keylet::unchecked(ctx_.tx.getFieldH256(sfOptionID)));
    if (!sleOption)
        return tecINTERNAL;

    // Get the transaction flags
    auto const flags = ctx_.tx.getFlags();

    // Handle expiration - either natural expiration or explicit expire flag
    if (hasExpired(sb, sleOffer->getFieldU32(sfExpiration)) ||
        (flags & tfExpire))
    {
        JLOG(j_.trace()) << "OptionSettle: Expire offer.";

        // Call utility function to handle the expiration
        if (auto const ter = option::expireOffer(sb, sleOffer, j_);
            ter != tesSUCCESS)
            return ter;

        // Apply the changes to the ledger
        sb.apply(ctx_.rawView());

        // Return expired status
        return tecEXPIRED;
    }

    // Get the sealed options array from the offer
    STArray const sealedOptions = sleOffer->getFieldArray(sfSealedOptions);

    // If there are no sealed options, simply delete the offer
    if (sealedOptions.size() == 0)
    {
        if (auto const ter = option::deleteOffer(sb, sleOffer, j_);
            ter != tesSUCCESS)
            return ter;

        // Apply the changes to the ledger
        sb.apply(ctx_.rawView());
        return tesSUCCESS;
    }

    // Extract option properties needed for processing
    auto const optionFlags = sleOffer->getFlags();
    bool const isPut = optionFlags & tfPut;    // Is this a put option?
    bool const isSell = optionFlags & tfSell;  // Is this a sell offer?
    auto const issue =
        (*sleOption)[sfAsset].get<Issue>();  // The underlying asset
    STAmount const strikePrice =
        sleOption->getFieldAmount(sfStrikePrice);  // Strike price
    std::int64_t const strike =
        static_cast<std::int64_t>(Number(strikePrice));  // Strike as integer
    std::uint32_t expiration =
        sleOffer->getFieldU32(sfExpiration);  // Expiration time

    // Get the option pair
    auto const optionPairKeylet =
        keylet::optionPair(issue, strikePrice.issue());
    auto const slePair = sb.peek(optionPairKeylet);
    if (!slePair)
        return tecINTERNAL;
    auto const pseudoAccount = slePair->getAccountID(sfAccount);

    // Handle option closing
    if (flags & tfClose)
    {
        JLOG(j_.trace()) << "OptionSettle: Close offer.";

        // Call utility function to handle the closing process
        auto const ter = option::closeOffer(
            sb,
            pseudoAccount,
            account_,
            offerKeylet,
            isPut,
            isSell,
            issue,
            strike,
            expiration,
            j_);

        if (ter != tesSUCCESS)
            return ter;

        // Update the account in the ledger
        sb.update(sleAccount);

        // Apply the changes to the ledger
        sb.apply(ctx_.rawView());
        return tesSUCCESS;
    }

    // If not closing or expiring, we're exercising the option
    JLOG(j_.trace()) << "OptionSettle: Exercise offer.";

    // Call utility function to handle the option exercise
    if (auto const ter = option::exerciseOffer(
            sb,
            pseudoAccount,
            isPut,
            strikePrice,
            account_,
            sleAccount,
            issue,
            sealedOptions,
            j_);
        ter != tesSUCCESS)
        return ter;

    // Delete the offer after successful exercise
    if (auto const ter = option::deleteOffer(sb, sleOffer, j_);
        ter != tesSUCCESS)
        return ter;

    // Apply the changes to the ledger
    sb.apply(ctx_.rawView());
    return tesSUCCESS;
}

}  // namespace ripple