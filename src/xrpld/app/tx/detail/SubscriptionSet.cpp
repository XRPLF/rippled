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

#include <xrpld/app/ledger/Ledger.h>
#include <xrpld/app/misc/SubscriptionHelpers.h>
#include <xrpld/app/paths/Flow.h>
#include <xrpld/app/tx/detail/SubscriptionSet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/scope.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

template <ValidIssueType T>
static NotTEC
setPreflightHelper(PreflightContext const& ctx);

template <>
NotTEC
setPreflightHelper<Issue>(PreflightContext const& ctx)
{
    STAmount const amount = ctx.tx[sfAmount];
    if (amount.native() || amount <= beast::zero)
        return temBAD_AMOUNT;

    if (badCurrency() == amount.getCurrency())
        return temBAD_CURRENCY;

    return tesSUCCESS;
}

template <>
NotTEC
setPreflightHelper<MPTIssue>(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureMPTokensV1))
        return temDISABLED;

    auto const amount = ctx.tx[sfAmount];
    if (amount.native() || amount.mpt() > MPTAmount{maxMPTokenAmount} ||
        amount <= beast::zero)
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

NotTEC
SubscriptionSet::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureSubscription))
        return temDISABLED;

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.isFieldPresent(sfSubscriptionID))
    {
        // update
        if (!ctx.tx.isFieldPresent(sfAmount))
        {
            JLOG(ctx.j.trace())
                << "SubscriptionSet: Malformed transaction: SubscriptionID "
                   "is present, but Amount is not.";
            return temMALFORMED;
        }

        if (ctx.tx.isFieldPresent(sfDestination) ||
            ctx.tx.isFieldPresent(sfFrequency) ||
            ctx.tx.isFieldPresent(sfStartTime))
        {
            JLOG(ctx.j.trace())
                << "SubscriptionSet: Malformed transaction: SubscriptionID "
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
            JLOG(ctx.j.trace())
                << "SubscriptionSet: Malformed transaction: SubscriptionID "
                   "is not present, and required fields are not present.";
            return temMALFORMED;
        }

        if (ctx.tx.getAccountID(sfDestination) ==
            ctx.tx.getAccountID(sfAccount))
        {
            JLOG(ctx.j.trace())
                << "SubscriptionSet: Malformed transaction: Account "
                   "is the same as the destination.";
            return temDST_IS_SRC;
        }
    }

    STAmount const amount = ctx.tx.getFieldAmount(sfAmount);
    if (amount.native())
    {
        if (!isLegalNet(amount) || amount <= beast::zero)
        {
            JLOG(ctx.j.trace())
                << "SubscriptionSet: Malformed transaction: bad amount: "
                << amount.getFullText();
            return temBAD_AMOUNT;
        }
    }
    else
    {
        if (auto const ret = std::visit(
                [&]<typename T>(T const&) {
                    return setPreflightHelper<T>(ctx);
                },
                amount.asset().value());
            !isTesSuccess(ret))
            return ret;
    }

    return preflight2(ctx);
}

TER
SubscriptionSet::preclaim(PreclaimContext const& ctx)
{
    STAmount const amount = ctx.tx.getFieldAmount(sfAmount);
    AccountID const account = ctx.tx.getAccountID(sfAccount);
    AccountID const dest = ctx.tx.getAccountID(sfDestination);
    if (ctx.tx.isFieldPresent(sfSubscriptionID))
    {
        // update
        auto sle = ctx.view.read(
            keylet::subscription(ctx.tx.getFieldH256(sfSubscriptionID)));
        if (!sle)
        {
            JLOG(ctx.j.trace())
                << "SubscriptionSet: Subscription does not exist.";
            return tecNO_ENTRY;
        }

        if (sle->getAccountID(sfAccount) != ctx.tx.getAccountID(sfAccount))
        {
            JLOG(ctx.j.trace()) << "SubscriptionSet: Account is not the "
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
            JLOG(ctx.j.trace())
                << "SubscriptionSet: Destination account does not exist.";
            return tecNO_DST;
        }

        auto const flags = sleDest->getFlags();
        if ((flags & lsfRequireDestTag) && !ctx.tx[~sfDestinationTag])
            return tecDST_TAG_NEEDED;

        if (ctx.tx.getFieldU32(sfFrequency) <= 0)
        {
            JLOG(ctx.j.trace())
                << "SubscriptionSet: The frequency is less than or equal to 0.";
            return temMALFORMED;
        }
    }

    if (!isXRP(amount))
    {
        if (auto const ret = std::visit(
                [&]<typename T>(T const&) {
                    return canTransferTokenHelper<T>(
                        ctx.view, account, dest, amount, ctx.j);
                },
                amount.asset().value());
            !isTesSuccess(ret))
            return ret;
    }
    return tesSUCCESS;
}

TER
SubscriptionSet::doApply()
{
    Sandbox sb(&ctx_.view());

    AccountID const account = ctx_.tx.getAccountID(sfAccount);
    auto const sleAccount = sb.peek(keylet::account(account));
    if (!sleAccount)
    {
        JLOG(ctx_.journal.trace())
            << "SubscriptionSet: Account does not exist.";
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
                JLOG(ctx_.journal.trace())
                    << "SubscriptionSet: The expiration time is in the past.";
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
        auto nextClaimTime = currentTime;

        // create
        {
            auto const balance = STAmount((*sleAccount)[sfBalance]).xrp();
            auto const reserve =
                sb.fees().accountReserve((*sleAccount)[sfOwnerCount] + 1);
            if (balance < reserve)
                return tecINSUFFICIENT_RESERVE;
        }

        AccountID const dest = ctx_.tx.getAccountID(sfDestination);
        Keylet const subKeylet =
            keylet::subscription(account, dest, ctx_.tx.getSeqProxy().value());
        auto sle = std::make_shared<SLE>(subKeylet);
        sle->setAccountID(sfAccount, account);
        sle->setAccountID(sfDestination, dest);
        if (ctx_.tx.isFieldPresent(sfDestinationTag))
            sle->setFieldU32(
                sfDestinationTag, ctx_.tx.getFieldU32(sfDestinationTag));
        sle->setFieldAmount(sfAmount, ctx_.tx.getFieldAmount(sfAmount));
        sle->setFieldAmount(sfBalance, ctx_.tx.getFieldAmount(sfAmount));
        sle->setFieldU32(sfFrequency, ctx_.tx.getFieldU32(sfFrequency));
        if (ctx_.tx.isFieldPresent(sfStartTime))
        {
            startTime = ctx_.tx.getFieldU32(sfStartTime);
            nextClaimTime = startTime;
            if (startTime < currentTime)
            {
                JLOG(ctx_.journal.trace())
                    << "SubscriptionSet: The start time is in the past.";
                return temMALFORMED;
            }
        }

        sle->setFieldU32(sfNextClaimTime, nextClaimTime);
        if (ctx_.tx.isFieldPresent(sfExpiration))
        {
            auto const expiration = ctx_.tx.getFieldU32(sfExpiration);

            if (expiration < currentTime)
            {
                JLOG(ctx_.journal.trace())
                    << "SubscriptionSet: The expiration time is in the past.";
                return temBAD_EXPIRATION;
            }

            if (expiration < nextClaimTime)
            {
                JLOG(ctx_.journal.trace())
                    << "SubscriptionSet: The expiration time is "
                       "less than the next claim time.";
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

}  // namespace ripple
