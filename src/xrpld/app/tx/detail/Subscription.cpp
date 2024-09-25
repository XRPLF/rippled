//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#include <xrpld/app/ledger/Ledger.h>
#include <xrpld/app/paths/Flow.h>
#include <xrpld/app/tx/detail/Subscription.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/scope.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
SetSubscription::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureSubscription))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.isFieldPresent(sfSubscriptionID))
    {
        // update
        if (ctx.tx.isFieldPresent(sfDestination) ||
            ctx.tx.isFieldPresent(sfFrequency) ||
            ctx.tx.isFieldPresent(sfStartTime))
        {
            JLOG(ctx.j.warn())
                << "SetSubscription: Malformed transaction: SubscriptionID "
                   "is  present, but optional fields are also present.";
            return temMALFORMED;
        }
    }
    else
    {
        // create
        if (!ctx.tx.isFieldPresent(sfDestination) ||
            !ctx.tx.isFieldPresent(sfAmount) ||
            !ctx.tx.isFieldPresent(sfFrequency))
        {
            JLOG(ctx.j.warn())
                << "SetSubscription: Malformed transaction: SubscriptionID "
                   "is not present, and required fields are not present.";
            return temMALFORMED;
        }

        if (ctx.tx.getAccountID(sfDestination) ==
            ctx.tx.getAccountID(sfAccount))
        {
            JLOG(ctx.j.warn())
                << "SetSubscription: Malformed transaction: Account "
                   "is the same as the destination.";
            return temDST_IS_SRC;
        }
    }

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    STAmount const amount = ctx.tx.getFieldAmount(sfAmount);
    if (!isLegalNet(amount) || amount.signum() <= 0)
    {
        JLOG(ctx.j.warn())
            << "SetSubscription: Malformed transaction: bad amount: "
            << amount.getFullText();
        return temBAD_AMOUNT;
    }

    if (badCurrency() == amount.getCurrency())
    {
        JLOG(ctx.j.warn())
            << "SetSubscription: Malformed transaction: Bad currency.";
        return temBAD_CURRENCY;
    }

    return preflight2(ctx);
}

TER
SetSubscription::preclaim(PreclaimContext const& ctx)
{
    if (ctx.tx.isFieldPresent(sfSubscriptionID))
    {
        // update
        auto sle = ctx.view.read(
            keylet::subscription(ctx.tx.getFieldH256(sfSubscriptionID)));
        if (!sle)
        {
            JLOG(ctx.j.warn())
                << "SetSubscription: Subscription does not exist.";
            return tecNO_ENTRY;
        }

        if (sle->getAccountID(sfAccount) != ctx.tx.getAccountID(sfAccount))
        {
            JLOG(ctx.j.warn()) << "SetSubscription: Account is not the "
                                  "owner of the subscription.";
            return tecNO_PERMISSION;
        }
    }
    else
    {
        // create
        auto const sleDest =
            ctx.view.read(keylet::account(ctx.tx.getAccountID(sfDestination)));
        if (!sleDest)
        {
            JLOG(ctx.j.warn())
                << "SetSubscription: Destination account does not exist.";
            return tecNO_DST;
        }

        auto const flags = sleDest->getFlags();
        if ((flags & lsfRequireDestTag) && !ctx.tx[~sfDestinationTag])
            return tecDST_TAG_NEEDED;

        if (ctx.tx.getFieldU32(sfFrequency) <= 0)
        {
            JLOG(ctx.j.warn())
                << "SetSubscription: The frequency is less than or equal to 0.";
            return temMALFORMED;
        }
    }
    return tesSUCCESS;
}

