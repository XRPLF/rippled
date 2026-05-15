#include <xrpl/tx/transactors/token/TrustSet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DelegateHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <memory>
#include <unordered_set>

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
        uFlags |= (bHigh ? xrpl::lsfHighFreeze : xrpl::lsfLowFreeze);
    }
    else if (bClearFreeze && !bSetFreeze)
    {
        uFlags &= ~(bHigh ? xrpl::lsfHighFreeze : xrpl::lsfLowFreeze);
    }
    if (bSetDeepFreeze && !bClearDeepFreeze && !bNoFreeze)
    {
        uFlags |= (bHigh ? xrpl::lsfHighDeepFreeze : xrpl::lsfLowDeepFreeze);
    }
    else if (bClearDeepFreeze && !bSetDeepFreeze)
    {
        uFlags &= ~(bHigh ? xrpl::lsfHighDeepFreeze : xrpl::lsfLowDeepFreeze);
    }

    return uFlags;
}

}  // namespace

namespace xrpl {

std::uint32_t
TrustSet::getFlagsMask(PreflightContext const& ctx)
{
    return tfTrustSetMask;
}

NotTEC
TrustSet::preflight(PreflightContext const& ctx)
{
    auto& tx = ctx.tx;
    auto& j = ctx.j;

    if (!ctx.rules.enabled(featureDeepFreeze))
    {
        // Even though the deep freeze flags are included in the
        // `tfTrustSetMask`, they are not valid if the amendment is not enabled.
        if ((tx.getFlags() & (tfSetDeepFreeze | tfClearDeepFreeze)) != 0u)
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

    if (badCurrency() == saLimitAmount.get<Issue>().currency)
    {
        JLOG(j.trace()) << "Malformed transaction: specifies XRP as IOU";
        return temBAD_CURRENCY;
    }

    if (saLimitAmount < beast::kZERO)
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

NotTEC
TrustSet::checkPermission(ReadView const& view, STTx const& tx)
{
    auto const delegate = tx[~sfDelegate];
    if (!delegate)
        return tesSUCCESS;

    auto const delegateKey = keylet::delegate(tx[sfAccount], *delegate);
    auto const sle = view.read(delegateKey);

    if (!sle)
        return terNO_DELEGATE_PERMISSION;

    if (isTesSuccess(checkTxPermission(sle, tx)))
        return tesSUCCESS;

    // Currently we only support TrustlineAuthorize, TrustlineFreeze and
    // TrustlineUnfreeze granular permission. Setting other flags returns
    // error.
    if ((tx.getFlags() & tfTrustSetPermissionMask) != 0u)
        return terNO_DELEGATE_PERMISSION;

    if (tx.isFieldPresent(sfQualityIn) || tx.isFieldPresent(sfQualityOut))
        return terNO_DELEGATE_PERMISSION;

    auto const saLimitAmount = tx.getFieldAmount(sfLimitAmount);
    auto const sleRippleState = view.read(
        keylet::line(
            tx[sfAccount], saLimitAmount.getIssuer(), saLimitAmount.get<Issue>().currency));

    // if the trustline does not exist, granular permissions are
    // not allowed to create trustline
    if (!sleRippleState)
        return terNO_DELEGATE_PERMISSION;

    std::unordered_set<GranularPermissionType> granularPermissions;
    loadGranularPermission(sle, ttTRUST_SET, granularPermissions);

    if (tx.isFlag(tfSetfAuth) && !granularPermissions.contains(TrustlineAuthorize))
        return terNO_DELEGATE_PERMISSION;
    if (tx.isFlag(tfSetFreeze) && !granularPermissions.contains(TrustlineFreeze))
        return terNO_DELEGATE_PERMISSION;
    if (tx.isFlag(tfClearFreeze) && !granularPermissions.contains(TrustlineUnfreeze))
        return terNO_DELEGATE_PERMISSION;

    // updating LimitAmount is not allowed only with granular permissions,
    // unless there's a new granular permission for this in the future.
    auto const curLimit = tx[sfAccount] > saLimitAmount.getIssuer()
        ? sleRippleState->getFieldAmount(sfHighLimit)
        : sleRippleState->getFieldAmount(sfLowLimit);

    STAmount saLimitAllow = saLimitAmount;
    saLimitAllow.get<Issue>().account = tx[sfAccount];

    if (curLimit != saLimitAllow)
        return terNO_DELEGATE_PERMISSION;

    return tesSUCCESS;
}

TER
TrustSet::preclaim(PreclaimContext const& ctx)
{
    auto const id = ctx.tx[sfAccount];

    auto const sle = ctx.view.read(keylet::account(id));
    if (!sle)
        return terNO_ACCOUNT;

    bool const bSetAuth = ctx.tx.isFlag(tfSetfAuth);

    if (bSetAuth && !sle->isFlag(lsfRequireAuth))
    {
        JLOG(ctx.j.trace()) << "Retry: Auth not required.";
        return tefNO_AUTH_REQUIRED;
    }

    auto const saLimitAmount = ctx.tx[sfLimitAmount];

    auto const currency = saLimitAmount.get<Issue>().currency;
    auto const uDstAccountID = saLimitAmount.getIssuer();

    if (id == uDstAccountID)
        return temDST_IS_SRC;

    // This might be nullptr
    auto const sleDst = ctx.view.read(keylet::account(uDstAccountID));
    if ((ammEnabled(ctx.view.rules()) || ctx.view.rules().enabled(featureSingleAssetVault)) &&
        sleDst == nullptr)
        return tecNO_DST;

    // If the destination has opted to disallow incoming trustlines
    // then honour that flag
    if (sleDst->isFlag(lsfDisallowIncomingTrustline))
    {
        // The original implementation of featureDisallowIncoming was
        // too restrictive. If
        //   o fixDisallowIncomingV1 is enabled and
        //   o The trust line already exists
        // Then allow the TrustSet.
        if (ctx.view.rules().enabled(fixDisallowIncomingV1) &&
            ctx.view.exists(keylet::line(id, uDstAccountID, currency)))
        {
            // pass
        }
        else
        {
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
        // TrustSet if the asset is AMM LP token and AMM is not in empty state.
        if (sleDst->isFieldPresent(sfAMMID))
        {
            if (ctx.view.exists(keylet::line(id, uDstAccountID, currency)))
            {
                // pass
            }
            else if (auto const ammSle = ctx.view.read({ltAMM, sleDst->getFieldH256(sfAMMID)}))
            {
                auto const lpTokens = ammSle->getFieldAmount(sfLPTokenBalance);
                if (lpTokens == beast::kZERO)
                {
                    return tecAMM_EMPTY;
                }
                if (lpTokens.get<Issue>().currency != saLimitAmount.get<Issue>().currency)
                {
                    return tecNO_PERMISSION;
                }
            }
            else
            {
                return tecINTERNAL;  // LCOV_EXCL_LINE
            }
        }
        else if (sleDst->isFieldPresent(sfVaultID) || sleDst->isFieldPresent(sfLoanBrokerID))
        {
            if (!ctx.view.exists(keylet::line(id, uDstAccountID, currency)))
                return tecNO_PERMISSION;
            // else pass
        }
        else
        {
            return tecPSEUDO_ACCOUNT;
        }
    }

    // Checking all freeze/deep freeze flag invariants.
    if (ctx.view.rules().enabled(featureDeepFreeze))
    {
        bool const bNoFreeze = sle->isFlag(lsfNoFreeze);
        bool const bSetFreeze = ctx.tx.isFlag(tfSetFreeze);
        bool const bSetDeepFreeze = ctx.tx.isFlag(tfSetDeepFreeze);

        if (bNoFreeze && (bSetFreeze || bSetDeepFreeze))
        {
            // Cannot freeze the trust line if NoFreeze is set
            return tecNO_PERMISSION;
        }

        bool const bClearFreeze = ctx.tx.isFlag(tfClearFreeze);
        bool const bClearDeepFreeze = ctx.tx.isFlag(tfClearDeepFreeze);
        if ((bSetFreeze || bSetDeepFreeze) && (bClearFreeze || bClearDeepFreeze))
        {
            // Freezing and unfreezing in the same transaction should be
            // illegal
            return tecNO_PERMISSION;
        }

        bool const bHigh = id > uDstAccountID;
        // Fetching current state of trust line
        auto const sleRippleState = ctx.view.read(keylet::line(id, uDstAccountID, currency));
        std::uint32_t uFlags = sleRippleState ? sleRippleState->getFieldU32(sfFlags) : 0u;
        // Computing expected trust line state
        uFlags = computeFreezeFlags(
            uFlags, bHigh, bNoFreeze, bSetFreeze, bClearFreeze, bSetDeepFreeze, bClearDeepFreeze);

        auto const frozen = uFlags & (bHigh ? lsfHighFreeze : lsfLowFreeze);
        auto const deepFrozen = uFlags & (bHigh ? lsfHighDeepFreeze : lsfLowDeepFreeze);

        // Trying to set deep freeze on not already frozen trust line must
        // fail. This also checks that clearing normal freeze while deep
        // frozen must not work
        if ((deepFrozen != 0u) && (frozen == 0u))
        {
            return tecNO_PERMISSION;
        }
    }

    return tesSUCCESS;
}

TER
TrustSet::doApply()
{
    TER terResult = tesSUCCESS;

    STAmount const saLimitAmount(ctx_.tx.getFieldAmount(sfLimitAmount));
    bool const bQualityIn(ctx_.tx.isFieldPresent(sfQualityIn));
    bool const bQualityOut(ctx_.tx.isFieldPresent(sfQualityOut));

    Currency const currency(saLimitAmount.get<Issue>().currency);
    AccountID const uDstAccountID(saLimitAmount.getIssuer());

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
    // which needs trust lines, is a powerful XRPL feature.
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
        (uOwnerCount < 2) ? XRPAmount(beast::kZERO)
                          : view().fees().accountReserve(uOwnerCount + 1));

    std::uint32_t const uQualityIn(bQualityIn ? ctx_.tx.getFieldU32(sfQualityIn) : 0);
    std::uint32_t uQualityOut(bQualityOut ? ctx_.tx.getFieldU32(sfQualityOut) : 0);

    if (bQualityOut && QUALITY_ONE == uQualityOut)
        uQualityOut = 0;

    bool const bSetAuth = ctx_.tx.isFlag(tfSetfAuth);
    bool const bSetNoRipple = ctx_.tx.isFlag(tfSetNoRipple);
    bool const bClearNoRipple = ctx_.tx.isFlag(tfClearNoRipple);
    bool const bSetFreeze = ctx_.tx.isFlag(tfSetFreeze);
    bool const bClearFreeze = ctx_.tx.isFlag(tfClearFreeze);
    bool const bSetDeepFreeze = ctx_.tx.isFlag(tfSetDeepFreeze);
    bool const bClearDeepFreeze = ctx_.tx.isFlag(tfClearDeepFreeze);

    auto viewJ = ctx_.registry.get().getJournal("View");

    SLE::pointer const sleDst = view().peek(keylet::account(uDstAccountID));

    if (!sleDst)
    {
        JLOG(j_.trace()) << "Delay transaction: Destination account does not exist.";
        return tecNO_DST;
    }

    STAmount saLimitAllow = saLimitAmount;
    saLimitAllow.get<Issue>().account = account_;

    SLE::pointer const sleRippleState =
        view().peek(keylet::line(account_, uDstAccountID, currency));

    if (sleRippleState)
    {
        STAmount saLowBalance;
        STAmount saLowLimit;
        STAmount saHighBalance;
        STAmount saHighLimit;
        std::uint32_t uLowQualityIn = 0;
        std::uint32_t uLowQualityOut = 0;
        std::uint32_t uHighQualityIn = 0;
        std::uint32_t uHighQualityOut = 0;
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

        sleRippleState->setFieldAmount(!bHigh ? sfLowLimit : sfHighLimit, saLimitAllow);

        saLowLimit = !bHigh ? saLimitAllow : sleRippleState->getFieldAmount(sfLowLimit);
        saHighLimit = bHigh ? saLimitAllow : sleRippleState->getFieldAmount(sfHighLimit);

        //
        // Quality in
        //

        if (!bQualityIn)
        {
            // Not setting. Just get it.

            uLowQualityIn = sleRippleState->getFieldU32(sfLowQualityIn);
            uHighQualityIn = sleRippleState->getFieldU32(sfHighQualityIn);
        }
        else if (uQualityIn != 0u)
        {
            // Setting.

            sleRippleState->setFieldU32(!bHigh ? sfLowQualityIn : sfHighQualityIn, uQualityIn);

            uLowQualityIn = !bHigh ? uQualityIn : sleRippleState->getFieldU32(sfLowQualityIn);
            uHighQualityIn = bHigh ? uQualityIn : sleRippleState->getFieldU32(sfHighQualityIn);
        }
        else
        {
            // Clearing.

            sleRippleState->makeFieldAbsent(!bHigh ? sfLowQualityIn : sfHighQualityIn);

            uLowQualityIn = !bHigh ? 0 : sleRippleState->getFieldU32(sfLowQualityIn);
            uHighQualityIn = bHigh ? 0 : sleRippleState->getFieldU32(sfHighQualityIn);
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
        else if (uQualityOut != 0u)
        {
            // Setting.

            sleRippleState->setFieldU32(!bHigh ? sfLowQualityOut : sfHighQualityOut, uQualityOut);

            uLowQualityOut = !bHigh ? uQualityOut : sleRippleState->getFieldU32(sfLowQualityOut);
            uHighQualityOut = bHigh ? uQualityOut : sleRippleState->getFieldU32(sfHighQualityOut);
        }
        else
        {
            // Clearing.

            sleRippleState->makeFieldAbsent(!bHigh ? sfLowQualityOut : sfHighQualityOut);

            uLowQualityOut = !bHigh ? 0 : sleRippleState->getFieldU32(sfLowQualityOut);
            uHighQualityOut = bHigh ? 0 : sleRippleState->getFieldU32(sfHighQualityOut);
        }

        std::uint32_t const uFlagsIn(sleRippleState->getFieldU32(sfFlags));
        std::uint32_t uFlagsOut(uFlagsIn);

        if (bSetNoRipple && !bClearNoRipple)
        {
            if ((bHigh ? saHighBalance : saLowBalance) >= beast::kZERO)
            {
                uFlagsOut |= (bHigh ? lsfHighNoRipple : lsfLowNoRipple);
            }
            else
            {
                // Cannot set noRipple on a negative balance.
                return tecNO_PERMISSION;
            }
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

        bool const bLowDefRipple = sleLowAccount->isFlag(lsfDefaultRipple);
        bool const bHighDefRipple = sleHighAccount->isFlag(lsfDefaultRipple);

        bool const bLowReserveSet = (uLowQualityIn != 0u) || (uLowQualityOut != 0u) ||
            ((uFlagsOut & lsfLowNoRipple) == 0) != bLowDefRipple ||
            ((uFlagsOut & lsfLowFreeze) != 0u) || saLowLimit || saLowBalance > beast::kZERO;
        bool const bLowReserveClear = !bLowReserveSet;

        bool const bHighReserveSet = (uHighQualityIn != 0u) || (uHighQualityOut != 0u) ||
            ((uFlagsOut & lsfHighNoRipple) == 0) != bHighDefRipple ||
            ((uFlagsOut & lsfHighFreeze) != 0u) || saHighLimit || saHighBalance > beast::kZERO;
        bool const bHighReserveClear = !bHighReserveSet;

        bool const bDefault = bLowReserveClear && bHighReserveClear;

        bool const bLowReserved = sleRippleState->isFlag(lsfLowReserve);
        bool const bHighReserved = sleRippleState->isFlag(lsfHighReserve);

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

            terResult = trustDelete(view(), sleRippleState, uLowAccountID, uHighAccountID, viewJ);
        }
        // Reserve is not scaled by load.
        else if (bReserveIncrease && preFeeBalance_ < reserveCreate)
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
        !saLimitAmount &&                         // Setting default limit.
        (!bQualityIn || (uQualityIn == 0u)) &&    // Not setting quality in or
                                                  // setting default quality in.
        (!bQualityOut || (uQualityOut == 0u)) &&  // Not setting quality out or
                                                  // setting default quality out.
        (!bSetAuth))
    {
        JLOG(j_.trace()) << "Redundant: Setting non-existent ripple line to defaults.";
        return tecNO_LINE_REDUNDANT;
    }
    else if (preFeeBalance_ < reserveCreate)  // Reserve is not scaled by
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
        STAmount const saBalance(Issue{currency, noAccount()});

        auto const k = keylet::line(account_, uDstAccountID, currency);

        JLOG(j_.trace()) << "doTrustSet: Creating ripple line: " << to_string(k.key);

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

void
TrustSet::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
TrustSet::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
