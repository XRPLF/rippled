//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

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

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/paths/Flow.h>
#include <ripple/app/tx/impl/CashCheck.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/TER.h>
#include <ripple/protocol/TxFlags.h>

#include <algorithm>

namespace ripple {

NotTEC
CashCheck::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureChecks))
        return temDISABLED;

    NotTEC const ret{preflight1(ctx)};
    if (!isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
    {
        // There are no flags (other than universal) for CashCheck yet.
        JLOG(ctx.j.warn()) << "Malformed transaction: Invalid flags set.";
        return temINVALID_FLAG;
    }

    // Exactly one of Amount or DeliverMin must be present.
    auto const optAmount = ctx.tx[~sfAmount];
    auto const optDeliverMin = ctx.tx[~sfDeliverMin];

    if (static_cast<bool>(optAmount) == static_cast<bool>(optDeliverMin))
    {
        JLOG(ctx.j.warn())
            << "Malformed transaction: "
               "does not specify exactly one of Amount and DeliverMin.";
        return temMALFORMED;
    }

    // Make sure the amount is valid.
    STAmount const value{optAmount ? *optAmount : *optDeliverMin};
    if (!isLegalNet(value) || value.signum() <= 0)
    {
        JLOG(ctx.j.warn()) << "Malformed transaction: bad amount: "
                           << value.getFullText();
        return temBAD_AMOUNT;
    }

    if (badCurrency() == value.getCurrency())
    {
        JLOG(ctx.j.warn()) << "Malformed transaction: Bad currency.";
        return temBAD_CURRENCY;
    }

    return preflight2(ctx);
}

