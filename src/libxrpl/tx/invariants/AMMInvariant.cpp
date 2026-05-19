#include <xrpl/tx/invariants/AMMInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AMMHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>

#include <memory>
#include <string>

namespace xrpl {

void
ValidAMM::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (isDelete)
        return;

    if (after)
    {
        auto const type = after->getType();
        // AMM object changed
        if (type == ltAMM)
        {
            ammAccount_ = after->getAccountID(sfAccount);
            lptAMMBalanceAfter_ = after->getFieldAmount(sfLPTokenBalance);
        }
        // AMM pool changed
        else if (
            (type == ltRIPPLE_STATE && after->isFlag(lsfAMMNode)) ||
            (type == ltACCOUNT_ROOT && after->isFieldPresent(sfAMMID)))
        {
            ammPoolChanged_ = true;
        }
    }

    if (before)
    {
        // AMM object changed
        if (before->getType() == ltAMM)
        {
            lptAMMBalanceBefore_ = before->getFieldAmount(sfLPTokenBalance);
        }
    }
}

static bool
validBalances(
    STAmount const& amount,
    STAmount const& amount2,
    STAmount const& lptAMMBalance,
    ValidAMM::ZeroAllowed zeroAllowed)
{
    bool const positive =
        amount > beast::kZero && amount2 > beast::kZero && lptAMMBalance > beast::kZero;
    if (zeroAllowed == ValidAMM::ZeroAllowed::Yes)
    {
        return positive ||
            (amount == beast::kZero && amount2 == beast::kZero && lptAMMBalance == beast::kZero);
    }
    return positive;
}

bool
ValidAMM::finalizeVote(bool enforce, beast::Journal const& j) const
{
    if (lptAMMBalanceAfter_ != lptAMMBalanceBefore_ || ammPoolChanged_)
    {
        // LPTokens and the pool can not change on vote
        // LCOV_EXCL_START
        JLOG(j.error()) << "AMMVote invariant failed: " << lptAMMBalanceBefore_.value_or(STAmount{})
                        << " " << lptAMMBalanceAfter_.value_or(STAmount{}) << " "
                        << ammPoolChanged_;
        if (enforce)
            return false;
        // LCOV_EXCL_STOP
    }

    return true;
}

bool
ValidAMM::finalizeBid(bool enforce, beast::Journal const& j) const
{
    if (ammPoolChanged_)
    {
        // The pool can not change on bid
        // LCOV_EXCL_START
        JLOG(j.error()) << "AMMBid invariant failed: pool changed";
        if (enforce)
            return false;
        // LCOV_EXCL_STOP
    }
    // LPTokens are burnt, therefore there should be fewer LPTokens
    else if (
        lptAMMBalanceBefore_ && lptAMMBalanceAfter_ &&
        (*lptAMMBalanceAfter_ > *lptAMMBalanceBefore_ || *lptAMMBalanceAfter_ <= beast::kZero))
    {
        // LCOV_EXCL_START
        JLOG(j.error()) << "AMMBid invariant failed: " << *lptAMMBalanceBefore_ << " "
                        << *lptAMMBalanceAfter_;
        if (enforce)
            return false;
        // LCOV_EXCL_STOP
    }

    return true;
}

bool
ValidAMM::finalizeCreate(
    STTx const& tx,
    ReadView const& view,
    bool enforce,
    beast::Journal const& j) const
{
    if (!ammAccount_)
    {
        // LCOV_EXCL_START
        JLOG(j.error()) << "AMMCreate invariant failed: AMM object is not created";
        if (enforce)
            return false;
        // LCOV_EXCL_STOP
    }
    else
    {
        auto const [amount, amount2] = ammPoolHolds(
            view,
            *ammAccount_,
            tx[sfAmount].asset(),
            tx[sfAmount2].asset(),
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            j);
        // Create invariant:
        // sqrt(amount * amount2) == LPTokens
        // all balances are greater than zero
        // NOLINTBEGIN(bugprone-unchecked-optional-access) lptAMMBalanceAfter_ set with ammAccount_
        // in visitEntry
        if (!validBalances(amount, amount2, *lptAMMBalanceAfter_, ZeroAllowed::No) ||
            ammLPTokens(amount, amount2, lptAMMBalanceAfter_->get<Issue>()) != *lptAMMBalanceAfter_)
        {
            JLOG(j.error()) << "AMMCreate invariant failed: " << amount << " " << amount2 << " "
                            << *lptAMMBalanceAfter_;
            if (enforce)
                return false;
        }
        // NOLINTEND(bugprone-unchecked-optional-access)
    }

    return true;
}

