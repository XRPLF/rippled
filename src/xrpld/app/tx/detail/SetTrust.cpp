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

#include <xrpld/app/misc/DelegateUtils.h>
#include <xrpld/app/tx/detail/SetTrust.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>

namespace {

uint32_t
computeFreezeFlags(
    uint32_t uFlags,
    bool bHigh,
    bool bNoFreeze,
    bool bSetFreeze,
    bool bClearFreeze,
    bool bSetDeepFreeze,
    bool bClearDeepFreeze)
{
    if (bSetFreeze && !bClearFreeze && !bNoFreeze)
    {
        uFlags |= (bHigh ? ripple::lsfHighFreeze : ripple::lsfLowFreeze);
    }
    else if (bClearFreeze && !bSetFreeze)
    {
        uFlags &= ~(bHigh ? ripple::lsfHighFreeze : ripple::lsfLowFreeze);
    }
    if (bSetDeepFreeze && !bClearDeepFreeze && !bNoFreeze)
    {
        uFlags |=
            (bHigh ? ripple::lsfHighDeepFreeze : ripple::lsfLowDeepFreeze);
    }
    else if (bClearDeepFreeze && !bSetDeepFreeze)
    {
        uFlags &=
            ~(bHigh ? ripple::lsfHighDeepFreeze : ripple::lsfLowDeepFreeze);
    }

    return uFlags;
}

}  // namespace