TER
SetSubscription::doApply()
{
    Sandbox sb(&ctx_.view());

    AccountID const account = ctx_.tx.getAccountID(sfAccount);
    auto const sleAccount = sb.peek(keylet::account(account));
    if (!sleAccount)
    {
        JLOG(ctx_.journal.warn()) << "SetSubscription: Account does not exist.";
        return tecINTERNAL;
    }

    if (ctx_.tx.isFieldPresent(sfSubscriptionID))
    {
        // update
        auto sle = sb.peek(
            keylet::subscription(ctx_.tx.getFieldH256(sfSubscriptionID)));
        sle->setFieldAmount(sfAmount, ctx_.tx.getFieldAmount(sfAmount));
        if (ctx_.tx.isFieldPresent(sfExpiration))
        {
            auto const currentTime =
                sb.info().parentCloseTime.time_since_epoch().count();
            auto const expiration = ctx_.tx.getFieldU32(sfExpiration);

            if (expiration < currentTime)
            {
                JLOG(ctx_.journal.warn())
                    << "SetSubscription: The expiration time is in the past.";
                return temBAD_EXPIRATION;
            }

            sle->setFieldU32(sfExpiration, ctx_.tx.getFieldU32(sfExpiration));
        }

        sb.update(sle);
    }
    else
    {
        auto const currentTime =
            sb.info().parentCloseTime.time_since_epoch().count();
        auto startTime = currentTime;
        auto nextPaymentTime = currentTime;

        // create
        {
            auto const balance = STAmount((*sleAccount)[sfBalance]).xrp();
            auto const reserve =
                sb.fees().accountReserve((*sleAccount)[sfOwnerCount] + 1);
            if (balance < reserve)
                return tecINSUFFICIENT_RESERVE;

            // TODO: DA Should you be required to have the first installment?
            // if (balance < reserve + STAmount(ctx_.tx[sfAmount]).xrp())
            //     return tecUNFUNDED;
        }

        AccountID const dest = ctx_.tx.getAccountID(sfDestination);
        Keylet const subKeylet =
            keylet::subscription(account, dest, ctx_.tx.getSeqProxy().value());
        auto sle = std::make_shared<SLE>(subKeylet);
        sle->setAccountID(sfAccount, account);
        sle->setAccountID(sfDestination, dest);
        sle->setFieldAmount(sfAmount, ctx_.tx.getFieldAmount(sfAmount));
        sle->setFieldU32(sfFrequency, ctx_.tx.getFieldU32(sfFrequency));
        if (ctx_.tx.isFieldPresent(sfStartTime))
        {
            startTime = ctx_.tx.getFieldU32(sfStartTime);
            nextPaymentTime = startTime;
            if (startTime < currentTime)
            {
                JLOG(ctx_.journal.warn())
                    << "SetSubscription: The start time is in the past.";
                return temMALFORMED;
            }
        }

        sle->setFieldU32(sfNextPaymentTime, nextPaymentTime);
        if (ctx_.tx.isFieldPresent(sfExpiration))
        {
            auto const expiration = ctx_.tx.getFieldU32(sfExpiration);

            if (expiration < currentTime)
            {
                JLOG(ctx_.journal.warn())
                    << "SetSubscription: The expiration time is in the past.";
                return temBAD_EXPIRATION;
            }

            if (expiration < nextPaymentTime)
            {
                JLOG(ctx_.journal.warn())
                    << "SetSubscription: The expiration time is "
                       "less than the next payment time.";
                return temBAD_EXPIRATION;
            }
            sle->setFieldU32(sfExpiration, expiration);
        }

        {
            auto page = sb.dirInsert(
                keylet::ownerDir(account),
                subKeylet,
                describeOwnerDir(account));
            if (!page)
                return tecDIR_FULL;
            (*sle)[sfOwnerNode] = *page;
        }

        {
            auto page = sb.dirInsert(
                keylet::ownerDir(dest), subKeylet, describeOwnerDir(dest));
            if (!page)
                return tecDIR_FULL;
            (*sle)[sfDestinationNode] = *page;
        }

        adjustOwnerCount(sb, sleAccount, 1, ctx_.journal);
        sb.insert(sle);
    }
    sb.apply(ctx_.rawView());
    return tesSUCCESS;
}

NotTEC
CancelSubscription::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureSubscription))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    return preflight2(ctx);
}

