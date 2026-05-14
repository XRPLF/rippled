/** @file
 *  Implements trust-line (RippleState) lifecycle and IOU primitives for the
 *  XRP Ledger.
 *
 *  Covers: credit-limit/balance queries, freeze enforcement, trust-line
 *  creation and deletion, IOU issuance and redemption (including automatic
 *  reserve cleanup), authorization checks, zero-balance holding management,
 *  and AMM-specific cleanup operations.  Originally split across Credit.cpp
 *  and this file; the two halves were merged and the section banners below
 *  mark the original boundaries.
 *
 *  @note The `sfBalance` field on every `ltRIPPLE_STATE` SLE is stored from
 *      the low account's perspective (positive = low account holds the IOU).
 *      Every function that exposes a balance to the caller inverts the sign
 *      when the querying account is the high side.
 */
#include <xrpl/ledger/helpers/RippleStateHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
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
#include <cstdint>
#include <memory>
#include <optional>

namespace xrpl {

// --- Credit queries (merged from Credit.cpp) ---

STAmount
creditLimit(
    ReadView const& view,
    AccountID const& account,
    AccountID const& issuer,
    Currency const& currency)
{
    STAmount result(Issue{currency, account});

    auto sleRippleState = view.read(keylet::line(account, issuer, currency));

    if (sleRippleState)
    {
        result = sleRippleState->getFieldAmount(account < issuer ? sfLowLimit : sfHighLimit);
        result.get<Issue>().account = account;
    }

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

    auto sleRippleState = view.read(keylet::line(account, issuer, currency));

    if (sleRippleState)
    {
        result = sleRippleState->getFieldAmount(sfBalance);
        if (account < issuer)
            result.negate();
        result.get<Issue>().account = account;
    }

    XRPL_ASSERT(result.getIssuer() == account, "xrpl::creditBalance : result issuer match");
    XRPL_ASSERT(
        result.get<Issue>().currency == currency,
        "xrpl::creditBalance : result currency "
        "match");
    return result;
}

// --- Freeze checks (IOU-specific) ---

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
        auto const sle = view.read(keylet::line(account, issuer, currency));
        if (sle && sle->isFlag((issuer > account) ? lsfHighFreeze : lsfLowFreeze))
            return true;
    }
    return false;
}

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
        sle = view.read(keylet::line(account, issuer, currency));
        if (sle && sle->isFlag((issuer > account) ? lsfHighFreeze : lsfLowFreeze))
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

    auto const sle = view.read(keylet::line(account, issuer, currency));
    if (!sle)
    {
        return false;
    }

    return sle->isFlag(lsfHighDeepFreeze) || sle->isFlag(lsfLowDeepFreeze);
}

// --- Trust-line lifecycle ---