namespace ripple {

std::uint32_t
SetTrust::getFlagsMask(PreflightContext const& ctx)
{
    return tfTrustSetMask;
}

NotTEC
SetTrust::preflight(PreflightContext const& ctx)
{
    auto& tx = ctx.tx;
    auto& j = ctx.j;

    std::uint32_t const uTxFlags = tx.getFlags();

    if (!ctx.rules.enabled(featureDeepFreeze))
    {
        // Even though the deep freeze flags are included in the
        // `tfTrustSetMask`, they are not valid if the amendment is not enabled.
        if (uTxFlags & (tfSetDeepFreeze | tfClearDeepFreeze))
        {
            return temINVALID_FLAG;
        }
    }

    STAmount const saLimitAmount(tx.getFieldAmount(sfLimitAmount));

    if (!isLegalNet(saLimitAmount))
        return temBAD_AMOUNT;

    if (saLimitAmount.native())
    {
        JLOG(j.trace()) << "Malformed transaction: specifies native limit "
                        << saLimitAmount.getFullText();
        return temBAD_LIMIT;
    }

    if (badCurrency() == saLimitAmount.getCurrency())
    {
        JLOG(j.trace()) << "Malformed transaction: specifies XRP as IOU";
        return temBAD_CURRENCY;
    }

    if (saLimitAmount < beast::zero)
    {
        JLOG(j.trace()) << "Malformed transaction: Negative credit limit.";
        return temBAD_LIMIT;
    }

    // Check if destination makes sense.
    auto const& issuer = saLimitAmount.getIssuer();

    if (!issuer || issuer == noAccount())
    {
        JLOG(j.trace()) << "Malformed transaction: no destination account.";
        return temDST_NEEDED;
    }

    return tesSUCCESS;
}

TER
SetTrust::checkPermission(ReadView const& view, STTx const& tx)
{
    auto const delegate = tx[~sfDelegate];
    if (!delegate)
        return tesSUCCESS;

    auto const delegateKey = keylet::delegate(tx[sfAccount], *delegate);
    auto const sle = view.read(delegateKey);

    if (!sle)
        return tecNO_DELEGATE_PERMISSION;

    if (checkTxPermission(sle, tx) == tesSUCCESS)
        return tesSUCCESS;

    std::uint32_t const txFlags = tx.getFlags();

    // Currently we only support TrustlineAuthorize, TrustlineFreeze and
    // TrustlineUnfreeze granular permission. Setting other flags returns
    // error.
    if (txFlags & tfTrustSetPermissionMask)
        return tecNO_DELEGATE_PERMISSION;

    if (tx.isFieldPresent(sfQualityIn) || tx.isFieldPresent(sfQualityOut))
        return tecNO_DELEGATE_PERMISSION;

    auto const saLimitAmount = tx.getFieldAmount(sfLimitAmount);
    auto const sleRippleState = view.read(keylet::line(
        tx[sfAccount], saLimitAmount.getIssuer(), saLimitAmount.getCurrency()));

    // if the trustline does not exist, granular permissions are
    // not allowed to create trustline
    if (!sleRippleState)
        return tecNO_DELEGATE_PERMISSION;

    std::unordered_set<GranularPermissionType> granularPermissions;
    loadGranularPermission(sle, ttTRUST_SET, granularPermissions);

    if (txFlags & tfSetfAuth &&
        !granularPermissions.contains(TrustlineAuthorize))
        return tecNO_DELEGATE_PERMISSION;
    if (txFlags & tfSetFreeze && !granularPermissions.contains(TrustlineFreeze))
        return tecNO_DELEGATE_PERMISSION;
    if (txFlags & tfClearFreeze &&
        !granularPermissions.contains(TrustlineUnfreeze))
        return tecNO_DELEGATE_PERMISSION;

    // updating LimitAmount is not allowed only with granular permissions,
    // unless there's a new granular permission for this in the future.
    auto const curLimit = tx[sfAccount] > saLimitAmount.getIssuer()
        ? sleRippleState->getFieldAmount(sfHighLimit)
        : sleRippleState->getFieldAmount(sfLowLimit);

    STAmount saLimitAllow = saLimitAmount;
    saLimitAllow.setIssuer(tx[sfAccount]);

    if (curLimit != saLimitAllow)
        return tecNO_DELEGATE_PERMISSION;

    return tesSUCCESS;
}

TER
SetTrust::preclaim(PreclaimContext const& ctx)
{
    auto const id = ctx.tx[sfAccount];

    auto const sle = ctx.view.read(keylet::account(id));
    if (!sle)
        return terNO_ACCOUNT;

    std::uint32_t const uTxFlags = ctx.tx.getFlags();

    bool const bSetAuth = (uTxFlags & tfSetfAuth);

    if (bSetAuth && !(sle->getFieldU32(sfFlags) & lsfRequireAuth))
    {
        JLOG(ctx.j.trace()) << "Retry: Auth not required.";
        return tefNO_AUTH_REQUIRED;
    }

    auto const saLimitAmount = ctx.tx[sfLimitAmount];

    auto const currency = saLimitAmount.getCurrency();
    auto const uDstAccountID = saLimitAmount.getIssuer();

    if (ctx.view.rules().enabled(fixTrustLinesToSelf))
    {
        if (id == uDstAccountID)
            return temDST_IS_SRC;
    }
    else
    {
        if (id == uDstAccountID)
        {
            // Prevent trustline to self from being created,
            // unless one has somehow already been created
            // (in which case doApply will clean it up).
            auto const sleDelete =
                ctx.view.read(keylet::line(id, uDstAccountID, currency));

            if (!sleDelete)
            {
                JLOG(ctx.j.trace())
                    << "Malformed transaction: Can not extend credit to self.";
                return temDST_IS_SRC;
            }
        }
    }

    // This might be nullptr
    auto const sleDst = ctx.view.read(keylet::account(uDstAccountID));
    if ((ctx.view.rules().enabled(featureDisallowIncoming) ||
         ammEnabled(ctx.view.rules()) ||
         ctx.view.rules().enabled(featureSingleAssetVault)) &&
        sleDst == nullptr)
        return tecNO_DST;

    // If the destination has opted to disallow incoming trustlines
    // then honour that flag
    if (ctx.view.rules().enabled(featureDisallowIncoming))
    {
        if (sleDst->getFlags() & lsfDisallowIncomingTrustline)
        {
            // The original implementation of featureDisallowIncoming was
            // too restrictive.  If
            //   o fixDisallowIncomingV1 is enabled and
            //   o The trust line already exists
            // Then allow the TrustSet.
            if (ctx.view.rules().enabled(fixDisallowIncomingV1) &&
                ctx.view.exists(keylet::line(id, uDstAccountID, currency)))
            {
                // pass
            }
            else
                return tecNO_PERMISSION;
        }
    }

    // In general, trust lines to pseudo accounts are not permitted, unless
    // enabled in the code section below, for specific cases. This block is not
    // amendment-gated because sleDst will not have a pseudo-account designator
    // field populated, unless the appropriate amendment was already enabled.
    if (sleDst && isPseudoAccount(sleDst))
    {
        // If destination is AMM and the trustline doesn't exist then only allow
        // SetTrust if the asset is AMM LP token and AMM is not in empty state.
        if (sleDst->isFieldPresent(sfAMMID))
        {
            if (ctx.view.exists(keylet::line(id, uDstAccountID, currency)))
            {
                // pass
            }
            else if (
                auto const ammSle =
                    ctx.view.read({ltAMM, sleDst->getFieldH256(sfAMMID)}))
            {
                if (auto const lpTokens =
                        ammSle->getFieldAmount(sfLPTokenBalance);
                    lpTokens == beast::zero)
                    return tecAMM_EMPTY;
                else if (lpTokens.getCurrency() != saLimitAmount.getCurrency())
                    return tecNO_PERMISSION;
            }
            else
                return tecINTERNAL;  // LCOV_EXCL_LINE
        }
        else if (sleDst->isFieldPresent(sfVaultID))
        {
            if (!ctx.view.exists(keylet::line(id, uDstAccountID, currency)))
                return tecNO_PERMISSION;
            // else pass
        }
        else
            return tecPSEUDO_ACCOUNT;
    }

    // Checking all freeze/deep freeze flag invariants.
    if (ctx.view.rules().enabled(featureDeepFreeze))
    {
        bool const bNoFreeze = sle->isFlag(lsfNoFreeze);
        bool const bSetFreeze = (uTxFlags & tfSetFreeze);
        bool const bSetDeepFreeze = (uTxFlags & tfSetDeepFreeze);

        if (bNoFreeze && (bSetFreeze || bSetDeepFreeze))
        {
            // Cannot freeze the trust line if NoFreeze is set
            return tecNO_PERMISSION;
        }

        bool const bClearFreeze = (uTxFlags & tfClearFreeze);
        bool const bClearDeepFreeze = (uTxFlags & tfClearDeepFreeze);
        if ((bSetFreeze || bSetDeepFreeze) &&
            (bClearFreeze || bClearDeepFreeze))
        {
            // Freezing and unfreezing in the same transaction should be
            // illegal
            return tecNO_PERMISSION;
        }

        bool const bHigh = id > uDstAccountID;
        // Fetching current state of trust line
        auto const sleRippleState =
            ctx.view.read(keylet::line(id, uDstAccountID, currency));
        std::uint32_t uFlags =
            sleRippleState ? sleRippleState->getFieldU32(sfFlags) : 0u;
        // Computing expected trust line state
        uFlags = computeFreezeFlags(
            uFlags,
            bHigh,
            bNoFreeze,
            bSetFreeze,
            bClearFreeze,
            bSetDeepFreeze,
            bClearDeepFreeze);

        auto const frozen = uFlags & (bHigh ? lsfHighFreeze : lsfLowFreeze);
        auto const deepFrozen =
            uFlags & (bHigh ? lsfHighDeepFreeze : lsfLowDeepFreeze);

        // Trying to set deep freeze on not already frozen trust line must
        // fail. This also checks that clearing normal freeze while deep
        // frozen must not work
        if (deepFrozen && !frozen)
        {
            return tecNO_PERMISSION;
        }
    }

    return tesSUCCESS;
}

TER
SetTrust::doApply()
{
    TER terResult = tesSUCCESS;

    STAmount const saLimitAmount(ctx_.tx.getFieldAmount(sfLimitAmount));
    bool const bQualityIn(ctx_.tx.isFieldPresent(sfQualityIn));
    bool const bQualityOut(ctx_.tx.isFieldPresent(sfQualityOut));

    Currency const currency(saLimitAmount.getCurrency());
    AccountID uDstAccountID(saLimitAmount.getIssuer());

    // true, if current is high account.
    bool const bHigh = account_ > uDstAccountID;

    auto const sle = view().peek(keylet::account(account_));
    if (!sle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    std::uint32_t const uOwnerCount = sle->getFieldU32(sfOwnerCount);

    // The reserve that is required to create the line. Note
    // that although the reserve increases with every item
    // an account owns, in the case of trust lines we only
    // *enforce* a reserve if the user owns more than two
    // items.
    //
    // We do this because being able to exchange currencies,
    // which needs trust lines, is a powerful Ripple feature.
    // So we want to make it easy for a gateway to fund the
    // accounts of its users without fear of being tricked.
    //
    // Without this logic, a gateway that wanted to have a
    // new user use its services, would have to give that
    // user enough XRP to cover not only the account reserve
    // but the incremental reserve for the trust line as
    // well. A person with no intention of using the gateway
    // could use the extra XRP for their own purposes.

    XRPAmount const reserveCreate(
        (uOwnerCount < 2) ? XRPAmount(beast::zero)
                          : view().fees().accountReserve(uOwnerCount + 1));

    std::uint32_t uQualityIn(bQualityIn ? ctx_.tx.getFieldU32(sfQualityIn) : 0);
    std::uint32_t uQualityOut(
        bQualityOut ? ctx_.tx.getFieldU32(sfQualityOut) : 0);

    if (bQualityOut && QUALITY_ONE == uQualityOut)
        uQualityOut = 0;

    std::uint32_t const uTxFlags = ctx_.tx.getFlags();

    bool const bSetAuth = (uTxFlags & tfSetfAuth);
    bool const bSetNoRipple = (uTxFlags & tfSetNoRipple);
    bool const bClearNoRipple = (uTxFlags & tfClearNoRipple);
    bool const bSetFreeze = (uTxFlags & tfSetFreeze);
    bool const bClearFreeze = (uTxFlags & tfClearFreeze);
    bool const bSetDeepFreeze = (uTxFlags & tfSetDeepFreeze);
    bool const bClearDeepFreeze = (uTxFlags & tfClearDeepFreeze);

    auto viewJ = ctx_.app.journal("View");

    // Trust lines to self are impossible but because of the old bug there
    // are two on 19-02-2022. This code was here to allow those trust lines
    // to be deleted. The fixTrustLinesToSelf fix amendment will remove them
    // when it enables so this code will no longer be needed.
    if (!view().rules().enabled(fixTrustLinesToSelf) &&
        account_ == uDstAccountID)
    {
        return trustDelete(
            view(),
            view().peek(keylet::line(account_, uDstAccountID, currency)),
            account_,
            uDstAccountID,
            viewJ);
    }

    SLE::pointer sleDst = view().peek(keylet::account(uDstAccountID));

    if (!sleDst)
    {
        JLOG(j_.trace())
            << "Delay transaction: Destination account does not exist.";
        return tecNO_DST;
    }

    STAmount saLimitAllow = saLimitAmount;
    saLimitAllow.setIssuer(account_);

    SLE::pointer sleRippleState =
        view().peek(keylet::line(account_, uDstAccountID, currency));

    if (sleRippleState)
    {
        STAmount saLowBalance;
        STAmount saLowLimit;
        STAmount saHighBalance;
        STAmount saHighLimit;
        std::uint32_t uLowQualityIn;
        std::uint32_t uLowQualityOut;
        std::uint32_t uHighQualityIn;
        std::uint32_t uHighQualityOut;
        auto const& uLowAccountID = !bHigh ? account_ : uDstAccountID;
        auto const& uHighAccountID = bHigh ? account_ : uDstAccountID;
        SLE::ref sleLowAccount = !bHigh ? sle : sleDst;
        SLE::ref sleHighAccount = bHigh ? sle : sleDst;

        //
        // Balances
        //

        saLowBalance = sleRippleState->getFieldAmount(sfBalance);
        saHighBalance = -saLowBalance;

        //
        // Limits
        //

        sleRippleState->setFieldAmount(
            !bHigh ? sfLowLimit : sfHighLimit, saLimitAllow);

        saLowLimit =
            !bHigh ? saLimitAllow : sleRippleState->getFieldAmount(sfLowLimit);
        saHighLimit =
            bHigh ? saLimitAllow : sleRippleState->getFieldAmount(sfHighLimit);

        //
        // Quality in
        //

        if (!bQualityIn)
        {
            // Not setting. Just get it.

            uLowQualityIn = sleRippleState->getFieldU32(sfLowQualityIn);
            uHighQualityIn = sleRippleState->getFieldU32(sfHighQualityIn);
        }
        else if (uQualityIn)
        {
            // Setting.

            sleRippleState->setFieldU32(
                !bHigh ? sfLowQualityIn : sfHighQualityIn, uQualityIn);

            uLowQualityIn = !bHigh
                ? uQualityIn
                : sleRippleState->getFieldU32(sfLowQualityIn);
            uHighQualityIn = bHigh
                ? uQualityIn
                : sleRippleState->getFieldU32(sfHighQualityIn);
        }
        else
        {
            // Clearing.

            sleRippleState->makeFieldAbsent(
                !bHigh ? sfLowQualityIn : sfHighQualityIn);

            uLowQualityIn =
                !bHigh ? 0 : sleRippleState->getFieldU32(sfLowQualityIn);
            uHighQualityIn =
                bHigh ? 0 : sleRippleState->getFieldU32(sfHighQualityIn);
        }

        if (QUALITY_ONE == uLowQualityIn)
            uLowQualityIn = 0;

        if (QUALITY_ONE == uHighQualityIn)
            uHighQualityIn = 0;

        //
        // Quality out
        //

        if (!bQualityOut)
        {
            // Not setting. Just get it.

            uLowQualityOut = sleRippleState->getFieldU32(sfLowQualityOut);
            uHighQualityOut = sleRippleState->getFieldU32(sfHighQualityOut);
        }
        else if (uQualityOut)
        {
            // Setting.

            sleRippleState->setFieldU32(
                !bHigh ? sfLowQualityOut : sfHighQualityOut, uQualityOut);

            uLowQualityOut = !bHigh
                ? uQualityOut
                : sleRippleState->getFieldU32(sfLowQualityOut);
            uHighQualityOut = bHigh
                ? uQualityOut
                : sleRippleState->getFieldU32(sfHighQualityOut);
        }
        else
        {
            // Clearing.

            sleRippleState->makeFieldAbsent(
                !bHigh ? sfLowQualityOut : sfHighQualityOut);

            uLowQualityOut =
                !bHigh ? 0 : sleRippleState->getFieldU32(sfLowQualityOut);
            uHighQualityOut =
                bHigh ? 0 : sleRippleState->getFieldU32(sfHighQualityOut);
        }

        std::uint32_t const uFlagsIn(sleRippleState->getFieldU32(sfFlags));
        std::uint32_t uFlagsOut(uFlagsIn);

        if (bSetNoRipple && !bClearNoRipple)
        {
            if ((bHigh ? saHighBalance : saLowBalance) >= beast::zero)
                uFlagsOut |= (bHigh ? lsfHighNoRipple : lsfLowNoRipple);

            else
                // Cannot set noRipple on a negative balance.
                return tecNO_PERMISSION;
        }
        else if (bClearNoRipple && !bSetNoRipple)
        {
            uFlagsOut &= ~(bHigh ? lsfHighNoRipple : lsfLowNoRipple);
        }

        // Have to use lsfNoFreeze to maintain pre-deep freeze behavior
        bool const bNoFreeze = sle->isFlag(lsfNoFreeze);
        uFlagsOut = computeFreezeFlags(
            uFlagsOut,
            bHigh,
            bNoFreeze,
            bSetFreeze,
            bClearFreeze,
            bSetDeepFreeze,
            bClearDeepFreeze);

        if (QUALITY_ONE == uLowQualityOut)
            uLowQualityOut = 0;

        if (QUALITY_ONE == uHighQualityOut)
            uHighQualityOut = 0;

        bool const bLowDefRipple = sleLowAccount->getFlags() & lsfDefaultRipple;
        bool const bHighDefRipple =
            sleHighAccount->getFlags() & lsfDefaultRipple;

        bool const bLowReserveSet = uLowQualityIn || uLowQualityOut ||
            ((uFlagsOut & lsfLowNoRipple) == 0) != bLowDefRipple ||
            (uFlagsOut & lsfLowFreeze) || saLowLimit ||
            saLowBalance > beast::zero;
        bool const bLowReserveClear = !bLowReserveSet;

        bool const bHighReserveSet = uHighQualityIn || uHighQualityOut ||
            ((uFlagsOut & lsfHighNoRipple) == 0) != bHighDefRipple ||
            (uFlagsOut & lsfHighFreeze) || saHighLimit ||
            saHighBalance > beast::zero;
        bool const bHighReserveClear = !bHighReserveSet;

        bool const bDefault = bLowReserveClear && bHighReserveClear;

        bool const bLowReserved = (uFlagsIn & lsfLowReserve);
        bool const bHighReserved = (uFlagsIn & lsfHighReserve);

        bool bReserveIncrease = false;

        if (bSetAuth)
        {
            uFlagsOut |= (bHigh ? lsfHighAuth : lsfLowAuth);
        }

        if (bLowReserveSet && !bLowReserved)
        {
            // Set reserve for low account.
            adjustOwnerCount(view(), sleLowAccount, 1, viewJ);
            uFlagsOut |= lsfLowReserve;

            if (!bHigh)
                bReserveIncrease = true;
        }

        if (bLowReserveClear && bLowReserved)
        {
            // Clear reserve for low account.
            adjustOwnerCount(view(), sleLowAccount, -1, viewJ);
            uFlagsOut &= ~lsfLowReserve;
        }

        if (bHighReserveSet && !bHighReserved)
        {
            // Set reserve for high account.
            adjustOwnerCount(view(), sleHighAccount, 1, viewJ);
            uFlagsOut |= lsfHighReserve;

            if (bHigh)
                bReserveIncrease = true;
        }

        if (bHighReserveClear && bHighReserved)
        {
            // Clear reserve for high account.
            adjustOwnerCount(view(), sleHighAccount, -1, viewJ);
            uFlagsOut &= ~lsfHighReserve;
        }

        if (uFlagsIn != uFlagsOut)
            sleRippleState->setFieldU32(sfFlags, uFlagsOut);

        if (bDefault || badCurrency() == currency)
        {
            // Delete.

            terResult = trustDelete(
                view(), sleRippleState, uLowAccountID, uHighAccountID, viewJ);
        }
        // Reserve is not scaled by load.
        else if (bReserveIncrease && mPriorBalance < reserveCreate)
        {
            JLOG(j_.trace()) << "Delay transaction: Insufficent reserve to "
                                "add trust line.";

            // Another transaction could provide XRP to the account and then
            // this transaction would succeed.
            terResult = tecINSUF_RESERVE_LINE;
        }
        else
        {
            view().update(sleRippleState);

            JLOG(j_.trace()) << "Modify ripple line";
        }
    }
    // Line does not exist.
    else if (
        !saLimitAmount &&                  // Setting default limit.
        (!bQualityIn || !uQualityIn) &&    // Not setting quality in or
                                           // setting default quality in.
        (!bQualityOut || !uQualityOut) &&  // Not setting quality out or
                                           // setting default quality out.
        (!bSetAuth))
    {
        JLOG(j_.trace())
            << "Redundant: Setting non-existent ripple line to defaults.";
        return tecNO_LINE_REDUNDANT;
    }
    else if (mPriorBalance < reserveCreate)  // Reserve is not scaled by
                                             // load.
    {
        JLOG(j_.trace()) << "Delay transaction: Line does not exist. "
                            "Insufficent reserve to create line.";

        // Another transaction could create the account and then this
        // transaction would succeed.
        terResult = tecNO_LINE_INSUF_RESERVE;
    }
    else
    {
        // Zero balance in currency.
        STAmount saBalance(Issue{currency, noAccount()});

        auto const k = keylet::line(account_, uDstAccountID, currency);

        JLOG(j_.trace()) << "doTrustSet: Creating ripple line: "
                         << to_string(k.key);

        // Create a new ripple line.
        terResult = trustCreate(
            view(),
            bHigh,
            account_,
            uDstAccountID,
            k.key,
            sle,
            bSetAuth,
            bSetNoRipple && !bClearNoRipple,
            bSetFreeze && !bClearFreeze,
            bSetDeepFreeze,
            saBalance,
            saLimitAllow,  // Limit for who is being charged.
            uQualityIn,
            uQualityOut,
            viewJ);
    }

    return terResult;
}

}  // namespace ripple
