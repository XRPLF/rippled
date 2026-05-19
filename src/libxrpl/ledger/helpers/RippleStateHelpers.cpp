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
        auto const sle = view.read(keylet::line(account, issuer, currency));
        if (sle && sle->isFlag((issuer > account) ? lsfHighFreeze : lsfLowFreeze))
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

//------------------------------------------------------------------------------
//
// Trust line operations
//
//------------------------------------------------------------------------------

TER
trustCreate(
    ApplyView& view,
    bool const bSrcHigh,
    AccountID const& uSrcAccountID,
    AccountID const& uDstAccountID,
    uint256 const& uIndex,      // ripple state entry
    SLE::ref sleAccount,        // the account being set.
    bool const bAuth,           // authorize account.
    bool const bNoRipple,       // others cannot ripple through
    bool const bFreeze,         // funds cannot leave
    bool bDeepFreeze,           // can neither receive nor send funds
    STAmount const& saBalance,  // balance of account being set.
                                // Issuer should be noAccount()
    STAmount const& saLimit,    // limit for account being set.
                                // Issuer should be the account being set.
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

    // Remember deletion hints.
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

    if (!slePeer->isFlag(lsfDefaultRipple))
    {
        // The other side's default is no rippling
        uFlags |= (bSetHigh ? lsfLowNoRipple : lsfHighNoRipple);
    }

    sleRippleState->setFieldU32(sfFlags, uFlags);
    adjustOwnerCount(view, sleAccount, 1, j);

    // ONLY: Create ripple balance.
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
    // Detect legacy dirs.
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

//------------------------------------------------------------------------------
//
// IOU issuance/redemption
//
//------------------------------------------------------------------------------

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
        adjustOwnerCount(view, sle, -1, j);

        // Clear reserve flag.
        state->clearFlag(senderReserveFlag);

        // Balance is zero, receiver reserve is clear.
        if (!after && !state->isFlag(receiverReserveFlag))
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

    // Consistency check
    XRPL_ASSERT(issue == amount.get<Issue>(), "xrpl::issueIOU : matching issue");

    // Can't send to self!
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

        // Adjust the balance on the trust line if necessary. We do this even
        // if we are going to delete the line to reflect the correct balance
        // at the time of deletion.
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

    bool const noRipple = !receiverAccount->isFlag(lsfDefaultRipple);

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

    // Consistency check
    XRPL_ASSERT(issue == amount.get<Issue>(), "xrpl::redeemIOU : matching issue");

    // Can't send to self!
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

        // Adjust the balance on the trust line if necessary. We do this even
        // if we are going to delete the line to reflect the correct balance
        // at the time of deletion.
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

    auto const trustLine = view.read(keylet::line(account, issue.account, issue.currency));
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
            return trustLine->isFlag((account > issue.account) ? lsfLowAuth : lsfHighAuth)
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
        // Line might not exist, but some transfers can create it. If this
        // is the case, just check the default ripple on the issuer account.
        auto const line = view.read(keylet::line(account, issue));
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
    ApplyView& view,
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
    // If the line already exists, don't create it again.
    if (view.read(index))
        return tecDUPLICATE;

    // Can the account cover the trust line reserve ?
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

    // `asset` is an IOU.
    // If the account is the issuer, then no line should exist. Check anyway.
    // If a line does exist, it will get deleted. If not, return success.
    bool const accountIsIssuer = accountID == issue.account;
    auto const line = view.peek(keylet::line(accountID, issue));
    if (!line)
        return accountIsIssuer ? (TER)tesSUCCESS : (TER)tecOBJECT_NOT_FOUND;
    if (!accountIsIssuer && line->at(sfBalance)->iou() != beast::kZero)
        return tecHAS_OBLIGATIONS;

    // Adjust the owner count(s)
    if (line->isFlag(lsfLowReserve))
    {
        // Clear reserve for low account.
        auto sleLowAccount = view.peek(keylet::account(line->at(sfLowLimit)->getIssuer()));
        if (!sleLowAccount)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        adjustOwnerCount(view, sleLowAccount, -1, journal);
        // It's not really necessary to clear the reserve flag, since the line
        // is about to be deleted, but this will make the metadata reflect an
        // accurate state at the time of deletion.
        line->clearFlag(lsfLowReserve);
    }

    if (line->isFlag(lsfHighReserve))
    {
        // Clear reserve for high account.
        auto sleHighAccount = view.peek(keylet::account(line->at(sfHighLimit)->getIssuer()));
        if (!sleHighAccount)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        adjustOwnerCount(view, sleHighAccount, -1, journal);
        // It's not really necessary to clear the reserve flag, since the line
        // is about to be deleted, but this will make the metadata reflect an
        // accurate state at the time of deletion.
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

    // can't both be AMM
    if (ammLow && ammHigh)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // at least one must be
    if (!ammLow && !ammHigh)
        return terNO_AMM;

    // one must be the target amm
    if (ammAccountID && (low != *ammAccountID && high != *ammAccountID))
        return terNO_AMM;

    if (auto const ter = trustDelete(view, sleState, low, high, j); !isTesSuccess(ter))
    {
        JLOG(j.error()) << "deleteAMMTrustLine: failed to delete the trustline.";
        return ter;
    }

    auto const uFlags = !ammLow ? lsfLowReserve : lsfHighReserve;
    if (!sleState->isFlag(uFlags))
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