bool
ValidAMM::finalizeDelete(bool enforce, TER res, beast::Journal const& j) const
{
    if (ammAccount_)
    {
        // LCOV_EXCL_START
        std::string const msg = (isTesSuccess(res)) ? "AMM object is not deleted on tesSUCCESS"
                                                    : "AMM object is changed on tecINCOMPLETE";
        JLOG(j.error()) << "AMMDelete invariant failed: " << msg;
        if (enforce)
            return false;
        // LCOV_EXCL_STOP
    }

    return true;
}

bool
ValidAMM::finalizeDEX(bool enforce, beast::Journal const& j) const
{
    if (ammAccount_)
    {
        // LCOV_EXCL_START
        JLOG(j.error()) << "AMM swap invariant failed: AMM object changed";
        if (enforce)
            return false;
        // LCOV_EXCL_STOP
    }

    return true;
}

bool
ValidAMM::generalInvariant(
    xrpl::STTx const& tx,
    xrpl::ReadView const& view,
    ZeroAllowed zeroAllowed,
    beast::Journal const& j) const
{
    // NOLINTBEGIN(bugprone-unchecked-optional-access) ammAccount_ and lptAMMBalanceAfter_ set
    // together in visitEntry; callers only invoke this inside else-of-if(!ammAccount_)
    auto const [amount, amount2] = ammPoolHolds(
        view,
        *ammAccount_,
        tx[sfAsset],
        tx[sfAsset2],
        FreezeHandling::IgnoreFreeze,
        AuthHandling::IgnoreAuth,
        j);
    // Deposit and Withdrawal invariant:
    // sqrt(amount * amount2) >= LPTokens
    // all balances are greater than zero
    // unless on last withdrawal
    auto const poolProductMean = root2(amount * amount2);
    bool const nonNegativeBalances =
        validBalances(amount, amount2, *lptAMMBalanceAfter_, zeroAllowed);
    bool const strongInvariantCheck = poolProductMean >= *lptAMMBalanceAfter_;
    // Allow for a small relative error if strongInvariantCheck fails
    auto weakInvariantCheck = [&]() {
        return *lptAMMBalanceAfter_ != beast::kZero &&
            withinRelativeDistance(poolProductMean, Number{*lptAMMBalanceAfter_}, Number{1, -11});
    };
    if (!nonNegativeBalances || (!strongInvariantCheck && !weakInvariantCheck()))
    {
        JLOG(j.error()) << "AMM " << tx.getTxnType()
                        << " invariant failed: " << tx.getHash(HashPrefix::TransactionId) << " "
                        << ammPoolChanged_ << " " << amount << " " << amount2 << " "
                        << poolProductMean << " " << lptAMMBalanceAfter_->getText() << " "
                        << ((*lptAMMBalanceAfter_ == beast::kZero)
                                ? Number{1}
                                : ((*lptAMMBalanceAfter_ - poolProductMean) / poolProductMean));
        return false;
    }
    // NOLINTEND(bugprone-unchecked-optional-access)

    return true;
}

bool
ValidAMM::finalizeDeposit(
    xrpl::STTx const& tx,
    xrpl::ReadView const& view,
    bool enforce,
    beast::Journal const& j) const
{
    if (!ammAccount_)
    {
        // LCOV_EXCL_START
        JLOG(j.error()) << "AMMDeposit invariant failed: AMM object is deleted";
        if (enforce)
            return false;
        // LCOV_EXCL_STOP
    }
    else if (!generalInvariant(tx, view, ZeroAllowed::No, j) && enforce)
    {
        return false;
    }

    return true;
}

bool
ValidAMM::finalizeWithdraw(
    xrpl::STTx const& tx,
    xrpl::ReadView const& view,
    bool enforce,
    beast::Journal const& j) const
{
    if (!ammAccount_)
    {
        // Last Withdraw or Clawback deleted AMM
    }
    else if (!generalInvariant(tx, view, ZeroAllowed::Yes, j))
    {
        if (enforce)
            return false;
    }

    return true;
}

bool
ValidAMM::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    // Delete may return tecINCOMPLETE if there are too many
    // trustlines to delete.
    if (!isTesSuccess(result) && result != tecINCOMPLETE)
        return true;

    bool const enforce = view.rules().enabled(fixAMMv1_3);

    switch (tx.getTxnType())
    {
        case ttAMM_CREATE:
            return finalizeCreate(tx, view, enforce, j);
        case ttAMM_DEPOSIT:
            return finalizeDeposit(tx, view, enforce, j);
        case ttAMM_CLAWBACK:
        case ttAMM_WITHDRAW:
            return finalizeWithdraw(tx, view, enforce, j);
        case ttAMM_BID:
            return finalizeBid(enforce, j);
        case ttAMM_VOTE:
            return finalizeVote(enforce, j);
        case ttAMM_DELETE:
            return finalizeDelete(enforce, result, j);
        case ttCHECK_CASH:
        case ttOFFER_CREATE:
        case ttPAYMENT:
            return finalizeDEX(enforce, j);
        default:
            break;
    }

    return true;
}

}  // namespace xrpl
