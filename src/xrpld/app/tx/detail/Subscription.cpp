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

        if (ctx.tx.isFieldPresent(sfExpiration))
        {
            auto const currentTime =
                ctx.view.info().parentCloseTime.time_since_epoch().count();
            auto const expiration = ctx.tx.getFieldU32(sfExpiration);

            if (expiration < currentTime)
            {
                JLOG(ctx.j.warn())
                    << "SetSubscription: The expiration time is in the past.";
                return temBAD_EXPIRATION;
            }
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

        auto const currentTime =
            ctx.view.info().parentCloseTime.time_since_epoch().count();
        auto startTime = currentTime;
        auto nextPaymentTime = currentTime;
        if (ctx.tx.isFieldPresent(sfStartTime))
        {
            startTime = ctx.tx.getFieldU32(sfStartTime);
            nextPaymentTime = startTime;
            if (startTime < currentTime)
            {
                JLOG(ctx.j.warn())
                    << "SetSubscription: The start time is in the past.";
                return temMALFORMED;
            }
        }

        if (ctx.tx.isFieldPresent(sfExpiration))
        {
            auto const expiration = ctx.tx.getFieldU32(sfExpiration);

            if (expiration < currentTime)
            {
                JLOG(ctx.j.warn())
                    << "SetSubscription: The expiration time is in the past.";
                return temBAD_EXPIRATION;
            }

            if (expiration < nextPaymentTime)
            {
                JLOG(ctx.j.warn()) << "SetSubscription: The expiration time is "
                                      "less than the next payment time.";
                return temBAD_EXPIRATION;
            }
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
            sle->setFieldU32(sfExpiration, ctx_.tx.getFieldU32(sfExpiration));

        sb.update(sle);
    }
    else
    {
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
        auto nextPaymentTime =
            sb.info().parentCloseTime.time_since_epoch().count();
        if (ctx_.tx.isFieldPresent(sfStartTime))
            nextPaymentTime = ctx_.tx.getFieldU32(sfStartTime);
        sle->setFieldU32(sfNextPaymentTime, nextPaymentTime);
        if (ctx_.tx.isFieldPresent(sfExpiration))
            sle->setFieldU32(sfExpiration, ctx_.tx.getFieldU32(sfExpiration));

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
        JLOG(j_.fatal()) << "Unable to delete check from source.";
        return tefBAD_LEDGER;
    }

    std::uint64_t const destPage{(*sleSub)[sfDestinationNode]};
    if (!sb.dirRemove(keylet::ownerDir(dstAcct), destPage, sleSub->key(), true))
    {
        JLOG(j_.fatal()) << "Unable to delete check from destination.";
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
    auto sleSub = ctx.view.read(
        keylet::subscription(ctx.tx.getFieldH256(sfSubscriptionID)));
    if (!sleSub)
    {
        JLOG(ctx.j.warn()) << "ClaimSubscription: Subscription does not exist.";
        return tecNO_TARGET;
    }

    AccountID const srcAcct{sleSub->getAccountID(sfAccount)};
    if (!ctx.view.exists(keylet::account(srcAcct)))
    {
        JLOG(ctx.j.warn()) << "ClaimSubscription: Account does not exist.";
        return terNO_ACCOUNT;
    }

    AccountID const destAcct{sleSub->getAccountID(sfDestination)};
    if (!ctx.view.exists(keylet::account(destAcct)))
    {
        JLOG(ctx.j.warn()) << "ClaimSubscription: Account does not exist.";
        return terNO_ACCOUNT;
    }

    if (ctx.tx.getFieldAmount(sfAmount) > sleSub->getFieldAmount(sfAmount))
    {
        JLOG(ctx.j.warn()) << "ClaimSubscription: The transaction amount is "
                              "greater than the subscription amount.";
        return temBAD_AMOUNT;
    }

    if (ctx.view.info().parentCloseTime.time_since_epoch().count() <
        sleSub->getFieldU32(sfNextPaymentTime))
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