TER
trustCreate(
    ApplyView& view,
    bool const bSrcHigh,
    AccountID const& uSrcAccountID,
    AccountID const& uDstAccountID,
    uint256 const& uIndex,
    SLE::ref sleAccount,
    bool const bAuth,
    bool const bNoRipple,
    bool const bFreeze,
    bool bDeepFreeze,
    STAmount const& saBalance,
    STAmount const& saLimit,
    std::uint32_t uQualityIn,
    std::uint32_t uQualityOut,
    beast::Journal j)
{
    JLOG(j.trace()) << "trustCreate: " << to_string(uSrcAccountID) << ", "
                    << to_string(uDstAccountID) << ", " << saBalance.getFullText();

    auto const& uLowAccountID = !bSrcHigh ? uSrcAccountID : uDstAccountID;
    auto const& uHighAccountID = bSrcHigh ? uSrcAccountID : uDstAccountID;
    if (uLowAccountID == uHighAccountID)
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::trustCreate : trust line to self");
        if (view.rules().enabled(featureLendingProtocol))
            return tecINTERNAL;
        // LCOV_EXCL_STOP
    }

    auto const sleRippleState = std::make_shared<SLE>(ltRIPPLE_STATE, uIndex);
    view.insert(sleRippleState);

    auto lowNode = view.dirInsert(
        keylet::ownerDir(uLowAccountID), sleRippleState->key(), describeOwnerDir(uLowAccountID));

    if (!lowNode)
        return tecDIR_FULL;  // LCOV_EXCL_LINE

    auto highNode = view.dirInsert(
        keylet::ownerDir(uHighAccountID), sleRippleState->key(), describeOwnerDir(uHighAccountID));

    if (!highNode)
        return tecDIR_FULL;  // LCOV_EXCL_LINE

    bool const bSetDst = saLimit.getIssuer() == uDstAccountID;
    bool const bSetHigh = bSrcHigh ^ bSetDst;

    XRPL_ASSERT(sleAccount, "xrpl::trustCreate : non-null SLE");
    if (!sleAccount)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    XRPL_ASSERT(
        sleAccount->getAccountID(sfAccount) == (bSetHigh ? uHighAccountID : uLowAccountID),
        "xrpl::trustCreate : matching account ID");
    auto const slePeer = view.peek(keylet::account(bSetHigh ? uLowAccountID : uHighAccountID));
    if (!slePeer)
        return tecNO_TARGET;

    sleRippleState->setFieldU64(sfLowNode, *lowNode);
    sleRippleState->setFieldU64(sfHighNode, *highNode);

    sleRippleState->setFieldAmount(bSetHigh ? sfHighLimit : sfLowLimit, saLimit);
    sleRippleState->setFieldAmount(
        bSetHigh ? sfLowLimit : sfHighLimit,
        STAmount(Issue{saBalance.get<Issue>().currency, bSetDst ? uSrcAccountID : uDstAccountID}));

    if (uQualityIn != 0u)
        sleRippleState->setFieldU32(bSetHigh ? sfHighQualityIn : sfLowQualityIn, uQualityIn);

    if (uQualityOut != 0u)
        sleRippleState->setFieldU32(bSetHigh ? sfHighQualityOut : sfLowQualityOut, uQualityOut);

    std::uint32_t uFlags = bSetHigh ? lsfHighReserve : lsfLowReserve;

    if (bAuth)
    {
        uFlags |= (bSetHigh ? lsfHighAuth : lsfLowAuth);
    }
    if (bNoRipple)
    {
        uFlags |= (bSetHigh ? lsfHighNoRipple : lsfLowNoRipple);
    }
    if (bFreeze)
    {
        uFlags |= (bSetHigh ? lsfHighFreeze : lsfLowFreeze);
    }
    if (bDeepFreeze)
    {
        uFlags |= (bSetHigh ? lsfHighDeepFreeze : lsfLowDeepFreeze);
    }

    if ((slePeer->getFlags() & lsfDefaultRipple) == 0)
    {
        // Propagate the peer's preference: absent lsfDefaultRipple means the
        // peer wants noRipple on by default for any new line.
        uFlags |= (bSetHigh ? lsfLowNoRipple : lsfHighNoRipple);
    }

    sleRippleState->setFieldU32(sfFlags, uFlags);
    adjustOwnerCount(view, sleAccount, 1, j);

    sleRippleState->setFieldAmount(sfBalance, bSetHigh ? -saBalance : saBalance);

    view.creditHookIOU(uSrcAccountID, uDstAccountID, saBalance, saBalance.zeroed());

    return tesSUCCESS;
}

TER
trustDelete(
    ApplyView& view,
    std::shared_ptr<SLE> const& sleRippleState,
    AccountID const& uLowAccountID,
    AccountID const& uHighAccountID,
    beast::Journal j)
{
    std::uint64_t const uLowNode = sleRippleState->getFieldU64(sfLowNode);
    std::uint64_t const uHighNode = sleRippleState->getFieldU64(sfHighNode);

    JLOG(j.trace()) << "trustDelete: Deleting ripple line: low";

    if (!view.dirRemove(keylet::ownerDir(uLowAccountID), uLowNode, sleRippleState->key(), false))
    {
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    }

    JLOG(j.trace()) << "trustDelete: Deleting ripple line: high";

    if (!view.dirRemove(keylet::ownerDir(uHighAccountID), uHighNode, sleRippleState->key(), false))
    {
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    }

    JLOG(j.trace()) << "trustDelete: Deleting ripple line: state";
    view.erase(sleRippleState);

    return tesSUCCESS;
}

