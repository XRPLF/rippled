#include <xrpl/ledger/helpers/RippleStateHelpers.h>
//
#include <xrpl/basics/Log.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/RippleState.h>
#include <xrpl/protocol/AmountConversions.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Rules.h>

namespace xrpl {

//------------------------------------------------------------------------------
//
// Credit functions (from Credit.cpp)
//
//------------------------------------------------------------------------------

STAmount
creditLimit(
    ReadView const& readView_,
    AccountID const& account,
    AccountID const& issuer,
    Currency const& currency)
{
    STAmount result(Issue{currency, account});

    auto sleRippleState = readView_.read(keylet::line(account, issuer, currency));

    if (sleRippleState)
    {
        result = sleRippleState->getFieldAmount(account < issuer ? sfLowLimit : sfHighLimit);
        result.setIssuer(account);
    }

    XRPL_ASSERT(result.getIssuer() == account, "xrpl::creditLimit : result issuer match");
    XRPL_ASSERT(result.getCurrency() == currency, "xrpl::creditLimit : result currency match");
    return result;
}

IOUAmount
creditLimit2(ReadView const& v, AccountID const& acc, AccountID const& iss, Currency const& cur)
{
    return toAmount<IOUAmount>(creditLimit(v, acc, iss, cur));
}

STAmount
creditBalance(
    ReadView const& readView_,
    AccountID const& account,
    AccountID const& issuer,
    Currency const& currency)
{
    STAmount result(Issue{currency, account});

    auto sleRippleState = readView_.read(keylet::line(account, issuer, currency));

    if (sleRippleState)
    {
        result = sleRippleState->getFieldAmount(sfBalance);
        if (account < issuer)
            result.negate();
        result.setIssuer(account);
    }

    XRPL_ASSERT(result.getIssuer() == account, "xrpl::creditBalance : result issuer match");
    XRPL_ASSERT(result.getCurrency() == currency, "xrpl::creditBalance : result currency match");
    return result;
}

//------------------------------------------------------------------------------
//
// Freeze checking (IOU-specific)
//
//------------------------------------------------------------------------------

bool
IOUToken::isIndividualFrozen(AccountID const& account) const
{
    if (isXRP(currency_))
        return false;
    if (issuer_ != account)
    {
        // Check if the issuer froze the line
        auto const sle = readView_.read(keylet::line(account, issuer_, currency_));
        if (sle && sle->isFlag((issuer_ > account) ? lsfHighFreeze : lsfLowFreeze))
            return true;
    }
    return false;
}

// Can the specified account spend the specified currency issued by
// the specified issuer or does the freeze flag prohibit it?
bool
IOUToken::isFrozen(AccountID const& account, int depth) const
{
    // NOTE: depth is ignored here because it's only relevant for MPTs
    if (isXRP(currency_))
        return false;
    if (issuerAccount_.exists() && issuerAccount_->isFlag(lsfGlobalFreeze))
        return true;
    if (issuer_ != account)
    {
        // Check if the issuer froze the line
        auto const sleLine = readView_.read(keylet::line(account, issuer_, currency_));
        if (sleLine && sleLine->isFlag((issuer_ > account) ? lsfHighFreeze : lsfLowFreeze))
            return true;
    }
    return false;
}

bool
IOUToken::isDeepFrozen(AccountID const& account, int depth) const
{
    // NOTE: depth is ignored here because it's only relevant for MPTs
    if (isXRP(currency_))
    {
        return false;
    }

    if (issuer_ == account)
    {
        return false;
    }

    auto const sle = readView_.read(keylet::line(account, issuer_, currency_));
    if (!sle)
    {
        return false;
    }

    return sle->isFlag(lsfHighDeepFreeze) || sle->isFlag(lsfLowDeepFreeze);
}

TER
IOUToken::checkFrozen(AccountID const& account) const
{
    return isFrozen(account) ? TER{tecFROZEN} : TER{tesSUCCESS};
}

TER
IOUToken::checkDeepFrozen(AccountID const& account) const
{
    return isDeepFrozen(account) ? TER{tecFROZEN} : TER{tesSUCCESS};
}

bool
IOUToken::isAnyFrozen(std::initializer_list<AccountID> const& accounts, int depth) const
{
    // NOTE: depth is ignored here because it's only relevant for MPTs
    if (isGlobalFrozen())
        return true;

    for (auto const& account : accounts)
    {
        if (isFrozen(account, depth))
            return true;
    }

    return false;
}

STAmount
IOUToken::accountFunds(
    AccountID const& id,
    STAmount const& saDefault,
    FreezeHandling freezeHandling,
    beast::Journal j) const
{
    if (!saDefault.native() && saDefault.getIssuer() == id)
        return saDefault;

    return accountHolds(id, freezeHandling, j);
}

STAmount
IOUToken::accountHolds(
    AccountID const& account,
    FreezeHandling zeroIfFrozen,
    beast::Journal j,
    SpendableHandling includeFullBalance) const
{
    return accountHolds(account, zeroIfFrozen, ahIGNORE_AUTH, j, includeFullBalance);
}

STAmount
IOUToken::accountHolds(
    AccountID const& account,
    FreezeHandling zeroIfFrozen,
    AuthHandling zeroIfUnauthorized,
    beast::Journal j,
    SpendableHandling includeFullBalance) const
{
    if (isXRP(currency_))
    {
        AccountRoot accountRoot(account, readView_);
        return {accountRoot.xrpLiquid(0, j)};
    }

    bool const returnSpendable = (includeFullBalance == shFULL_BALANCE);
    if (returnSpendable && account == issuer_)
        // If the account is the issuer, then their limit is effectively
        // infinite
        return STAmount{issue_, STAmount::cMaxValue, STAmount::cMaxOffset};

    // IOU: Return balance on trust line modulo freeze
    // Check if line exists and is usable (mirrors old getLineIfUsable)
    SLE::const_pointer sle = readView_.read(keylet::line(account, issuer_, currency_));

    if (sle && zeroIfFrozen == fhZERO_IF_FROZEN)
    {
        if (isFrozen(account) || isDeepFrozen(account))
        {
            sle = nullptr;
        }

        // when fixFrozenLPTokenTransfer is enabled, if currency is lptoken,
        // we need to check if the associated assets have been frozen
        if (sle && readView_.rules().enabled(fixFrozenLPTokenTransfer))
        {
            auto const sleIssuer = readView_.read(keylet::account(issuer_));
            if (!sleIssuer)
            {
                sle = nullptr;  // LCOV_EXCL_LINE
            }
            else if (sleIssuer->isFieldPresent(sfAMMID))
            {
                auto const sleAmm = readView_.read(keylet::amm((*sleIssuer)[sfAMMID]));

                if (!sleAmm ||
                    isLPTokenFrozen(
                        readView_,
                        account,
                        (*sleAmm)[sfAsset].get<Issue>(),
                        (*sleAmm)[sfAsset2].get<Issue>()))
                {
                    sle = nullptr;
                }
            }
        }
    }

    // Extract balance (mirrors old getTrustLineBalance)
    STAmount amount;
    if (sle)
    {
        amount = sle->getFieldAmount(sfBalance);
        bool const accountHigh = account > issuer_;
        auto const& oppositeField = accountHigh ? sfLowLimit : sfHighLimit;
        if (accountHigh)
        {
            // Put balance in account terms.
            amount.negate();
        }
        if (returnSpendable)
        {
            amount += sle->getFieldAmount(oppositeField);
        }
        amount.setIssuer(issuer_);
    }
    else
    {
        amount.clear(Issue{currency_, issuer_});
    }

    JLOG(j.trace()) << "IOUToken::accountHolds:" << " account=" << to_string(account)
                    << " amount=" << amount.getFullText();

    return readView_.balanceHook(account, issuer_, amount);
}

TER
IOUToken::canAddHolding() const
{
    if (isXRP(issue_))
        return tesSUCCESS;

    if (!issuerAccount_.exists())
        return terNO_ACCOUNT;

    if (!issuerAccount_->isFlag(lsfDefaultRipple))
        return terNO_RIPPLE;

    return tesSUCCESS;
}

Rate
IOUToken::transferRate() const
{
    return issuerAccount_.transferRate();
}

//------------------------------------------------------------------------------
//
// Trust line operations
//
//------------------------------------------------------------------------------

TER
WritableRippleState::trustCreate(
    ApplyView& view,
    bool const bSrcHigh,
    AccountID const& uSrcAccountID,
    AccountID const& uDstAccountID,
    uint256 const& uIndex,             // ripple state entry
    WritableAccountRoot& wrappedAcct,  // the account being set.
    bool const bAuth,                  // authorize account.
    bool const bNoRipple,              // others cannot ripple through
    bool const bFreeze,                // funds cannot leave
    bool bDeepFreeze,                  // can neither receive nor send funds
    STAmount const& saBalance,         // balance of account being set.
                                       // Issuer should be noAccount()
    STAmount const& saLimit,           // limit for account being set.
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

    XRPL_ASSERT(wrappedAcct, "xrpl::trustCreate : non-null SLE");
    if (!wrappedAcct)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    XRPL_ASSERT(
        wrappedAcct->getAccountID(sfAccount) == (bSetHigh ? uHighAccountID : uLowAccountID),
        "xrpl::trustCreate : matching account ID");
    auto const peer = AccountRoot(bSetHigh ? uLowAccountID : uHighAccountID, view);
    if (!peer.exists())
        return tecNO_TARGET;

    // Remember deletion hints.
    sleRippleState->setFieldU64(sfLowNode, *lowNode);
    sleRippleState->setFieldU64(sfHighNode, *highNode);

    sleRippleState->setFieldAmount(bSetHigh ? sfHighLimit : sfLowLimit, saLimit);
    sleRippleState->setFieldAmount(
        bSetHigh ? sfLowLimit : sfHighLimit,
        STAmount(Issue{saBalance.getCurrency(), bSetDst ? uSrcAccountID : uDstAccountID}));

    if (uQualityIn)
        sleRippleState->setFieldU32(bSetHigh ? sfHighQualityIn : sfLowQualityIn, uQualityIn);

    if (uQualityOut)
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

    if ((peer->getFlags() & lsfDefaultRipple) == 0)
    {
        // The other side's default is no rippling
        uFlags |= (bSetHigh ? lsfLowNoRipple : lsfHighNoRipple);
    }

    sleRippleState->setFieldU32(sfFlags, uFlags);
    wrappedAcct.adjustOwnerCount(1, j);

    // ONLY: Create ripple balance.
    sleRippleState->setFieldAmount(sfBalance, bSetHigh ? -saBalance : saBalance);

    view.creditHook(uSrcAccountID, uDstAccountID, saBalance, saBalance.zeroed());

    return tesSUCCESS;
}

TER
WritableRippleState::trustDelete(
    ApplyView& view,
    std::shared_ptr<SLE> const& sleRippleState,
    AccountID const& uLowAccountID,
    AccountID const& uHighAccountID,
    beast::Journal j)
{
    // Detect legacy dirs.
    std::uint64_t uLowNode = sleRippleState->getFieldU64(sfLowNode);
    std::uint64_t uHighNode = sleRippleState->getFieldU64(sfHighNode);

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
    std::uint32_t const flags(state->getFieldU32(sfFlags));

    WritableAccountRoot wrappedAcct(sender, view);
    if (!wrappedAcct)
        return false;

    // YYY Could skip this if rippling in reverse.
    if (before > beast::zero
        // Sender balance was positive.
        && after <= beast::zero
        // Sender is zero or negative.
        && (flags & (!bSenderHigh ? lsfLowReserve : lsfHighReserve))
        // Sender reserve is set.
        && static_cast<bool>(flags & (!bSenderHigh ? lsfLowNoRipple : lsfHighNoRipple)) !=
            static_cast<bool>(wrappedAcct->getFlags() & lsfDefaultRipple) &&
        !(flags & (!bSenderHigh ? lsfLowFreeze : lsfHighFreeze)) &&
        !state->getFieldAmount(!bSenderHigh ? sfLowLimit : sfHighLimit)
        // Sender trust limit is 0.
        && !state->getFieldU32(!bSenderHigh ? sfLowQualityIn : sfHighQualityIn)
        // Sender quality in is 0.
        && !state->getFieldU32(!bSenderHigh ? sfLowQualityOut : sfHighQualityOut))
    // Sender quality out is 0.
    {
        // VFALCO Where is the line being deleted?
        // Clear the reserve of the sender, possibly delete the line!
        wrappedAcct.adjustOwnerCount(-1, j);

        // Clear reserve flag.
        state->setFieldU32(sfFlags, flags & (!bSenderHigh ? ~lsfLowReserve : ~lsfHighReserve));

        // Balance is zero, receiver reserve is clear.
        if (!after  // Balance is zero.
            && !(flags & (bSenderHigh ? lsfLowReserve : lsfHighReserve)))
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
    XRPL_ASSERT(issue == amount.issue(), "xrpl::issueIOU : matching issue");

    // Can't send to self!
    XRPL_ASSERT(issue.account != account, "xrpl::issueIOU : not issuer account");

    JLOG(j.trace()) << "issueIOU: " << to_string(account) << ": " << amount.getFullText();

    bool bSenderHigh = issue.account > account;

    auto const index = keylet::line(issue.account, account, issue.currency);

    if (auto state = view.peek(index))
    {
        STAmount final_balance = state->getFieldAmount(sfBalance);

        if (bSenderHigh)
            final_balance.negate();  // Put balance in sender terms.

        STAmount const start_balance = final_balance;

        final_balance -= amount;

        auto const must_delete = updateTrustLine(
            view, state, bSenderHigh, issue.account, start_balance, final_balance, j);

        view.creditHook(issue.account, account, amount, start_balance);

        if (bSenderHigh)
            final_balance.negate();

        // Adjust the balance on the trust line if necessary. We do this even
        // if we are going to delete the line to reflect the correct balance
        // at the time of deletion.
        state->setFieldAmount(sfBalance, final_balance);
        if (must_delete)
        {
            return WritableRippleState::trustDelete(
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
    STAmount final_balance = amount;

    final_balance.setIssuer(noAccount());

    WritableAccountRoot receiverAccount(account, view);
    if (!receiverAccount)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    bool noRipple = (receiverAccount->getFlags() & lsfDefaultRipple) == 0;

    return WritableRippleState::trustCreate(
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
        final_balance,
        limit,
        0,
        0,
        j);
}

TER
redeemIOU(
    ApplyView& applyView,
    AccountID const& account,
    STAmount const& amount,
    Issue const& issue,
    beast::Journal j)
{
    XRPL_ASSERT(
        !isXRP(account) && !isXRP(issue.account),
        "xrpl::redeemIOU : neither account nor issuer is XRP");

    // Consistency check
    XRPL_ASSERT(issue == amount.issue(), "xrpl::redeemIOU : matching issue");

    // Can't send to self!
    XRPL_ASSERT(issue.account != account, "xrpl::redeemIOU : not issuer account");

    JLOG(j.trace()) << "redeemIOU: " << to_string(account) << ": " << amount.getFullText();

    bool bSenderHigh = account > issue.account;

    if (auto state = applyView.peek(keylet::line(account, issue.account, issue.currency)))
    {
        STAmount final_balance = state->getFieldAmount(sfBalance);

        if (bSenderHigh)
            final_balance.negate();  // Put balance in sender terms.

        STAmount const start_balance = final_balance;

        final_balance -= amount;

        auto const must_delete = updateTrustLine(
            applyView, state, bSenderHigh, account, start_balance, final_balance, j);

        applyView.creditHook(account, issue.account, amount, start_balance);

        if (bSenderHigh)
            final_balance.negate();

        // Adjust the balance on the trust line if necessary. We do this even
        // if we are going to delete the line to reflect the correct balance
        // at the time of deletion.
        state->setFieldAmount(sfBalance, final_balance);

        if (must_delete)
        {
            return WritableRippleState::trustDelete(
                applyView,
                state,
                bSenderHigh ? issue.account : account,
                bSenderHigh ? account : issue.account,
                j);
        }

        applyView.update(state);
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
IOUToken::requireAuth(AccountID const& account, AuthType authType, int depth) const
{
    // NOTE: depth is ignored here because it's only relevant for MPTs
    if (isXRP(issue_) || issuer_ == account)
        return tesSUCCESS;

    auto const trustLine = readView_.read(keylet::line(account, issuer_, issue_.currency));
    // If account has no line, and this is a strong check, fail
    if (!trustLine && authType == AuthType::StrongAuth)
        return tecNO_LINE;

    // If this is a weak or legacy check, or if the account has a line, fail if
    // auth is required and not set on the line
    if (issuerAccount_.exists() && (*issuerAccount_)[sfFlags] & lsfRequireAuth)
    {
        if (trustLine)
        {
            return ((*trustLine)[sfFlags] & ((account > issuer_) ? lsfLowAuth : lsfHighAuth))
                ? tesSUCCESS
                : TER{tecNO_AUTH};
        }
        return TER{tecNO_LINE};
    }

    return tesSUCCESS;
}

TER
IOUToken::canTransfer(AccountID const& from, AccountID const& to) const
{
    if (issue_.native())
        return tesSUCCESS;

    if (issuer_ == from || issuer_ == to)
        return tesSUCCESS;
    if (!issuerAccount_.exists())
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const isRippleDisabled = [&](AccountID account) -> bool {
        // Line might not exist, but some transfers can create it. If this
        // is the case, just check the default ripple on the issuer account.
        auto const line = readView_.read(keylet::line(account, issue_));
        if (line)
        {
            bool const issuerHigh = issuer_ > account;
            return line->isFlag(issuerHigh ? lsfHighNoRipple : lsfLowNoRipple);
        }
        return issuerAccount_->isFlag(lsfDefaultRipple) == false;
    };

    // Fail if rippling disabled on both trust lines
    if (isRippleDisabled(from) && isRippleDisabled(to))
        return terNO_RIPPLE;

    return tesSUCCESS;
}

//------------------------------------------------------------------------------
//
// Token capability checks (IOU-specific)
//
//------------------------------------------------------------------------------

bool
IOUToken::canClawback() const
{
    if (!issuerAccount_.exists())
        return false;
    return issuerAccount_->isFlag(lsfAllowTrustLineClawback) &&
        !issuerAccount_->isFlag(lsfNoFreeze);
}

bool
IOUToken::requiresAuth() const
{
    if (!issuerAccount_.exists())
        return false;
    return issuerAccount_->isFlag(lsfRequireAuth);
}

//------------------------------------------------------------------------------
//
// Empty holding operations (IOU-specific)
//
//------------------------------------------------------------------------------

TER
WritableIOUToken::addEmptyHolding(
    AccountID const& accountID,
    XRPAmount priorBalance,
    beast::Journal journal)
{
    // Every account can hold XRP. An issuer can issue directly.
    if (issue_.native() || accountID == issuer_)
        return tesSUCCESS;

    if (issuerAccount_.isGlobalFrozen())
        return tecFROZEN;  // LCOV_EXCL_LINE

    auto const& srcId = issuer_;
    auto const& dstId = accountID;
    auto const high = srcId > dstId;
    auto const index = keylet::line(srcId, dstId, currency_);
    WritableAccountRoot wrappedSrc(srcId, applyView_);
    WritableAccountRoot wrappedDst(dstId, applyView_);
    if (!wrappedDst || !wrappedSrc)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    if (!wrappedSrc->isFlag(lsfDefaultRipple))
        return tecINTERNAL;  // LCOV_EXCL_LINE
    // If the line already exists, don't create it again.
    if (applyView_.read(index))
        return tecDUPLICATE;

    // Can the account cover the trust line reserve ?
    std::uint32_t const ownerCount = wrappedDst->at(sfOwnerCount);
    if (priorBalance < readView_.fees().accountReserve(ownerCount + 1))
        return tecNO_LINE_INSUF_RESERVE;

    return WritableRippleState::trustCreate(
        applyView_,
        high,
        srcId,
        dstId,
        index.key,
        wrappedDst,
        /*bAuth=*/false,
        /*bNoRipple=*/true,
        /*bFreeze=*/false,
        /*deepFreeze*/ false,
        /*saBalance=*/STAmount{Issue{currency_, noAccount()}},
        /*saLimit=*/STAmount{Issue{currency_, dstId}},
        /*uQualityIn=*/0,
        /*uQualityOut=*/0,
        journal);
}

TER
WritableIOUToken::removeEmptyHolding(AccountID const& accountID, beast::Journal journal)
{
    if (issue_.native())
    {
        auto const account = AccountRoot(accountID, applyView_);
        if (!account.exists())
            return tecINTERNAL;  // LCOV_EXCL_LINE

        auto const balance = account->getFieldAmount(sfBalance);
        if (balance.xrp() != 0)
            return tecHAS_OBLIGATIONS;

        return tesSUCCESS;
    }

    // `asset` is an IOU.
    // If the account is the issuer, then no line should exist. Check anyway.
    // If a line does exist, it will get deleted. If not, return success.
    bool const accountIsIssuer = accountID == issue_.account;
    auto const line = applyView_.peek(keylet::line(accountID, issue_));
    if (!line)
        return accountIsIssuer ? (TER)tesSUCCESS : (TER)tecOBJECT_NOT_FOUND;
    if (!accountIsIssuer && line->at(sfBalance)->iou() != beast::zero)
        return tecHAS_OBLIGATIONS;

    // Adjust the owner count(s)
    if (line->isFlag(lsfLowReserve))
    {
        // Clear reserve for low account.
        WritableAccountRoot wrappedLow(line->at(sfLowLimit)->getIssuer(), applyView_);
        if (!wrappedLow)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        wrappedLow.adjustOwnerCount(-1, journal);
        // It's not really necessary to clear the reserve flag, since the line
        // is about to be deleted, but this will make the metadata reflect an
        // accurate state at the time of deletion.
        line->clearFlag(lsfLowReserve);
    }

    if (line->isFlag(lsfHighReserve))
    {
        // Clear reserve for high account.
        WritableAccountRoot wrappedHigh(line->at(sfHighLimit)->getIssuer(), applyView_);
        if (!wrappedHigh)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        wrappedHigh.adjustOwnerCount(-1, journal);
        // It's not really necessary to clear the reserve flag, since the line
        // is about to be deleted, but this will make the metadata reflect an
        // accurate state at the time of deletion.
        line->clearFlag(lsfHighReserve);
    }

    return WritableRippleState::trustDelete(
        applyView_,
        line,
        line->at(sfLowLimit)->getIssuer(),
        line->at(sfHighLimit)->getIssuer(),
        journal);
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
    WritableAccountRoot wrappedLow(low, view);
    WritableAccountRoot wrappedHigh(high, view);
    if (!wrappedLow || !wrappedHigh)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    bool const ammLow = wrappedLow->isFieldPresent(sfAMMID);
    bool const ammHigh = wrappedHigh->isFieldPresent(sfAMMID);

    // can't both be AMM
    if (ammLow && ammHigh)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // at least one must be
    if (!ammLow && !ammHigh)
        return terNO_AMM;

    // one must be the target amm
    if (ammAccountID && (low != *ammAccountID && high != *ammAccountID))
        return terNO_AMM;

    if (auto const ter = WritableRippleState::trustDelete(view, sleState, low, high, j);
        !isTesSuccess(ter))
    {
        JLOG(j.error()) << "deleteAMMTrustLine: failed to delete the trustline.";
        return ter;
    }

    auto const uFlags = !ammLow ? lsfLowReserve : lsfHighReserve;
    if (!(sleState->getFlags() & uFlags))
        return tecINTERNAL;  // LCOV_EXCL_LINE

    WritableAccountRoot wrappedHolder = !ammLow ? wrappedLow : wrappedHigh;
    wrappedHolder.adjustOwnerCount(-1, j);

    return tesSUCCESS;
}

std::unique_ptr<TokenHolderBase>
IOUToken::getHolder(AccountID const& holder) const
{
    return std::make_unique<RippleState>(*this, holder);
}

}  // namespace xrpl
