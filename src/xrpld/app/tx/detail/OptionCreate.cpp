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

#include <xrpld/app/ledger/OrderBookDB.h>
#include <xrpld/app/tx/detail/OptionCreate.h>
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
OptionCreate::preflight(PreflightContext const& ctx)
{
    // Check if the Options feature is enabled
    if (!ctx.rules.enabled(featureOptions))
        return temDISABLED;

    // Perform base preflight checks (sequence number, fee, etc.)
    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    // Verify no invalid flags are set
    if (ctx.tx.getFlags() & tfOptionCreateMask)
    {
        JLOG(ctx.j.warn()) << "OptionCreate: Invalid flags set.";
        return temINVALID_FLAG;
    }

    // Verify quantity is valid (must be divisible by 100)
    std::uint32_t const quantity = ctx.tx[sfQuantity];
    if (quantity % 100)
    {
        JLOG(ctx.j.warn()) << "OptionCreate: Invalid quantity.";
        return temMALFORMED;
    }

    // Perform additional preflight checks
    return preflight2(ctx);
}

TER
OptionCreate::preclaim(PreclaimContext const& ctx)
{
    return tesSUCCESS;
}

TER
OptionCreate::doApply()
{
    // Create sandbox view for transaction application
    Sandbox sb(&ctx_.view());

    // Extract option parameters from transaction
    auto const flags = ctx_.tx.getFlags();
    std::uint32_t const expiration = ctx_.tx[sfExpiration];
    STAmount const strikePrice = ctx_.tx[sfStrikePrice];
    std::int64_t const strike = static_cast<std::int64_t>(Number(strikePrice));
    Asset const asset = ctx_.tx[sfAsset].get<Issue>();
    Issue const issue = asset.get<Issue>();
    STAmount const premium = ctx_.tx.getFieldAmount(sfPremium);
    std::uint32_t const quantity = ctx_.tx.getFieldU32(sfQuantity);

    // Verify source account exists
    auto sleSource = sb.peek(keylet::account(account_));
    if (!sleSource)
        return terNO_ACCOUNT;

    // Determine option type flags
    bool const isPut = flags & tfPut;
    bool const isMarket = flags & tfMarket;
    bool const isSell = flags & tfSell;

    // Generate keylets for option book and offer
    auto optionBookDirKeylet =
        keylet::optionBook(issue.account, issue.currency, strike, expiration);
    auto optionOfferKeylet =
        keylet::optionOffer(account_, ctx_.tx.getSeqProxy().value());

    // Seal option against matching orders in the book
    std::vector<option::SealedOptionData> sealedOptions = option::matchOptions(
        sb,
        issue,
        strike,
        expiration,
        isPut,
        isSell,
        quantity,
        account_,
        optionOfferKeylet.key,
        isMarket,
        premium);

    JLOG(j_.trace()) << "OptionCreate: Sealed Options: "
                     << sealedOptions.size();

    // Calculate total quantity that was matched/sealed
    std::uint32_t totalSealedQuantity = 0;
    for (const auto& sealedOption : sealedOptions)
    {
        totalSealedQuantity += sealedOption.quantitySealed;
    }

    // Calculate remaining open interest
    std::uint32_t openInterest = quantity - totalSealedQuantity;

    // Calculate values needed for token transfers and records
    STAmount const quantityShares = STAmount(issue, quantity);

    // Calculate total value for the option
    STAmount const totalValue = mulRound(
        strikePrice,
        STAmount(strikePrice.issue(), quantity),
        strikePrice.issue(),
        false);

    // Calculate premium for the open portion
    STAmount const openPremium = mulRound(
        premium,
        STAmount(premium.issue(), openInterest),
        premium.issue(),
        false);

    // Handle premium transfers and token locking
    if (!isSell)  // Buy offer
    {
        // For buy offers that were matched, transfer premium to each seller
        if (!sealedOptions.empty())
        {
            for (const auto& sealedOption : sealedOptions)
            {
                // Calculate premium for the sealed portion
                STAmount const sealedPremium = mulRound(
                    sealedOption.premium,
                    STAmount(
                        sealedOption.premium.issue(),
                        sealedOption.quantitySealed),
                    sealedOption.premium.issue(),
                    false);

                // Transfer premium from buyer to seller
                JLOG(j_.trace())
                    << "OptionCreate: Transfer premium: " << sealedPremium
                    << " from " << account_ << " to " << sealedOption.account;
                auto const ter = option::transferTokens(
                    sb,
                    account_,              // sender
                    sealedOption.account,  // receiver
                    sealedPremium,         // amount
                    j_);

                if (ter != tesSUCCESS)
                    return ter;
            }
        }
    }
    else  // Sell offer
    {
        // For sell offers, always lock tokens from seller to escrow
        // For puts, lock the strike value; for calls, lock the asset quantity
        JLOG(j_.trace()) << "OptionCreate: Locking tokens for sell offer: "
                         << (isPut ? totalValue : quantityShares);
        auto const ter = option::lockTokens(
            sb,
            mSourceBalance,
            account_,
            isPut ? totalValue : quantityShares,
            j_);

        if (ter != tesSUCCESS)
            return ter;

        // For sell offers that were matched, receive premium from each buyer
        if (!sealedOptions.empty())
        {
            for (const auto& sealedOption : sealedOptions)
            {
                // Calculate premium for the sealed portion
                STAmount const sealedPremium = mulRound(
                    sealedOption.premium,
                    STAmount(
                        sealedOption.premium.issue(),
                        sealedOption.quantitySealed),
                    sealedOption.premium.issue(),
                    false);

                // Transfer premium from buyer to this seller
                JLOG(j_.trace())
                    << "OptionCreate: Transfer premium: " << sealedPremium
                    << " from " << sealedOption.account << " to " << account_;
                auto const ter = option::transferTokens(
                    sb,
                    sealedOption.account,  // sender
                    account_,              // receiver
                    sealedPremium,         // amount
                    j_);

                if (ter != tesSUCCESS)
                    return ter;
            }
        }
    }

    // Create new option offer with the matched and open quantities
    auto const ter = option::createOffer(
        sb,
        account_,
        optionOfferKeylet,
        flags,
        quantity,
        openInterest,
        premium,
        isSell,
        isPut ? totalValue : quantityShares,
        issue,
        strikePrice,
        strike,
        expiration,
        optionBookDirKeylet,
        sealedOptions,
        j_);

    if (ter != tesSUCCESS)
        return ter;

    // Create the option in the ledger if it doesn't exist yet
    std::optional<Keylet> const optionKeylet =
        keylet::option(issue.account, issue.currency, strike, expiration);

    if (!sb.exists(*optionKeylet))
    {
        // Create new option ledger entry
        auto const sleOption = std::make_shared<SLE>(*optionKeylet);

        // Add to owner directory
        auto const newPage = sb.dirInsert(
            keylet::ownerDir(issue.account),
            *optionKeylet,
            describeOwnerDir(issue.account));

        if (!newPage)
        {
            JLOG(j_.trace())
                << "OptionList: Failed to add list to owner directory";
            return tecDIR_FULL;
        }

        // Set option properties
        (*sleOption)[sfOwnerNode] = *newPage;
        (*sleOption)[sfStrikePrice] = strikePrice;
        (*sleOption)[sfAsset] = STIssue{sfAsset, asset};
        (*sleOption)[sfExpiration] = expiration;

        // Add option to ledger
        sb.insert(sleOption);
    }

    // Apply all changes to the ledger
    sb.update(sleSource);
    sb.apply(ctx_.rawView());
    return tesSUCCESS;
}

}  // namespace ripple