TER
CashCheck::preclaim(PreclaimContext const& ctx)
{
    auto const sleCheck = ctx.view.read(keylet::check(ctx.tx[sfCheckID]));
    if (!sleCheck)
    {
        JLOG(ctx.j.warn()) << "Check does not exist.";
        return tecNO_ENTRY;
    }

    // Only cash a check with this account as the destination.
    AccountID const dstId{(*sleCheck)[sfDestination]};
    if (ctx.tx[sfAccount] != dstId)
    {
        JLOG(ctx.j.warn()) << "Cashing a check with wrong Destination.";
        return tecNO_PERMISSION;
    }
    AccountID const srcId{(*sleCheck)[sfAccount]};
    if (srcId == dstId)
    {
        // They wrote a check to themselves.  This should be caught when
        // the check is created, but better late than never.
        JLOG(ctx.j.error()) << "Malformed transaction: Cashing check to self.";
        return tecINTERNAL;
    }
    {
        auto const sleSrc = ctx.view.read(keylet::account(srcId));
        auto const sleDst = ctx.view.read(keylet::account(dstId));
        if (!sleSrc || !sleDst)
        {
            // If the check exists this should never occur.
            JLOG(ctx.j.warn())
                << "Malformed transaction: source or destination not in ledger";
            return tecNO_ENTRY;
        }

        if ((sleDst->getFlags() & lsfRequireDestTag) &&
            !sleCheck->isFieldPresent(sfDestinationTag))
        {
            // The tag is basically account-specific information we don't
            // understand, but we can require someone to fill it in.
            JLOG(ctx.j.warn())
                << "Malformed transaction: DestinationTag required in check.";
            return tecDST_TAG_NEEDED;
        }
    }
    {
        using duration = NetClock::duration;
        using timepoint = NetClock::time_point;
        auto const optExpiry = (*sleCheck)[~sfExpiration];

        // Expiration is defined in terms of the close time of the parent
        // ledger, because we definitively know the time that it closed but
        // we do not know the closing time of the ledger that is under
        // construction.
        if (optExpiry &&
            (ctx.view.parentCloseTime() >= timepoint{duration{*optExpiry}}))
        {
            JLOG(ctx.j.warn()) << "Cashing a check that has already expired.";
            return tecEXPIRED;
        }
    }
    {
        // Preflight verified exactly one of Amount or DeliverMin is present.
        // Make sure the requested amount is reasonable.
        STAmount const value{[](STTx const& tx) {
            auto const optAmount = tx[~sfAmount];
            return optAmount ? *optAmount : tx[sfDeliverMin];
        }(ctx.tx)};

        STAmount const sendMax{(*sleCheck)[sfSendMax]};
        Currency const currency{value.getCurrency()};
        if (currency != sendMax.getCurrency())
        {
            JLOG(ctx.j.warn()) << "Check cash does not match check currency.";
            return temMALFORMED;
        }
        AccountID const issuerId{value.getIssuer()};
        if (issuerId != sendMax.getIssuer())
        {
            JLOG(ctx.j.warn()) << "Check cash does not match check issuer.";
            return temMALFORMED;
        }
        if (value > sendMax)
        {
            JLOG(ctx.j.warn()) << "Check cashed for more than check sendMax.";
            return tecPATH_PARTIAL;
        }

        // Make sure the check owner holds at least value.  If they have
        // less than value the check cannot be cashed.
        {
            STAmount availableFunds{accountFunds(
                ctx.view,
                (*sleCheck)[sfAccount],
                value,
                fhZERO_IF_FROZEN,
                ctx.j)};

            // Note that src will have one reserve's worth of additional XRP
            // once the check is cashed, since the check's reserve will no
            // longer be required.  So, if we're dealing in XRP, we add one
            // reserve's worth to the available funds.
            if (value.native())
                availableFunds += XRPAmount{ctx.view.fees().increment};

            if (value > availableFunds)
            {
                JLOG(ctx.j.warn())
                    << "Check cashed for more than owner's balance.";
                return tecPATH_PARTIAL;
            }
        }

        // An issuer can always accept their own currency.
        if (!value.native() && (value.getIssuer() != dstId))
        {
            auto const sleTrustLine =
                ctx.view.read(keylet::line(dstId, issuerId, currency));
            if (!sleTrustLine)
            {
                JLOG(ctx.j.warn())
                    << "Cannot cash check for IOU without trustline.";
                return tecNO_LINE;
            }

            auto const sleIssuer = ctx.view.read(keylet::account(issuerId));
            if (!sleIssuer)
            {
                JLOG(ctx.j.warn())
                    << "Can't receive IOUs from non-existent issuer: "
                    << to_string(issuerId);
                return tecNO_ISSUER;
            }

            if ((*sleIssuer)[sfFlags] & lsfRequireAuth)
            {
                // Entries have a canonical representation, determined by a
                // lexicographical "greater than" comparison employing strict
                // weak ordering. Determine which entry we need to access.
                bool const canonical_gt(dstId > issuerId);

                bool const is_authorized(
                    (*sleTrustLine)[sfFlags] &
                    (canonical_gt ? lsfLowAuth : lsfHighAuth));

                if (!is_authorized)
                {
                    JLOG(ctx.j.warn())
                        << "Can't receive IOUs from issuer without auth.";
                    return tecNO_AUTH;
                }
            }

            // The trustline from source to issuer does not need to
            // be checked for freezing, since we already verified that the
            // source has sufficient non-frozen funds available.

            // However, the trustline from destination to issuer may not
            // be frozen.
            if (isFrozen(ctx.view, dstId, currency, issuerId))
            {
                JLOG(ctx.j.warn()) << "Cashing a check to a frozen trustline.";
                return tecFROZEN;
            }
        }
    }
    return tesSUCCESS;
}