TER
CancelSubscription::preclaim(PreclaimContext const& ctx)
{
    auto const sleSub = ctx.view.read(
        keylet::subscription(ctx.tx.getFieldH256(sfSubscriptionID)));
    if (!sleSub)
    {
        JLOG(ctx.j.warn())
            << "CancelSubscription: Subscription does not exist.";
        return tecNO_ENTRY;
    }

    return tesSUCCESS;
}

TER
CancelSubscription::doApply()
{
    Sandbox sb(&ctx_.view());

    auto const sleSub =
        sb.peek(keylet::subscription(ctx_.tx.getFieldH256(sfSubscriptionID)));
    if (!sleSub)
    {
        JLOG(ctx_.journal.warn())
            << "CancelSubscription: Subscription does not exist.";
        return tecINTERNAL;
    }

    AccountID const srcAcct{sleSub->getAccountID(sfAccount)};
    AccountID const dstAcct{sleSub->getAccountID(sfDestination)};
    auto viewJ = ctx_.app.journal("View");

    std::uint64_t const ownerPage{(*sleSub)[sfOwnerNode]};
    if (!sb.dirRemove(
            keylet::ownerDir(srcAcct), ownerPage, sleSub->key(), true))
    {
        JLOG(j_.fatal()) << "Unable to delete subscription from source.";
        return tefBAD_LEDGER;
    }

    std::uint64_t const destPage{(*sleSub)[sfDestinationNode]};
    if (!sb.dirRemove(keylet::ownerDir(dstAcct), destPage, sleSub->key(), true))
    {
        JLOG(j_.fatal()) << "Unable to delete subscription from destination.";
        return tefBAD_LEDGER;
    }

    auto const sleSrc = sb.peek(keylet::account(srcAcct));
    sb.erase(sleSub);

    adjustOwnerCount(sb, sleSrc, -1, viewJ);

    sb.apply(ctx_.rawView());
    return tesSUCCESS;
}

NotTEC
ClaimSubscription::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureSubscription))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    return preflight2(ctx);
}

