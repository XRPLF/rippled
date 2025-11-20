#include <xrpld/app/ledger/Ledger.h>
#include <xrpld/app/paths/Flow.h>
#include <xrpld/app/tx/detail/CashCheck.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/scope.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <algorithm>

namespace ripple {

NotTEC
CashCheck::preflight(PreflightContext const& ctx)
{
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

    return tesSUCCESS;
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
    AccountID const dstId = sleCheck->at(sfDestination);
    if (ctx.tx[sfAccount] != dstId)
    {
        JLOG(ctx.j.warn()) << "Cashing a check with wrong Destination.";
        return tecNO_PERMISSION;
    }
    AccountID const srcId = sleCheck->at(sfAccount);
    if (srcId == dstId)
    {
        // They wrote a check to themselves.  This should be caught when
        // the check is created, but better late than never.
        // LCOV_EXCL_START
        JLOG(ctx.j.error()) << "Malformed transaction: Cashing check to self.";
        return tecINTERNAL;
        // LCOV_EXCL_STOP
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

    if (hasExpired(ctx.view, sleCheck->at(~sfExpiration)))
    {
        JLOG(ctx.j.warn()) << "Cashing a check that has already expired.";
        return tecEXPIRED;
    }

    {
        // Preflight verified exactly one of Amount or DeliverMin is present.
        // Make sure the requested amount is reasonable.
        STAmount const value{[](STTx const& tx) {
            auto const optAmount = tx[~sfAmount];
            return optAmount ? *optAmount : tx[sfDeliverMin];
        }(ctx.tx)};

        STAmount const sendMax = sleCheck->at(sfSendMax);
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
                sleCheck->at(sfAccount),
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
            auto const sleIssuer = ctx.view.read(keylet::account(issuerId));
            if (!sleIssuer)
            {
                JLOG(ctx.j.warn())
                    << "Can't receive IOUs from non-existent issuer: "
                    << to_string(issuerId);
                return tecNO_ISSUER;
            }

            if (sleIssuer->at(sfFlags) & lsfRequireAuth)
            {
                auto const sleTrustLine =
                    ctx.view.read(keylet::line(dstId, issuerId, currency));

                if (!sleTrustLine)
                {
                    // We can only create a trust line if the issuer does not
                    // have lsfRequireAuth set.
                    return tecNO_AUTH;
                }

                // Entries have a canonical representation, determined by a
                // lexicographical "greater than" comparison employing strict
                // weak ordering. Determine which entry we need to access.
                bool const canonical_gt(dstId > issuerId);

                bool const is_authorized(
                    sleTrustLine->at(sfFlags) &
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

    auto sleCheck = psb.peek(keylet::check(ctx_.tx[sfCheckID]));
    if (!sleCheck)
    {
        // LCOV_EXCL_START
        JLOG(j_.fatal()) << "Precheck did not verify check's existence.";
        return tecFAILED_PROCESSING;
        // LCOV_EXCL_STOP
    }

    AccountID const srcId{sleCheck->getAccountID(sfAccount)};
    if (!psb.exists(keylet::account(srcId)) ||
        !psb.exists(keylet::account(account_)))
    {
        // LCOV_EXCL_START
        JLOG(ctx_.journal.fatal())
            << "Precheck did not verify source or destination's existence.";
        return tecFAILED_PROCESSING;
        // LCOV_EXCL_STOP
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

    if (srcId != account_)
    {
        STAmount const sendMax = sleCheck->at(sfSendMax);

        // Flow() doesn't do XRP to XRP transfers.
        if (sendMax.native())
        {
            // Here we need to calculate the amount of XRP src can send.
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

            if (optDeliverMin)
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
            // Note that for DeliverMin we don't know exactly how much
            // currency we want flow to deliver.  We can't ask for the
            // maximum possible currency because there might be a gateway
            // transfer rate to account for.  Since the transfer rate cannot
            // exceed 200%, we use 1/2 maxValue as our limit.
            STAmount const flowDeliver{
                optDeliverMin ? STAmount(
                                    optDeliverMin->issue(),
                                    STAmount::cMaxValue / 2,
                                    STAmount::cMaxOffset)
                              : ctx_.tx.getFieldAmount(sfAmount)};

            // If a trust line does not exist yet create one.
            Issue const& trustLineIssue = flowDeliver.issue();
            AccountID const issuer = flowDeliver.getIssuer();
            AccountID const truster = issuer == account_ ? srcId : account_;
            Keylet const trustLineKey = keylet::line(truster, trustLineIssue);
            bool const destLow = issuer > account_;

            if (!psb.exists(trustLineKey))
            {
                // 1. Can the check casher meet the reserve for the trust line?
                // 2. Create trust line between destination (this) account
                //    and the issuer.
                // 3. Apply correct noRipple settings on trust line.  Use...
                //     a. this (destination) account and
                //     b. issuing account (not sending account).

                auto const sleDst = psb.peek(keylet::account(account_));

                // Can the account cover the trust line's reserve?
                if (std::uint32_t const ownerCount = {sleDst->at(sfOwnerCount)};
                    mPriorBalance < psb.fees().accountReserve(ownerCount + 1))
                {
                    JLOG(j_.trace()) << "Trust line does not exist. "
                                        "Insufficent reserve to create line.";

                    return tecNO_LINE_INSUF_RESERVE;
                }

                Currency const currency = flowDeliver.getCurrency();
                STAmount initialBalance(flowDeliver.issue());
                initialBalance.setIssuer(noAccount());

                // clang-format off
                if (TER const ter = trustCreate(
                        psb,                            // payment sandbox
                        destLow,                        // is dest low?
                        issuer,                         // source
                        account_,                       // destination
                        trustLineKey.key,               // ledger index
                        sleDst,                         // Account to add to
                        false,                          // authorize account
                        (sleDst->getFlags() & lsfDefaultRipple) == 0,
                        false,                          // freeze trust line
                        false,                          // deep freeze trust line
                        initialBalance,                 // zero initial balance
                        Issue(currency, account_),      // limit of zero
                        0,                              // quality in
                        0,                              // quality out
                        viewJ);                         // journal
                    !isTesSuccess(ter))
                {
                    return ter;
                }
                // clang-format on

                psb.update(sleDst);

                // Note that we _don't_ need to be careful about destroying
                // the trust line if the check cashing fails.  The transaction
                // machinery will automatically clean it up.
            }

            // Since the destination is signing the check, they clearly want
            // the funds even if their new total funds would exceed the limit
            // on their trust line.  So we tweak the trust line limits before
            // calling flow and then restore the trust line limits afterwards.
            auto const sleTrustLine = psb.peek(trustLineKey);
            if (!sleTrustLine)
                return tecNO_LINE;

            SF_AMOUNT const& tweakedLimit = destLow ? sfLowLimit : sfHighLimit;
            STAmount const savedLimit = sleTrustLine->at(tweakedLimit);

            // Make sure the tweaked limits are restored when we leave scope.
            scope_exit fixup(
                [&psb, &trustLineKey, &tweakedLimit, &savedLimit]() {
                    if (auto const sleTrustLine = psb.peek(trustLineKey))
                        sleTrustLine->at(tweakedLimit) = savedLimit;
                });

            // Set the trust line limit to the highest possible value
            // while flow runs.
            STAmount const bigAmount(
                trustLineIssue, STAmount::cMaxValue, STAmount::cMaxOffset);
            sleTrustLine->at(tweakedLimit) = bigAmount;

            // Let flow() do the heavy lifting on a check for an IOU.
            auto const result = flow(
                psb,
                flowDeliver,
                srcId,
                account_,
                STPathSet{},
                true,                              // default path
                static_cast<bool>(optDeliverMin),  // partial payment
                true,                              // owner pays transfer fee
                OfferCrossing::no,
                std::nullopt,
                sleCheck->getFieldAmount(sfSendMax),
                std::nullopt,  // check does not support domain
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
            }

            // Set the delivered amount metadata in all cases, not just
            // for DeliverMin.
            ctx_.deliver(result.actualAmountOut);

            sleCheck = psb.peek(keylet::check(ctx_.tx[sfCheckID]));
        }
    }

    // Check was cashed.  If not a self send (and it shouldn't be), remove
    // check link from destination directory.
    if (srcId != account_ &&
        !psb.dirRemove(
            keylet::ownerDir(account_),
            sleCheck->at(sfDestinationNode),
            sleCheck->key(),
            true))
    {
        // LCOV_EXCL_START
        JLOG(j_.fatal()) << "Unable to delete check from destination.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    // Remove check from check owner's directory.
    if (!psb.dirRemove(
            keylet::ownerDir(srcId),
            sleCheck->at(sfOwnerNode),
            sleCheck->key(),
            true))
    {
        // LCOV_EXCL_START
        JLOG(j_.fatal()) << "Unable to delete check from owner.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    // If we succeeded, update the check owner's reserve.
    adjustOwnerCount(psb, psb.peek(keylet::account(srcId)), -1, viewJ);

    // Remove check from ledger.
    psb.erase(sleCheck);

    psb.apply(ctx_.rawView());
    return tesSUCCESS;
}

}  // namespace ripple