// --- IOU issuance / redemption ---

/** Opportunistically release the sender's reserve and signal line deletion.
 *
 *  Called by `issueIOU` and `redeemIOU` after every balance mutation.
 *  Releases the sender's owner-count reserve and clears `lsfLowReserve` /
 *  `lsfHighReserve` when **all** of the following are true:
 *    1. The sender's balance transitioned from positive to zero or negative.
 *    2. The sender's reserve flag is currently set.
 *    3. The sender's `lsfNoRipple` state disagrees with the issuer's
 *       `lsfDefaultRipple` — meaning neither side wants this line kept alive.
 *    4. The sender's side of the line is not frozen.
 *    5. The sender's trust limit is zero.
 *    6. The sender's `sfLowQualityIn` / `sfHighQualityIn` is zero.
 *    7. The sender's `sfLowQualityOut` / `sfHighQualityOut` is zero.
 *
 *  Returns `true` only when the reserve was cleared *and* the final balance
 *  is zero *and* the peer's reserve flag is also clear — meaning neither
 *  side holds a stake in the line and the caller should delete it.  The
 *  caller must write the final balance onto the SLE before calling
 *  `trustDelete` so the deletion metadata captures accurate state.
 *
 *  @param view       Mutable ledger view.
 *  @param state      The `ltRIPPLE_STATE` SLE, already peeked from @p view.
 *  @param bSenderHigh `true` if the sender occupies the high slot.
 *  @param sender     The account sending IOUs (issuer in `issueIOU`,
 *      holder in `redeemIOU`).
 *  @param before     Sender's balance before the transfer (sender perspective).
 *  @param after      Sender's balance after the transfer (sender perspective).
 *  @param j          Journal for trace/debug logging.
 *  @return `true` if the trust line should be deleted (neither side has a
 *      reserve and the balance is zero), `false` otherwise.
 */
static bool
updateTrustLine(
    ApplyView& view,
    SLE::pointer state,
    bool bSenderHigh,
    AccountID const& sender,
    STAmount const& before,
    STAmount const& after,
    beast::Journal j)
{
    if (!state)
        return false;
    std::uint32_t const flags(state->getFieldU32(sfFlags));

    auto sle = view.peek(keylet::account(sender));
    if (!sle)
        return false;

    if (before > beast::kZERO && after <= beast::kZERO &&
        ((flags & (!bSenderHigh ? lsfLowReserve : lsfHighReserve)) != 0u) &&
        static_cast<bool>(flags & (!bSenderHigh ? lsfLowNoRipple : lsfHighNoRipple)) !=
            static_cast<bool>(sle->getFlags() & lsfDefaultRipple) &&
        ((flags & (!bSenderHigh ? lsfLowFreeze : lsfHighFreeze)) == 0u) &&
        !state->getFieldAmount(!bSenderHigh ? sfLowLimit : sfHighLimit) &&
        (state->getFieldU32(!bSenderHigh ? sfLowQualityIn : sfHighQualityIn) == 0u) &&
        (state->getFieldU32(!bSenderHigh ? sfLowQualityOut : sfHighQualityOut) == 0u))
    {
        adjustOwnerCount(view, sle, -1, j);
        state->setFieldU32(sfFlags, flags & (!bSenderHigh ? ~lsfLowReserve : ~lsfHighReserve));

        // Neither side holds a stake: caller should delete the line.
        if (!after && ((flags & (bSenderHigh ? lsfLowReserve : lsfHighReserve)) == 0u))
            return true;
    }
    return false;
}