TER
ClaimSubscription::preclaim(PreclaimContext const& ctx)
{
    auto const sleSub = ctx.view.read(
        keylet::subscription(ctx.tx.getFieldH256(sfSubscriptionID)));
    if (!sleSub)
    {
        JLOG(ctx.j.warn()) << "ClaimSubscription: Subscription does not exist.";
        return tecNO_ENTRY;
    }

    // Only claim a subscription with this account as the destination.
    AccountID const dstId = sleSub->getAccountID(sfDestination);
    if (ctx.tx[sfAccount] != dstId)
    {
        JLOG(ctx.j.warn()) << "ClaimSubscription: Cashing a subscription with "
                              "wrong Destination.";
        return tecNO_PERMISSION;
    }
    AccountID const srcId = sleSub->getAccountID(sfAccount);
    if (srcId == dstId)
    {
        JLOG(ctx.j.error()) << "ClaimSubscription: Malformed transaction: "
                               "Cashing subscription to self.";
        return tecINTERNAL;
    }
    {
        auto const sleSrc = ctx.view.read(keylet::account(srcId));
        auto const sleDst = ctx.view.read(keylet::account(dstId));
        if (!sleSrc || !sleDst)
        {
            JLOG(ctx.j.warn())
                << "ClaimSubscription: source or destination not in ledger";
            return tecNO_ENTRY;
        }

        if ((sleDst->getFlags() & lsfRequireDestTag) &&
            !sleSub->isFieldPresent(sfDestinationTag))
        {
            // The tag is basically account-specific information we don't
            // understand, but we can require someone to fill it in.
            JLOG(ctx.j.warn()) << "ClaimSubscription: DestinationTag required "
                                  "in subscription.";
            return tecDST_TAG_NEEDED;
        }
    }

    {
        STAmount const value = ctx.tx.getFieldAmount(sfAmount);
        STAmount const sendMax = sleSub->getFieldAmount(sfAmount);
        Currency const currency{value.getCurrency()};
        if (currency != sendMax.getCurrency())
        {
            JLOG(ctx.j.warn()) << "ClaimSubscription: Subscription claim does "
                                  "not match subscription currency.";
            return temMALFORMED;
        }
        AccountID const issuerId{value.getIssuer()};
        if (issuerId != sendMax.getIssuer())
        {
            JLOG(ctx.j.warn()) << "ClaimSubscription: Subscription claim does "
                                  "not match subscription issuer.";
            return temMALFORMED;
        }
        if (value > sendMax)
        {
            JLOG(ctx.j.warn()) << "ClaimSubscription: Subscription claim for "
                                  "more than subscription sendMax.";
            return tecPATH_PARTIAL;
        }

        {
            STAmount availableFunds{accountFunds(
                ctx.view,
                sleSub->at(sfAccount),
                value,
                fhZERO_IF_FROZEN,
                ctx.j)};

            if (value > availableFunds)
            {
                JLOG(ctx.j.warn()) << "ClaimSubscription: Subscription claimed "
                                      "for more than owner's balance.";
                return tecPATH_PARTIAL;
            }
        }

        // An issuer can always accept their own currency.
        if (!value.native() && (value.getIssuer() != dstId))
        {
            auto const sleTrustLine =
                ctx.view.read(keylet::line(dstId, issuerId, currency));

            auto const sleIssuer = ctx.view.read(keylet::account(issuerId));
            if (!sleIssuer)
            {
                JLOG(ctx.j.warn()) << "ClaimSubscription: Can't receive IOUs "
                                      "from non-existent issuer: "
                                   << to_string(issuerId);
                return tecNO_ISSUER;
            }

            if (sleIssuer->at(sfFlags) & lsfRequireAuth)
            {
                if (!sleTrustLine)
                {
                    // We can only create a trust line if the issuer does not
                    // have requireAuth set.
                    return tecNO_AUTH;
                }

                // Entries have a canonical representation, determined by a
                // lexicographical "greater than" comparison employing strict
                // weak ordering. Determine which entry we need to access.
                bool const canonical_gt(dstId > issuerId);

                bool const isAuthorized(
                    sleTrustLine->at(sfFlags) &
                    (canonical_gt ? lsfLowAuth : lsfHighAuth));

                if (!isAuthorized)
                {
                    JLOG(ctx.j.warn()) << "ClaimSubscription: Can't receive "
                                          "IOUs from issuer without auth.";
                    return tecNO_AUTH;
                }
            }

            // The trustline from source to issuer does not need to
            // be claimed for freezing, since we already verified that the
            // source has sufficient non-frozen funds available.

            // However, the trustline from destination to issuer may not
            // be frozen.
            if (isFrozen(ctx.view, dstId, currency, issuerId))
            {
                JLOG(ctx.j.warn()) << "ClaimSubscription: Claiming a "
                                      "subscription to a frozen trustline.";
                return tecFROZEN;
            }
        }
    }

    if (!hasExpired(ctx.view, sleSub->getFieldU32(sfNextPaymentTime)))
    {
        JLOG(ctx.j.warn()) << "ClaimSubscription: The subscription has not "
                              "reached the next payment time.";
        return tefFAILURE;
    }

    return tesSUCCESS;
}

