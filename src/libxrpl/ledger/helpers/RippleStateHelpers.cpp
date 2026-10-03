#include <xrpl/ledger/helpers/RippleStateHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/RippleStateEntry.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/AmountConversions.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>

#include <algorithm>
#include <memory>
#include <optional>

namespace xrpl {

//------------------------------------------------------------------------------
//
// Credit functions (from Credit.cpp)
//
//------------------------------------------------------------------------------

STAmount
creditLimit(
    ReadView const& view,
    AccountID const& account,
    AccountID const& issuer,
    Currency const& currency)
{
    STAmount result(Issue{currency, account});

    RippleStateEntryR const sleRippleState(account, issuer, currency, view);

    if (sleRippleState)
        result = sleRippleState.creditLimit(account, issuer);

    XRPL_ASSERT(result.getIssuer() == account, "xrpl::creditLimit : result issuer match");
    XRPL_ASSERT(
        result.get<Issue>().currency == currency,
        "xrpl::creditLimit : result currency "
        "match");
    return result;
}

IOUAmount
creditLimit2(ReadView const& v, AccountID const& acc, AccountID const& iss, Currency const& cur)
{
    return toAmount<IOUAmount>(creditLimit(v, acc, iss, cur));
}

STAmount
creditBalance(
    ReadView const& view,
    AccountID const& account,
    AccountID const& issuer,
    Currency const& currency)
{
    STAmount result(Issue{currency, account});

    RippleStateEntryR const sleRippleState(account, issuer, currency, view);

    if (sleRippleState)
        result = sleRippleState.creditBalance(account, issuer);

    XRPL_ASSERT(result.getIssuer() == account, "xrpl::creditBalance : result issuer match");
    XRPL_ASSERT(
        result.get<Issue>().currency == currency,
        "xrpl::creditBalance : result currency "
        "match");
    return result;
}

//------------------------------------------------------------------------------
//
// Freeze checking (IOU-specific)
//
//------------------------------------------------------------------------------

bool
isIndividualFrozen(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer)
{
    if (isXRP(currency))
        return false;
    if (issuer != account)
    {
        // Check if the issuer froze the line
        RippleStateEntryR const sle(account, issuer, currency, view);
        if (sle && sle.isIndividualFrozen(account, issuer))
            return true;
    }
    return false;
}

// Can the specified account spend the specified currency issued by
// the specified issuer or does the freeze flag prohibit it?
bool
isFrozen(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer)
{
    if (isXRP(currency))
        return false;
    auto sle = view.read(keylet::account(issuer));
    if (sle && sle->isFlag(lsfGlobalFreeze))
        return true;
    if (issuer != account)
    {
        // Check if the issuer froze the line
        RippleStateEntryR const sleLine(account, issuer, currency, view);
        if (sleLine && sleLine.isIndividualFrozen(account, issuer))
            return true;
    }
    return false;
}

bool
isDeepFrozen(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer)
{
    if (isXRP(currency))
    {
        return false;
    }

    if (issuer == account)
    {
        return false;
    }

    RippleStateEntryR const sle(account, issuer, currency, view);
    if (!sle)
    {
        return false;
    }

    return sle.isDeepFrozen();
}

//------------------------------------------------------------------------------
//
// IOU issuance/redemption
//
//------------------------------------------------------------------------------

static bool
updateTrustLine(
    ApplyView& view,
    RippleStateEntryW& state,
    bool bSenderHigh,
    AccountID const& sender,
    STAmount const& before,
    STAmount const& after,
    beast::Journal j)
{
    if (!state)
        return false;

    auto sle = view.peek(keylet::account(sender));
    if (!sle)
        return false;

    auto const senderReserveFlag = bSenderHigh ? lsfHighReserve : lsfLowReserve;
    auto const senderNoRippleFlag = bSenderHigh ? lsfHighNoRipple : lsfLowNoRipple;
    auto const senderFreezeFlag = bSenderHigh ? lsfHighFreeze : lsfLowFreeze;
    auto const receiverReserveFlag = bSenderHigh ? lsfLowReserve : lsfHighReserve;

    // YYY Could skip this if rippling in reverse.
    if (before > beast::kZero
        // Sender balance was positive.
        && after <= beast::kZero
        // Sender is zero or negative.
        && state->isFlag(senderReserveFlag)
        // Sender reserve is set.
        && state->isFlag(senderNoRippleFlag) != sle->isFlag(lsfDefaultRipple) &&
        !state->isFlag(senderFreezeFlag) &&
        !state->getFieldAmount(!bSenderHigh ? sfLowLimit : sfHighLimit)
        // Sender trust limit is 0.
        && (state->getFieldU32(!bSenderHigh ? sfLowQualityIn : sfHighQualityIn) == 0u)
        // Sender quality in is 0.
        && (state->getFieldU32(!bSenderHigh ? sfLowQualityOut : sfHighQualityOut) == 0u))
    // Sender quality out is 0.
    {
        // VFALCO Where is the line being deleted?
        // Clear the reserve of the sender, possibly delete the line!
        auto const currentSponsor = getLedgerEntryReserveSponsor(
            view, state.rawSle(), bSenderHigh ? sfHighSponsor : sfLowSponsor);
        decreaseOwnerCount(view, sle, currentSponsor, 1, j);

        // Clear reserve flag.
        state->clearFlag(senderReserveFlag);

        removeSponsorFromLedgerEntry(
            state.mutableRawSle(), !bSenderHigh ? sfLowSponsor : sfHighSponsor);

        // Balance is zero, receiver reserve is clear.
        if (!after && !state->isFlag(receiverReserveFlag))
            return true;
    }
    return false;
}

// Only used in tests
TER
issueIOU(
    ApplyView& view,
    AccountID const& account,
    STAmount const& amount,
    Issue const& issue,
    SLE::Ref sponsorSle,
    beast::Journal j)
{
    XRPL_ASSERT(
        !isXRP(account) && !isXRP(issue.account),
        "xrpl::issueIOU : neither account nor issuer is XRP");

    // Consistency check
    XRPL_ASSERT(issue == amount.get<Issue>(), "xrpl::issueIOU : matching issue");

    // Can't send to self!
    XRPL_ASSERT(issue.account != account, "xrpl::issueIOU : not issuer account");

    JLOG(j.trace()) << "issueIOU: " << to_string(account) << ": " << amount.getFullText();

    bool const bSenderHigh = issue.account > account;

    auto const index = keylet::trustLine(issue.account, account, issue.currency);

    if (RippleStateEntryW state(index, view, j); state)
    {
        STAmount finalBalance = state->getFieldAmount(sfBalance);

        if (bSenderHigh)
            finalBalance.negate();  // Put balance in sender terms.

        STAmount const startBalance = finalBalance;

        finalBalance -= amount;

        auto const mustDelete =
            updateTrustLine(view, state, bSenderHigh, issue.account, startBalance, finalBalance, j);

        view.creditHookIOU(issue.account, account, amount, startBalance);

        if (bSenderHigh)
            finalBalance.negate();

        // Adjust the balance on the trust line if necessary. We do this even
        // if we are going to delete the line to reflect the correct balance
        // at the time of deletion.
        state->setFieldAmount(sfBalance, finalBalance);
        if (mustDelete)
        {
            return state.removeFromLedger(
                bSenderHigh ? account : issue.account, bSenderHigh ? issue.account : account);
        }

        state.update();

        return tesSUCCESS;
    }

    // NIKB TODO: The limit uses the receiver's account as the issuer and
    // this is unnecessarily inefficient as copying which could be avoided
    // is now required. Consider available options.
    STAmount const limit(Issue{issue.currency, account});
    STAmount finalBalance = amount;

    finalBalance.get<Issue>().account = noAccount();

    auto const receiverAccount = view.peek(keylet::account(account));
    if (!receiverAccount)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    bool const noRipple = !receiverAccount->isFlag(lsfDefaultRipple);

    return RippleStateEntryW::create(
        view,
        bSenderHigh,
        issue.account,
        account,
        index.key,
        receiverAccount,
        false,
        noRipple,
        false,
        false,
        finalBalance,
        limit,
        0,
        0,
        sponsorSle,
        j);
}

TER
redeemIOU(
    ApplyView& view,
    AccountID const& account,
    STAmount const& amount,
    Issue const& issue,
    beast::Journal j)
{
    XRPL_ASSERT(
        !isXRP(account) && !isXRP(issue.account),
        "xrpl::redeemIOU : neither account nor issuer is XRP");

    // Consistency check
    XRPL_ASSERT(issue == amount.get<Issue>(), "xrpl::redeemIOU : matching issue");

    // Can't send to self!
    XRPL_ASSERT(issue.account != account, "xrpl::redeemIOU : not issuer account");

    JLOG(j.trace()) << "redeemIOU: " << to_string(account) << ": " << amount.getFullText();

    bool const bSenderHigh = account > issue.account;

    if (RippleStateEntryW state(account, issue.account, issue.currency, view, j); state)
    {
        STAmount finalBalance = state->getFieldAmount(sfBalance);

        if (bSenderHigh)
            finalBalance.negate();  // Put balance in sender terms.

        STAmount const startBalance = finalBalance;

        finalBalance -= amount;

        auto const mustDelete =
            updateTrustLine(view, state, bSenderHigh, account, startBalance, finalBalance, j);

        view.creditHookIOU(account, issue.account, amount, startBalance);

        if (bSenderHigh)
            finalBalance.negate();

        // Adjust the balance on the trust line if necessary. We do this even
        // if we are going to delete the line to reflect the correct balance
        // at the time of deletion.
        state->setFieldAmount(sfBalance, finalBalance);

        if (mustDelete)
        {
            return state.removeFromLedger(
                bSenderHigh ? issue.account : account, bSenderHigh ? account : issue.account);
        }

        state.update();
        return tesSUCCESS;
    }

    // In order to hold an IOU, a trust line *MUST* exist to track the
    // balance. If it doesn't, then something is very wrong. Don't try
    // to continue.
    // LCOV_EXCL_START
    JLOG(j.fatal()) << "redeemIOU: " << to_string(account) << " attempts to "
                    << "redeem " << amount.getFullText() << " but no trust line exists!";

    return tefINTERNAL;
    // LCOV_EXCL_STOP
}

//------------------------------------------------------------------------------
//
// Authorization and transfer checks (IOU-specific)
//
//------------------------------------------------------------------------------

TER
requireAuth(ReadView const& view, Issue const& issue, AccountID const& account, AuthType authType)
{
    if (isXRP(issue) || issue.account == account)
        return tesSUCCESS;

    RippleStateEntryR const trustLine(account, issue.account, issue.currency, view);
    // If account has no line, and this is a strong check, fail
    if (!trustLine && authType == AuthType::StrongAuth)
        return tecNO_LINE;

    // If this is a weak or legacy check, or if the account has a line, fail if
    // auth is required and not set on the line
    if (auto const issuerAccount = view.read(keylet::account(issue.account));
        issuerAccount && issuerAccount->isFlag(lsfRequireAuth))
    {
        if (trustLine)
        {
            if (trustLine.isAuthorized(account, issue.account))
                return tesSUCCESS;

            // A pseudo-account cannot submit transactions and only stores assets for the object
            // that owns it, so it is implicitly authorized.
            if (view.rules().enabled(fixCleanup3_4_0) && isPseudoAccount(view, account))
                return tesSUCCESS;

            return TER{tecNO_AUTH};
        }
        return TER{tecNO_LINE};
    }

    return tesSUCCESS;
}

TER
canTransfer(ReadView const& view, Issue const& issue, AccountID const& from, AccountID const& to)
{
    if (issue.native())
        return tesSUCCESS;

    auto const& issuerId = issue.getIssuer();
    if (issuerId == from || issuerId == to)
        return tesSUCCESS;
    auto const sleIssuer = view.read(keylet::account(issuerId));
    if (sleIssuer == nullptr)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const isRippleDisabled = [&](AccountID account) -> bool {
        // Line might not exist, but some transfers can create it. If this
        // is the case, just check the default ripple on the issuer account.
        RippleStateEntryR const line(account, issue, view);
        if (line)
        {
            bool const issuerHigh = issuerId > account;
            return line->isFlag(issuerHigh ? lsfHighNoRipple : lsfLowNoRipple);
        }
        return !sleIssuer->isFlag(lsfDefaultRipple);
    };

    // Fail if rippling disabled on both trust lines
    if (isRippleDisabled(from) && isRippleDisabled(to))
        return terNO_RIPPLE;

    return tesSUCCESS;
}

//------------------------------------------------------------------------------
//
// Empty holding operations (IOU-specific)
//
//------------------------------------------------------------------------------

TER
addEmptyHolding(
    ApplyViewContext ctx,
    AccountID const& accountID,
    XRPAmount priorBalance,
    Issue const& issue,
    beast::Journal journal)
{
    // Every account can hold XRP. An issuer can issue directly.
    if (issue.native() || accountID == issue.getIssuer())
        return tesSUCCESS;

    auto const& issuerId = issue.getIssuer();
    auto const& currency = issue.currency;
    auto const& srcId = issuerId;
    auto const& dstId = accountID;
    auto const high = srcId > dstId;
    auto const index = keylet::trustLine(srcId, dstId, currency);
    // Post-fixCleanup3_4_0: an existing line is a no-op. Issuer freeze and
    // DefaultRipple only matter when this function has to create a line.
    bool const fix340Enabled = ctx.view.rules().enabled(fixCleanup3_4_0);
    if (fix340Enabled && ctx.view.exists(index))
        return tecDUPLICATE;

    if (isGlobalFrozen(ctx.view, issuerId))
        return tecFROZEN;  // LCOV_EXCL_LINE

    auto const sleSrc = ctx.view.peek(keylet::account(srcId));
    auto const sleDst = ctx.view.peek(keylet::account(dstId));
    if (!sleDst || !sleSrc)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    // Create path: DefaultRipple is still required. terNO_RIPPLE is
    // intentional so VaultWithdraw / CoverWithdraw fail in preclaim via
    // canAddHolding (retryable, no fee) rather than claiming a tec* fee
    // in doApply. Transactor::operator() will not apply and will not
    // convert it to tefINTERNAL.
    if (!sleSrc->isFlag(lsfDefaultRipple))
        return fix340Enabled ? TER{terNO_RIPPLE} : tecINTERNAL;
    // If the line already exists, don't create it again.
    if (!fix340Enabled && ctx.view.exists(index))
        return tecDUPLICATE;

    // A reserve sponsor only covers tx.Account's own objects.
    auto const sponsorExp = getEffectiveTxReserveSponsor(ctx, sleDst);
    if (!sponsorExp)
        return sponsorExp.error();  // LCOV_EXCL_LINE
    auto const sponsorSle = *sponsorExp;

    // Can the account cover the trust line reserve ?
    if (auto const ret = checkReserve(
            ctx,
            sleDst,
            priorBalance,
            sponsorSle,
            {.ownerCountDelta = 1},
            journal,
            tecNO_LINE_INSUF_RESERVE);
        !isTesSuccess(ret))
    {
        return ret;
    }

    return RippleStateEntryW::create(
        ctx.view,
        high,
        srcId,
        dstId,
        index.key,
        sleDst,
        /*bAuth=*/false,
        /*bNoRipple=*/true,
        /*bFreeze=*/false,
        /*deepFreeze*/ false,
        /*saBalance=*/STAmount{Issue{currency, noAccount()}},
        /*saLimit=*/STAmount{Issue{currency, dstId}},
        /*uQualityIn=*/0,
        /*uQualityOut=*/0,
        sponsorSle,
        journal);
}

TER
removeEmptyHolding(
    ApplyViewContext ctx,
    AccountID const& accountID,
    Issue const& issue,
    beast::Journal journal)
{
    if (issue.native())
    {
        auto const sle = ctx.view.read(keylet::account(accountID));
        if (!sle)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        auto const balance = sle->getFieldAmount(sfBalance);
        if (balance.xrp() != 0)
            return tecHAS_OBLIGATIONS;

        return tesSUCCESS;
    }

    // `asset` is an IOU.
    // If the account is the issuer, then no line should exist. Check anyway.
    // If a line does exist, it will get deleted. If not, return success.
    bool const accountIsIssuer = accountID == issue.account;
    RippleStateEntryW line(accountID, issue, ctx.view, journal);
    if (!line)
        return accountIsIssuer ? (TER)tesSUCCESS : (TER)tecOBJECT_NOT_FOUND;
    if (!accountIsIssuer && line->at(sfBalance)->iou() != beast::kZero)
        return tecHAS_OBLIGATIONS;

    // Adjust the owner count(s)
    if (line->isFlag(lsfLowReserve))
    {
        // Clear reserve for low account.
        auto sleLowAccount = ctx.view.peek(keylet::account(line->at(sfLowLimit)->getIssuer()));
        if (!sleLowAccount)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        auto const currentLowSponsor =
            getLedgerEntryReserveSponsor(ctx.view, line.rawSle(), sfLowSponsor);

        decreaseOwnerCount(ctx.view, sleLowAccount, currentLowSponsor, 1, journal);
        // It's not really necessary to clear the reserve flag, since the line
        // is about to be deleted, but this will make the metadata reflect an
        // accurate state at the time of deletion.
        line->clearFlag(lsfLowReserve);
        removeSponsorFromLedgerEntry(line.mutableRawSle(), sfLowSponsor);
    }

    if (line->isFlag(lsfHighReserve))
    {
        // Clear reserve for high account.
        auto sleHighAccount = ctx.view.peek(keylet::account(line->at(sfHighLimit)->getIssuer()));
        if (!sleHighAccount)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        auto const currentHighSponsor =
            getLedgerEntryReserveSponsor(ctx.view, line.rawSle(), sfHighSponsor);

        decreaseOwnerCount(ctx.view, sleHighAccount, currentHighSponsor, 1, journal);
        // It's not really necessary to clear the reserve flag, since the line
        // is about to be deleted, but this will make the metadata reflect an
        // accurate state at the time of deletion.
        line->clearFlag(lsfHighReserve);
        removeSponsorFromLedgerEntry(line.mutableRawSle(), sfHighSponsor);
    }

    return line.removeFromLedger(
        line->at(sfLowLimit)->getIssuer(), line->at(sfHighLimit)->getIssuer());
}

TER
deleteAMMTrustLine(
    ApplyView& view,
    RippleStateEntryW& sleState,
    std::optional<AccountID> const& ammAccountID,
    beast::Journal j)
{
    if (!sleState || sleState->getType() != ltRIPPLE_STATE)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const& [low, high] = std::minmax(
        sleState->getFieldAmount(sfLowLimit).getIssuer(),
        sleState->getFieldAmount(sfHighLimit).getIssuer());
    auto sleLow = view.peek(keylet::account(low));
    auto sleHigh = view.peek(keylet::account(high));
    if (!sleLow || !sleHigh)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    bool const ammLow = sleLow->isFieldPresent(sfAMMID);
    bool const ammHigh = sleHigh->isFieldPresent(sfAMMID);

    // can't both be AMM
    if (ammLow && ammHigh)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // at least one must be
    if (!ammLow && !ammHigh)
        return terNO_AMM;

    // one must be the target amm
    if (ammAccountID && (low != *ammAccountID && high != *ammAccountID))
        return terNO_AMM;

    auto const sponsorSle = getLedgerEntryReserveSponsor(
        view, sleState.rawSle(), !ammLow ? sfLowSponsor : sfHighSponsor);

    // removeFromLedger() drops the entry's SLE, so read the reserve flag
    // first. removeFromLedger() does not change the flags.
    auto const uFlags = !ammLow ? lsfLowReserve : lsfHighReserve;
    bool const hasReserve = sleState->isFlag(uFlags);

    if (auto const ter = sleState.removeFromLedger(low, high); !isTesSuccess(ter))
    {
        JLOG(j.error()) << "deleteAMMTrustLine: failed to delete the trustline.";
        return ter;
    }

    if (!hasReserve)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    decreaseOwnerCount(view, !ammLow ? sleLow : sleHigh, sponsorSle, 1, j);

    return tesSUCCESS;
}

TER
deleteAMMMPToken(
    ApplyView& view,
    SLE::pointer sleMpt,
    AccountID const& ammAccountID,
    beast::Journal j)
{
    if (!view.dirRemove(
            keylet::ownerDir(ammAccountID), (*sleMpt)[sfOwnerNode], sleMpt->key(), false))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    view.erase(sleMpt);

    return tesSUCCESS;
}

}  // namespace xrpl