TER
issueIOU(
    ApplyView& view,
    AccountID const& account,
    STAmount const& amount,
    Issue const& issue,
    beast::Journal j)
{
    XRPL_ASSERT(
        !isXRP(account) && !isXRP(issue.account),
        "xrpl::issueIOU : neither account nor issuer is XRP");
    XRPL_ASSERT(issue == amount.get<Issue>(), "xrpl::issueIOU : matching issue");
    XRPL_ASSERT(issue.account != account, "xrpl::issueIOU : not issuer account");

    JLOG(j.trace()) << "issueIOU: " << to_string(account) << ": " << amount.getFullText();

    bool const bSenderHigh = issue.account > account;

    auto const index = keylet::line(issue.account, account, issue.currency);

    if (auto state = view.peek(index))
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

        // Write the final balance even when mustDelete is true: deletion
        // metadata must reflect accurate state at the moment of removal.
        state->setFieldAmount(sfBalance, finalBalance);
        if (mustDelete)
        {
            return trustDelete(
                view,
                state,
                bSenderHigh ? account : issue.account,
                bSenderHigh ? issue.account : account,
                j);
        }

        view.update(state);

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

    bool const noRipple = (receiverAccount->getFlags() & lsfDefaultRipple) == 0;

    return trustCreate(
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
    XRPL_ASSERT(issue == amount.get<Issue>(), "xrpl::redeemIOU : matching issue");
    XRPL_ASSERT(issue.account != account, "xrpl::redeemIOU : not issuer account");

    JLOG(j.trace()) << "redeemIOU: " << to_string(account) << ": " << amount.getFullText();

    bool const bSenderHigh = account > issue.account;

    if (auto state = view.peek(keylet::line(account, issue.account, issue.currency)))
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

        // Write the final balance even when mustDelete is true: deletion
        // metadata must reflect accurate state at the moment of removal.
        state->setFieldAmount(sfBalance, finalBalance);

        if (mustDelete)
        {
            return trustDelete(
                view,
                state,
                bSenderHigh ? issue.account : account,
                bSenderHigh ? account : issue.account,
                j);
        }

        view.update(state);
        return tesSUCCESS;
    }

    // A holder cannot redeem a balance without an existing trust line —
    // ledger state is corrupt if we reach here.
    // LCOV_EXCL_START
    JLOG(j.fatal()) << "redeemIOU: " << to_string(account) << " attempts to "
                    << "redeem " << amount.getFullText() << " but no trust line exists!";

    return tefINTERNAL;
    // LCOV_EXCL_STOP
}

// --- Authorization and transfer checks (IOU-specific) ---