TER
ClaimSubscription::doApply()
{
    PaymentSandbox psb(&ctx_.view());
    auto viewJ = ctx_.app.journal("View");

    auto sleSub =
        psb.peek(keylet::subscription(ctx_.tx.getFieldH256(sfSubscriptionID)));
    if (!sleSub)
    {
        JLOG(ctx_.journal.warn())
            << "ClaimSubscription: Subscription does not exist.";
        return tecINTERNAL;
    }

    AccountID const srcAcct{sleSub->getAccountID(sfAccount)};
    if (!psb.exists(keylet::account(srcAcct)))
    {
        JLOG(ctx_.journal.warn())
            << "ClaimSubscription: Account does not exist.";
        return tecINTERNAL;
    }

    AccountID const destAcct{sleSub->getAccountID(sfDestination)};
    if (!psb.exists(keylet::account(destAcct)))
    {
        JLOG(ctx_.journal.warn())
            << "ClaimSubscription: Account does not exist.";
        return tecINTERNAL;
    }

    if (destAcct != ctx_.tx.getAccountID(sfAccount))
    {
        JLOG(ctx_.journal.warn()) << "ClaimSubscription: Account is not the "
                                     "destination of the subscription.";
        return tecNO_PERMISSION;
    }

    STAmount const amount = sleSub->getFieldAmount(sfAmount);
    if (amount.native())
    {
        STAmount const srcLiquid{xrpLiquid(psb, srcAcct, 0, viewJ)};
        STAmount const xrpDeliver{ctx_.tx.getFieldAmount(sfAmount)};

        if (srcLiquid < xrpDeliver)
        {
            JLOG(ctx_.journal.warn())
                << "ClaimSubscription: Insufficient funds.";
            return tecUNFUNDED_PAYMENT;
        }

        if (TER const ter{
                transferXRP(psb, srcAcct, destAcct, xrpDeliver, viewJ)};
            ter != tesSUCCESS)
        {
            return ter;
        }
    }
    else
    {
        STAmount const flowDeliver{ctx_.tx.getFieldAmount(sfAmount)};
        Issue const& trustLineIssue = flowDeliver.issue();
        AccountID const issuer = flowDeliver.getIssuer();
        AccountID const truster = issuer == destAcct ? srcAcct : destAcct;
        Keylet const trustLineKey = keylet::line(truster, trustLineIssue);
        bool const destLow = issuer > destAcct;

        if (!psb.exists(trustLineKey))
        {
            auto const sleDst = psb.peek(keylet::account(destAcct));

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
                    destAcct,                       // destination
                    trustLineKey.key,               // ledger index
                    sleDst,                         // Account to add to
                    false,                          // authorize account
                    (sleDst->getFlags() & lsfDefaultRipple) == 0,
                    false,                          // freeze trust line
                    initialBalance,                 // zero initial balance
                    Issue(currency, destAcct),      // limit of zero
                    0,                              // quality in
                    0,                              // quality out
                    viewJ);                         // journal
                !isTesSuccess(ter))
            {
                return ter;
            }
            // clang-format on

            psb.update(sleDst);
        }

        auto const sleTrustLine = psb.peek(trustLineKey);
        if (!sleTrustLine)
            return tecINTERNAL;

        SF_AMOUNT const& tweakedLimit = destLow ? sfLowLimit : sfHighLimit;
        STAmount const savedLimit = sleTrustLine->at(tweakedLimit);

        scope_exit fixup([&psb, &trustLineKey, &tweakedLimit, &savedLimit]() {
            if (auto const sleTrustLine = psb.peek(trustLineKey))
                sleTrustLine->at(tweakedLimit) = savedLimit;
        });

        STAmount const bigAmount(
            trustLineIssue, STAmount::cMaxValue, STAmount::cMaxOffset);
        sleTrustLine->at(tweakedLimit) = bigAmount;

        auto const result = flow(
            psb,
            flowDeliver,
            srcAcct,
            destAcct,
            STPathSet{},
            true,
            false,
            true,
            OfferCrossing::no,
            std::nullopt,
            sleSub->getFieldAmount(sfAmount),
            viewJ);

        if (result.result() != tesSUCCESS)
        {
            JLOG(ctx_.journal.warn())
                << "flow failed when claiming subscription.";
            return result.result();
        }

        ctx_.deliver(result.actualAmountOut);
    }

    sleSub->setFieldU32(
        sfNextPaymentTime,
        sleSub->getFieldU32(sfNextPaymentTime) +
            sleSub->getFieldU32(sfFrequency));
    psb.update(sleSub);

    if (sleSub->isFieldPresent(sfExpiration) &&
        psb.info().parentCloseTime.time_since_epoch().count() >=
            sleSub->getFieldU32(sfExpiration))
    {
        psb.erase(sleSub);
    }

    psb.apply(ctx_.rawView());
    return tesSUCCESS;
}

}  // namespace ripple