TER
CashCheck::doApply()
{
    // Flow requires that we operate on a PaymentSandbox, rather than
    // directly on a View.
    PaymentSandbox psb(&ctx_.view());

    auto const sleCheck = psb.peek(keylet::check(ctx_.tx[sfCheckID]));
    if (!sleCheck)
    {
        JLOG(j_.fatal()) << "Precheck did not verify check's existence.";
        return tecFAILED_PROCESSING;
    }

    AccountID const srcId{sleCheck->getAccountID(sfAccount)};
    auto const sleSrc = psb.peek(keylet::account(srcId));
    auto const sleDst = psb.peek(keylet::account(account_));

    if (!sleSrc || !sleDst)
    {
        JLOG(ctx_.journal.fatal())
            << "Precheck did not verify source or destination's existence.";
        return tecFAILED_PROCESSING;
    }

    // Preclaim already checked that source has at least the requested
    // funds.
    //
    // Therefore, if this is a check written to self, (and it shouldn't be)
    // we know they have sufficient funds to pay the check.  Since they are
    // taking the funds from their own pocket and putting it back in their
    // pocket no balance will change.
    //
    // If it is not a check to self (as should be the case), then there's
    // work to do...
    auto viewJ = ctx_.app.journal("View");
    auto const optDeliverMin = ctx_.tx[~sfDeliverMin];
    bool const doFix1623{ctx_.view().rules().enabled(fix1623)};
    if (srcId != account_)
    {
        STAmount const sendMax{sleCheck->getFieldAmount(sfSendMax)};

        // Flow() doesn't do XRP to XRP transfers.
        if (sendMax.native())
        {
            // Here we need to calculate the amount of XRP sleSrc can send.
            // The amount they have available is their balance minus their
            // reserve.
            //
            // Since (if we're successful) we're about to remove an entry
            // from src's directory, we allow them to send that additional
            // incremental reserve amount in the transfer.  Hence the -1
            // argument.
            STAmount const srcLiquid{xrpLiquid(psb, srcId, -1, viewJ)};

            // Now, how much do they need in order to be successful?
            STAmount const xrpDeliver{
                optDeliverMin
                    ? std::max(*optDeliverMin, std::min(sendMax, srcLiquid))
                    : ctx_.tx.getFieldAmount(sfAmount)};

            if (srcLiquid < xrpDeliver)
            {
                // Vote no. However the transaction might succeed if applied
                // in a different order.
                JLOG(j_.trace()) << "Cash Check: Insufficient XRP: "
                                 << srcLiquid.getFullText() << " < "
                                 << xrpDeliver.getFullText();
                return tecUNFUNDED_PAYMENT;
            }

            if (optDeliverMin && doFix1623)
                // Set the DeliveredAmount metadata.
                ctx_.deliver(xrpDeliver);

            // The source account has enough XRP so make the ledger change.
            if (TER const ter{
                    transferXRP(psb, srcId, account_, xrpDeliver, viewJ)};
                ter != tesSUCCESS)
            {
                // The transfer failed.  Return the error code.
                return ter;
            }
        }
        else
        {
            // Let flow() do the heavy lifting on a check for an IOU.
            //
            // Note that for DeliverMin we don't know exactly how much
            // currency we want flow to deliver.  We can't ask for the
            // maximum possible currency because there might be a gateway
            // transfer rate to account for.  Since the transfer rate cannot
            // exceed 200%, we use 1/2 maxValue as our limit.
            STAmount const flowDeliver{
                optDeliverMin
                    ? STAmount{optDeliverMin->issue(), STAmount::cMaxValue / 2, STAmount::cMaxOffset}
                    : static_cast<STAmount>(ctx_.tx[sfAmount])};

            // Call the payment engine's flow() to do the actual work.
            auto const result = flow(
                psb,
                flowDeliver,
                srcId,
                account_,
                STPathSet{},
                true,                              // default path
                static_cast<bool>(optDeliverMin),  // partial payment
                true,                              // owner pays transfer fee
                false,                             // offer crossing
                boost::none,
                sleCheck->getFieldAmount(sfSendMax),
                viewJ);

            if (result.result() != tesSUCCESS)
            {
                JLOG(ctx_.journal.warn()) << "flow failed when cashing check.";
                return result.result();
            }

            // Make sure that deliverMin was satisfied.
            if (optDeliverMin)
            {
                if (result.actualAmountOut < *optDeliverMin)
                {
                    JLOG(ctx_.journal.warn())
                        << "flow did not produce DeliverMin.";
                    return tecPATH_PARTIAL;
                }
                if (doFix1623)
                    // Set the delivered_amount metadata.
                    ctx_.deliver(result.actualAmountOut);
            }
        }
    }

    // Check was cashed.  If not a self send (and it shouldn't be), remove
    // check link from destination directory.
    if (srcId != account_)
    {
        std::uint64_t const page{(*sleCheck)[sfDestinationNode]};
        if (!ctx_.view().dirRemove(
                keylet::ownerDir(account_), page, sleCheck->key(), true))
        {
            JLOG(j_.fatal()) << "Unable to delete check from destination.";
            return tefBAD_LEDGER;
        }
    }
    // Remove check from check owner's directory.
    {
        std::uint64_t const page{(*sleCheck)[sfOwnerNode]};
        if (!ctx_.view().dirRemove(
                keylet::ownerDir(srcId), page, sleCheck->key(), true))
        {
            JLOG(j_.fatal()) << "Unable to delete check from owner.";
            return tefBAD_LEDGER;
        }
    }
    // If we succeeded, update the check owner's reserve.
    adjustOwnerCount(psb, sleSrc, -1, viewJ);

    // Remove check from ledger.
    psb.erase(sleCheck);

    psb.apply(ctx_.rawView());
    return tesSUCCESS;
}

}  // namespace ripple