TER
requireAuth(ReadView const& view, Issue const& issue, AccountID const& account, AuthType authType)
{
    if (isXRP(issue) || issue.account == account)
        return tesSUCCESS;

    auto const trustLine = view.read(keylet::line(account, issue.account, issue.currency));
    if (!trustLine && authType == AuthType::StrongAuth)
        return tecNO_LINE;

    if (auto const issuerAccount = view.read(keylet::account(issue.account));
        issuerAccount && (((*issuerAccount)[sfFlags] & lsfRequireAuth) != 0u))
    {
        if (trustLine)
        {
            return (((*trustLine)[sfFlags] &
                     ((account > issue.account) ? lsfLowAuth : lsfHighAuth)) != 0u)
                ? tesSUCCESS
                : TER{tecNO_AUTH};
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
        // A payment may create the line on the fly; if none exists yet, fall
        // back to the issuer's lsfDefaultRipple as the "intended" state.
        auto const line = view.read(keylet::line(account, issue));
        if (line)
        {
            bool const issuerHigh = issuerId > account;
            return line->isFlag(issuerHigh ? lsfHighNoRipple : lsfLowNoRipple);
        }
        return !sleIssuer->isFlag(lsfDefaultRipple);
    };

    if (isRippleDisabled(from) && isRippleDisabled(to))
        return terNO_RIPPLE;

    return tesSUCCESS;
}

// --- Empty holding operations (IOU-specific) ---

TER
addEmptyHolding(
    ApplyView& view,
    AccountID const& accountID,
    XRPAmount priorBalance,
    Issue const& issue,
    beast::Journal journal)
{
    if (issue.native() || accountID == issue.getIssuer())
        return tesSUCCESS;

    auto const& issuerId = issue.getIssuer();
    auto const& currency = issue.currency;
    if (isGlobalFrozen(view, issuerId))
        return tecFROZEN;  // LCOV_EXCL_LINE

    auto const& srcId = issuerId;
    auto const& dstId = accountID;
    auto const high = srcId > dstId;
    auto const index = keylet::line(srcId, dstId, currency);
    auto const sleSrc = view.peek(keylet::account(srcId));
    auto const sleDst = view.peek(keylet::account(dstId));
    if (!sleDst || !sleSrc)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    if (!sleSrc->isFlag(lsfDefaultRipple))
        return tecINTERNAL;  // LCOV_EXCL_LINE
    if (view.read(index))
        return tecDUPLICATE;

    std::uint32_t const ownerCount = sleDst->at(sfOwnerCount);
    if (priorBalance < view.fees().accountReserve(ownerCount + 1))
        return tecNO_LINE_INSUF_RESERVE;

    return trustCreate(
        view,
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
        journal);
}

TER
removeEmptyHolding(
    ApplyView& view,
    AccountID const& accountID,
    Issue const& issue,
    beast::Journal journal)
{
    if (issue.native())
    {
        auto const sle = view.read(keylet::account(accountID));
        if (!sle)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        auto const balance = sle->getFieldAmount(sfBalance);
        if (balance.xrp() != 0)
            return tecHAS_OBLIGATIONS;

        return tesSUCCESS;
    }

    bool const accountIsIssuer = accountID == issue.account;
    auto const line = view.peek(keylet::line(accountID, issue));
    if (!line)
        return accountIsIssuer ? (TER)tesSUCCESS : (TER)tecOBJECT_NOT_FOUND;
    if (!accountIsIssuer && line->at(sfBalance)->iou() != beast::kZERO)
        return tecHAS_OBLIGATIONS;

    if (line->isFlag(lsfLowReserve))
    {
        auto sleLowAccount = view.peek(keylet::account(line->at(sfLowLimit)->getIssuer()));
        if (!sleLowAccount)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        adjustOwnerCount(view, sleLowAccount, -1, journal);
        // Clear now so deletion metadata reflects accurate owner-count state.
        line->clearFlag(lsfLowReserve);
    }

    if (line->isFlag(lsfHighReserve))
    {
        auto sleHighAccount = view.peek(keylet::account(line->at(sfHighLimit)->getIssuer()));
        if (!sleHighAccount)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        adjustOwnerCount(view, sleHighAccount, -1, journal);
        // Clear now so deletion metadata reflects accurate owner-count state.
        line->clearFlag(lsfHighReserve);
    }

    return trustDelete(
        view, line, line->at(sfLowLimit)->getIssuer(), line->at(sfHighLimit)->getIssuer(), journal);
}

TER
deleteAMMTrustLine(
    ApplyView& view,
    std::shared_ptr<SLE> sleState,
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

    if (ammLow && ammHigh)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (!ammLow && !ammHigh)
        return terNO_AMM;

    if (ammAccountID && (low != *ammAccountID && high != *ammAccountID))
        return terNO_AMM;

    if (auto const ter = trustDelete(view, sleState, low, high, j); !isTesSuccess(ter))
    {
        JLOG(j.error()) << "deleteAMMTrustLine: failed to delete the trustline.";
        return ter;
    }

    auto const uFlags = !ammLow ? lsfLowReserve : lsfHighReserve;
    if ((sleState->getFlags() & uFlags) == 0u)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    adjustOwnerCount(view, !ammLow ? sleLow : sleHigh, -1, j);

    return tesSUCCESS;
}

TER
deleteAMMMPToken(
    ApplyView& view,
    std::shared_ptr<SLE> sleMpt,
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
