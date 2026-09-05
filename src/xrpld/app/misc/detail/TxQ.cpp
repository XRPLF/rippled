#include <xrpld/app/misc/TxQ.h>

#include <xrpld/app/ledger/OpenLedger.h>
#include <xrpld/app/main/Application.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/mulDiv.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ApplyViewImpl.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/RippleLedgerHash.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/apply.h>
#include <xrpl/tx/applySteps.h>

#include <boost/function/function_base.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace xrpl {

//////////////////////////////////////////////////////////////////////////

static FeeLevel64
getFeeLevelPaid(ReadView const& view, STTx const& tx)
{
    auto const [baseFee, effectiveFeePaid] = [&view, &tx]() {
        XRPAmount const baseFee = calculateBaseFee(view, tx);
        XRPAmount const feePaid = tx[sfFee].xrp();

        // If baseFee is 0 then the cost of a basic transaction is free, but we
        // need the effective fee level to be non-zero.
        XRPAmount const mod = [&view, &tx, baseFee]() {
            if (baseFee.signum() > 0)
                return XRPAmount{0};
            auto def = calculateDefaultBaseFee(view, tx);
            return def.signum() == 0 ? XRPAmount{1} : def;
        }();
        return std::pair{baseFee + mod, feePaid + mod};
    }();

    XRPL_ASSERT(baseFee.signum() > 0, "xrpl::getFeeLevelPaid : positive fee");
    if (effectiveFeePaid.signum() <= 0 || baseFee.signum() <= 0)
    {
        return FeeLevel64(0);
    }

    return mulDiv(effectiveFeePaid, TxQ::kBaseLevel, baseFee)
        .value_or(FeeLevel64(std::numeric_limits<std::uint64_t>::max()));
}

static std::optional<LedgerIndex>
getLastLedgerSequence(STTx const& tx)
{
    if (!tx.isFieldPresent(sfLastLedgerSequence))
        return std::nullopt;
    return tx.getFieldU32(sfLastLedgerSequence);
}

static FeeLevel64
increase(FeeLevel64 level, std::uint32_t increasePercent)
{
    return mulDiv(level, 100 + increasePercent, 100)
        .value_or(static_cast<FeeLevel64>(xrpl::kMuldivMax));
}

//////////////////////////////////////////////////////////////////////////

struct MultiTxn
{
    ApplyViewImpl applyView;
    OpenView openView;

    MultiTxn(OpenView& view, ApplyFlags flags) : applyView(&view, flags), openView(&applyView)
    {
    }
};

class TxQApplyImpl
{
    using TxMapCIter = TxQ::TxQAccount::TxMap::const_iterator;
    using TxMapIter = TxQ::TxQAccount::TxMap::iterator;
    using TxRange = std::pair<TxMapIter, TxMapIter>;
    using AccMapIter = TxQ::AccountMap::iterator;

    TxQ& txq_;
    std::optional<std::reference_wrapper<std::scoped_lock<std::mutex> const>> txqLock_;

    Application& app_;
    OpenView& view_;
    ApplyFlags flags_;
    beast::Journal j_;
    std::shared_ptr<STTx const> const& tx_;

    AccountID const accID_;
    Keylet const accKey_;
    SLE::const_pointer accSle_;
    SeqProxy accSeq_ = SeqProxy(SeqProxy::Type::Seq, 0);

    uint256 const txID_;
    SeqProxy const txSeq_;
    PreflightResult const preflightRes_;

    std::optional<TxQ::FeeMetrics::Snapshot> metricsSnapshot_;
    FeeLevel64 feeLevelPaid_{};
    FeeLevel64 requiredFeeLevel_{};

    // A set of queued transactions for this account that will be applied before the current
    // transaction so we can make correct balance and expense calculations
    std::optional<TxRange> prevTxs_;
    // size of prevTxs_ range
    std::uint32_t accTxCount_ = 0;

    // Current account present in TxQ byAccount_ map.
    // Can be used only under txq_.mutex_ lock
    AccMapIter byAccountIter_;

    // If current tx can replace tx that already in TxQ.
    std::optional<TxMapCIter> txToReplaceIter_;

    // view for updating balance in case of several queued txs for the account
    std::optional<MultiTxn> multiTxn_;

    // Parent processor in case of Batch
    std::optional<std::reference_wrapper<TxQApplyImpl const>> parent_;

    ///////////////////////////////////////////////////////////////////////////////

public:
    TxQApplyImpl(
        TxQ& txq,
        Application& app,
        OpenView& view,
        std::shared_ptr<STTx const> const& tx,
        ApplyFlags flags,
        beast::Journal j)
        : txq_(txq)
        , app_(app)
        , view_(view)
        , flags_(flags)
        , j_(j)
        , tx_(tx)
        , accID_(tx_->at(sfAccount))
        , accKey_(keylet::account(accID_))
        , txID_(tx_->getTransactionID())
        , txSeq_(tx_->getSeqProxy())
        , preflightRes_(preflight(app_, view_.rules(), *tx_, flags_, j_))
    {
    }

    TxQApplyImpl(
        TxQ& txq,
        Application& app,
        OpenView& view,
        std::reference_wrapper<TxQApplyImpl const> const& parent,
        std::shared_ptr<STTx const> const& tx,
        PreflightResult const& pfRes,
        ApplyFlags flags,
        beast::Journal j)
        : txq_(txq)
        , app_(app)
        , view_(view)
        , flags_(flags)
        , j_(j)
        , tx_(tx)
        , accID_(tx_->at(sfAccount))
        , accKey_(keylet::account(accID_))
        , txID_(tx_->getTransactionID())
        , txSeq_(tx_->getSeqProxy())
        , preflightRes_(pfRes)
        , metricsSnapshot_(txq_.feeMetrics_.getSnapshot())
        , parent_(parent)
    {
    }

public:
    ApplyResult
    apply();

private:
    std::optional<ApplyResult>
    applyImplPrelock();

    ApplyResult
    applyImpl();

    // Retrieve prevTxs_.
    void
    retrievePrevTxs();

    std::optional<ApplyResult>
    checkTicketInLedger() const;

    std::optional<ApplyResult>
    checkIsTxBlocker() const;

    std::optional<ApplyResult>
    retrieveTxToReplace();

    std::optional<ApplyResult>
    checkFeeLevel() const;

    std::optional<ApplyResult>
    checkBlockerInAccQue() const;

    std::expected<bool, ApplyResult>
    checkMultiTxn() const;

    std::optional<ApplyResult>
    processAccountTxs();

    std::optional<ApplyResult>
    checkFrontTx(TxMapCIter const& prevIter, TxMapCIter const& endIter) const;

    // Returns potential spend and fee of the TX
    std::pair<XRPAmount, XRPAmount>
    calcSpendAndFee() const;

    std::optional<ApplyResult>
    tryClearAccountQueueUpThruTx();

    std::optional<ApplyResult>
    canBeHeld() const;

    std::optional<ApplyResult>
    processQueueIsFull();

    void
    addTxToQueue();

    std::optional<ApplyResult>
    processInnerBatch(std::vector<TxQApplyImpl>& vec);

    std::expected<std::vector<TxQApplyImpl>, ApplyResult>
    processInnerBatchPrelock();

    void
    removeBatchTx();

public:
    static void
    removeInnerTxs(TxQ::AccountMap& map, STTx const& tx);

    // Check whether a queued candidate is at the front (lowest SeqProxy)
    // of its account's transactions. For a regular transaction this checks
    // only the candidate itself. For a Batch, the Batch and every inner
    // transaction must be runnable: an inner tx is runnable if it is first
    // in its account or is preceded only by other inner txs of the same
    // Batch. If any member is blocked by a foreign tx, this returns false.
    static bool
    isFirstInAccount(TxQ::AccountMap const& map, TxQ::MaybeTx const& candidate);
};

std::optional<ApplyResult>
TxQApplyImpl::checkTicketInLedger() const
{
    if (txSeq_.isTicket() && !view_.exists(keylet::ticket(accID_, txSeq_)))
    {
        if (txSeq_.value() < accSeq_.value())
        {
            // The ticket number is low enough that it should already be
            // in the ledger if it were ever going to exist.
            return ApplyResult{tefNO_TICKET, false};
        }

        // We don't queue transactions that use Tickets unless
        // we can find the Ticket in the ledger.
        return ApplyResult{terPRE_TICKET, false};
    }

    return {};
}

void
TxQApplyImpl::retrievePrevTxs()
{
    if (byAccountIter_ == txq_.byAccount_.end())
        return;

    // Find the first transaction in the queue that we might apply.
    TxQ::TxQAccount::TxMap& acctTxs = byAccountIter_->second.transactions;
    auto const firstIter = acctTxs.lower_bound(accSeq_);
    if (firstIter == acctTxs.end())
    {
        // Even though there may be transactions in the queue, there are none that we should pay
        // attention to.
        return;
    }

    prevTxs_ = {{firstIter, acctTxs.end()}};
    accTxCount_ = std::distance(prevTxs_->first, prevTxs_->second);
}

std::optional<ApplyResult>
TxQApplyImpl::checkIsTxBlocker() const
{
    if (preflightRes_.consequences.isBlocker())
    {
        if (accTxCount_ > 1)
        {
            // A blocker may not be co-resident with other transactions in
            // the account's queue.
            JLOG(j_.trace()) << "Rejecting blocker transaction " << txID_
                             << ".  Account has other queued transactions.";
            return ApplyResult{telCAN_NOT_QUEUE_BLOCKS, false};
        }

        // NOLINTNEXTLINE(bugprone-unchecked-optional-access) acctTxCount == 1 implies txIter is set
        if (accTxCount_ == 1 && (txSeq_ != prevTxs_->first->first))
        {
            // The blocker is not replacing the lone queued transaction.
            JLOG(j_.trace()) << "Rejecting blocker transaction " << txID_
                             << ".  Blocker does not replace lone queued transaction.";
            return ApplyResult{telCAN_NOT_QUEUE_BLOCKS, false};
        }
    }

    return {};
}

std::optional<ApplyResult>
TxQApplyImpl::retrieveTxToReplace()
{
    if (byAccountIter_ == txq_.byAccount_.end())
        return {};

    auto const& txQAcct = byAccountIter_->second.transactions;
    if (auto const existingIter = txQAcct.find(txSeq_); existingIter != txQAcct.end())
    {
        // can't replace inner tx
        // can't be replaced by batch, or by inner tx
        if (parent_ || tx_->getTxnType() == ttBATCH || existingIter->second.parentTx)
            return ApplyResult{telCAN_NOT_QUEUE, false};

        txToReplaceIter_ = existingIter;
    }
    return {};
}

std::optional<ApplyResult>
TxQApplyImpl::checkFeeLevel() const
{
    if (!txToReplaceIter_)
        return {};

    [[maybe_unused]] auto const& [replacedSeq, replacedTx] = **txToReplaceIter_;

    // We are attempting to replace a transaction in the queue.
    //
    // Is the current transaction's fee higher than
    // the queued transaction's fee + a percentage
    auto requiredRetryLevel = increase(replacedTx.feeLevel, txq_.setup_.retrySequencePercent);
    JLOG(j_.trace()) << "Found transaction in queue for account " << accID_ << " with " << txSeq_
                     << " new txn fee level is " << feeLevelPaid_ << ", old txn fee level is "
                     << replacedTx.feeLevel << ", new txn needs fee level of "
                     << requiredRetryLevel;
    if (feeLevelPaid_ > requiredRetryLevel)
    {
        // Continue, leaving the queued transaction marked for removal.
        // DO NOT REMOVE if the new tx fails, because there may
        // be other txs dependent on it in the queue.
        JLOG(j_.trace()) << "Removing transaction from queue " << replacedTx.txID << " in favor of "
                         << txID_;
    }
    else
    {
        // Drop the current transaction
        JLOG(j_.trace()) << "Ignoring transaction " << txID_ << " in favor of queued "
                         << replacedTx.txID;
        return ApplyResult{telCAN_NOT_QUEUE_FEE, false};
    }

    return {};
}

std::optional<ApplyResult>
TxQApplyImpl::checkBlockerInAccQue() const
{
    if (accTxCount_ == 0)
        return {};

    // Allow tx to replace a blocker.  Otherwise, if there's a
    // blocker, we can't queue tx.
    //
    // We only need to check if txIter->first is a blocker because we
    // require that a blocker be alone in the account's queue.

    // NOLINTBEGIN(bugprone-unchecked-optional-access) acctTxCount == 1 implies txIter is set
    auto const& seq = prevTxs_->first->first;
    auto const& tx = prevTxs_->first->second;
    if (accTxCount_ == 1 && tx.consequences().isBlocker() && (seq != txSeq_))
        return ApplyResult{telCAN_NOT_QUEUE_BLOCKED, false};
    // NOLINTEND(bugprone-unchecked-optional-access)

    // Is there a transaction for the same account with the same
    // SeqProxy already in the queue?  If so we may replace the
    // existing entry with this new transaction.
    if (auto const err = checkFeeLevel(); err.has_value())
        return err;

    return {};
}

std::expected<bool, ApplyResult>
TxQApplyImpl::checkMultiTxn() const
{
    // Determine if we need a multiTxn object.  Assuming the account
    // is in the queue, there are two situations where we need to
    // build multiTx:
    //  1. If there are two or more transactions in the account's queue, or
    //  2. If the account has a single queue entry, we may still need
    //     multiTxn, but only if that lone entry will not be replaced by tx.
    bool requiresMultiTxn = false;
    if (accTxCount_ > 1 || !txToReplaceIter_)
    {
        // If the transaction is queueable, create the multiTxn
        // object to hold the info we need to adjust for prior txns.

        // NOLINTBEGIN(bugprone-unchecked-optional-access)
        TER const ter{txq_.canBeHeld(
            *tx_, flags_, view_, accSle_, byAccountIter_, txToReplaceIter_, *txqLock_)};
        if (!isTesSuccess(ter))
            return std::unexpected(ApplyResult{ter, false});
        // NOLINTEND(bugprone-unchecked-optional-access)

        requiresMultiTxn = true;
    }

    return requiresMultiTxn;
}

std::optional<ApplyResult>
TxQApplyImpl::checkFrontTx(TxMapCIter const& prevIter, TxMapCIter const& endIter) const
{
    // Does the new transaction go to the front of the queue?
    // This can happen if:
    //  o A transaction in the queue with a Sequence expired, or
    //  o The current first thing in the queue has a Ticket and
    //    * The tx has a Ticket that precedes it or
    //    * txSeq == acctSeq.
    // implies txIter is set

    // NOLINTBEGIN(bugprone-unchecked-optional-access)
    XRPL_ASSERT(prevIter != endIter, "xrpl::TxQ::apply : not end");
    if (prevIter == endIter || txSeq_ < prevIter->first)
    {
        // The first Sequence number in the queue must be the
        // account's sequence.
        if (txSeq_.isSeq())
        {
            if (txSeq_ < accSeq_)
                return ApplyResult{tefPAST_SEQ, false};
            if (txSeq_ > accSeq_)
                return ApplyResult{terPRE_SEQ, false};
        }
    }
    else if (!txToReplaceIter_)
    {
        // The current transaction is not replacing a transaction
        // in the queue.  So apparently there's a transaction in
        // front of this one in the queue.  Make sure the current
        // transaction fits in proper sequence order with the
        // previous transaction or is a ticket.
        if (txSeq_.isSeq() && txq_.nextQueuableSeqImpl(accSle_, *txqLock_) != txSeq_)
            return ApplyResult{telCAN_NOT_QUEUE, false};
    }
    // NOLINTEND(bugprone-unchecked-optional-access)

    return {};
}

std::pair<XRPAmount, XRPAmount>
TxQApplyImpl::calcSpendAndFee() const
{
    // Sum fees and spending for all of the queued transactions
    // so we know how much to remove from the account balance
    // for the trial preclaim.
    XRPAmount potentialSpend = beast::kZero;
    XRPAmount totalFee = beast::kZero;

    if (!prevTxs_)
        return {potentialSpend, totalFee};

    for (auto iter = prevTxs_->first, end = prevTxs_->second; iter != end; ++iter)
    {
        [[maybe_unused]] auto const& [seq, tx] = *iter;

        // If we're replacing this transaction don't include
        // the replaced transaction's XRP spend.  Otherwise add
        // it to potentialSpend.
        if (seq != txSeq_)
        {
            totalFee += tx.consequences().fee();
            potentialSpend += tx.consequences().potentialSpend();
        }
        else if (std::next(iter) != end)
        {
            // The fee for the candidate transaction _should_ be
            // counted if it's replacing a transaction in the middle
            // of the queue.
            totalFee += preflightRes_.consequences.fee();
            potentialSpend += preflightRes_.consequences.potentialSpend();
        }
    }

    return {potentialSpend, totalFee};
}

std::optional<ApplyResult>
TxQApplyImpl::processAccountTxs()
{
    // There are probably other transactions in the queue for this
    // account.  Make sure the new transaction can work with the others
    // in the queue.
    if (accSeq_ > txSeq_)
        return ApplyResult{tefPAST_SEQ, false};

    {
        // Check if there are previous txs that will be applied
        auto const requiresMultiTxn = checkMultiTxn();
        if (!requiresMultiTxn)
            return requiresMultiTxn.error();
        if (!*requiresMultiTxn)
            return {};
    }

    // See if adding this entry to the queue makes sense.
    //
    //  o Transactions with sequences should start with the
    //    account's Sequence.
    //
    //  o Additional transactions with Sequences should
    //    follow preceding sequence-based transactions with no
    //    gaps (except for those required by TicketCreate
    //    transactions).

    // Find the entry in the queue that precedes the new
    // transaction, if one does.
    if (!prevTxs_)
        return {};  // LCOV_EXCL_LINE
    TxQ::TxQAccount const& txQAcct = byAccountIter_->second;
    if (auto const err = checkFrontTx(txQAcct.getPrevTx(txSeq_), prevTxs_->second); err.has_value())
        return err;

    // Sum fees and spending for all of the queued transactions
    // so we know how much to remove from the account balance
    // for the trial preclaim.
    auto const [potentialSpend, totalFee] = calcSpendAndFee();

    /* Check if the total fees in flight are greater
        than the account's current balance, or the
        minimum reserve. If it is, then there's a risk
        that the fees won't get paid, so drop this
        transaction with a telCAN_NOT_QUEUE_BALANCE result.
        Assume: Minimum account reserve is 20 XRP.
        Example 1: If I have 1,000,000 XRP, I can queue
            a transaction with a 1,000,000 XRP fee. In
            the meantime, some other transaction may
            lower my balance (eg. taking an offer). When
            the transaction executes, I will either
            spend the 1,000,000 XRP, or the transaction
            will get stuck in the queue with a
            `terINSUF_FEE_B`.
        Example 2: If I have 1,000,000 XRP, and I queue
            10 transactions with 0.1 XRP fee, I have 1 XRP
            in flight. I can now queue another tx with a
            999,999 XRP fee. When the first 10 execute,
            they're guaranteed to pay their fee, because
            nothing can eat into my reserve. The last
            transaction, again, will either spend the
            999,999 XRP, or get stuck in the queue.
        Example 3: If I have 1,000,000 XRP, and I queue
            7 transactions with 3 XRP fee, I have 21 XRP
            in flight. I can not queue any more transactions,
            no matter how small or large the fee.
        Transactions stuck in the queue are mitigated by
        LastLedgerSeq and MaybeTx::retriesRemaining.
    */

    auto const balance = (*accSle_)[sfBalance].xrp();
    /* Get the minimum possible account reserve. If it
       is at least 10 * the base fee, and fees exceed
       this amount, the transaction can't be queued.

             Currently typical fees are several orders
             of magnitude smaller than any current or expected
             future reserve. This calculation is simpler than
             trying to figure out the potential changes to
             the ownerCount that may occur to the account
             as a result of these transactions, and removes
             any need to account for other transactions that
             may affect the owner count while these are queued.

               However, in case the account reserve is on a
               comparable scale to the base fee, ignore the
               reserve. Only check the account balance.accountKey
            */
    auto const reserve = view_.fees().reserve;
    auto const base = view_.fees().base;
    if (totalFee >= balance || (reserve > 10 * base && totalFee >= reserve))
    {
        // Drop the current transaction
        JLOG(j_.trace()) << "Ignoring transaction " << txID_ << ". Total fees in flight too high.";
        return ApplyResult{telCAN_NOT_QUEUE_BALANCE, false};
    }

    // Create the test view from the current view.
    multiTxn_.emplace(view_, flags_);

    auto const sleBump = multiTxn_->applyView.peek(accKey_);
    if (!sleBump)
        return ApplyResult{tefINTERNAL, false};

    // Subtract the fees and XRP spend from all of the other
    // transactions in the queue.  That prevents a transaction
    // inserted in the middle from fouling up later transactions.
    auto const potentialTotalSpend =
        totalFee + std::min(balance - std::min(balance, reserve), potentialSpend);
    XRPL_ASSERT(
        potentialTotalSpend > XRPAmount{0} ||
            (potentialTotalSpend == XRPAmount{0} && multiTxn_->applyView.fees().base == 0),
        "xrpl::TxQ::apply : total spend check");
    sleBump->setFieldAmount(sfBalance, balance - potentialTotalSpend);
    // The transaction's sequence/ticket will be valid when the other
    // transactions in the queue have been processed. If the tx has a
    // sequence, set the account to match it. If it has a ticket, use
    // the next queueable sequence, which is the closest approximation
    // to the most successful case.

    // NOLINTBEGIN(bugprone-unchecked-optional-access)
    sleBump->at(sfSequence) =
        txSeq_.isSeq() ? txSeq_.value() : txq_.nextQueuableSeqImpl(accSle_, *txqLock_).value();
    // NOLINTEND(bugprone-unchecked-optional-access)

    return {};
}

std::optional<ApplyResult>
TxQApplyImpl::tryClearAccountQueueUpThruTx()
{
    /* Quick heuristic check to see if it's worth checking that this tx has
        a high enough fee to clear all the txs in front of it in the queue.
        1) Transaction is trying to get into the open ledger.
        2) Transaction must be Sequence-based.
        3) Must be an account already in the queue.
        4) Must be have passed the multiTxn checks (tx is not the next
            account seq, the skipped seqs are in the queue, the reserve
            doesn't get exhausted, etc).
        5) The next transaction must not have previously tried and failed
            to apply to an open ledger.
        6) Tx must be paying more than just the required fee level to
            get itself into the queue.
        7) Fee level must be escalated above the default (if it's not,
            then the first tx _must_ have failed to process in `accept`
            for some other reason. Tx is allowed to queue in case
            conditions change, but don't waste the effort to clear).
    */

    if (txSeq_.isSeq() && prevTxs_ && multiTxn_ &&
        prevTxs_->first->second.retriesRemaining == TxQ::MaybeTx::kRetriesAllowed &&
        feeLevelPaid_ > requiredFeeLevel_ && requiredFeeLevel_ > TxQ::kBaseLevel)
    {
        OpenView sandbox(kOpenLedger, &view_, view_.rules());

        // NOLINTBEGIN(bugprone-unchecked-optional-access)
        auto result = txq_.tryClearAccountQueueUpThruTx(
            app_,
            sandbox,
            *tx_,
            byAccountIter_,
            prevTxs_->first,
            feeLevelPaid_,
            preflightRes_,
            view_.txCount(),
            flags_,
            *metricsSnapshot_,
            j_);
        // NOLINTEND(bugprone-unchecked-optional-access)

        if (result.applied)
        {
            sandbox.apply(view_);
            /* Can't erase (*replacedTxIter) here because success
                implies that it has already been deleted.
            */
            return result;
        }
    }

    return {};
}

std::optional<ApplyResult>
TxQApplyImpl::canBeHeld() const
{
    // If `multiTxn` has a value, then `canBeHeld` has already been verified
    if (multiTxn_.has_value())
        return {};

    // NOLINTBEGIN(bugprone-unchecked-optional-access)
    TER const ter =
        txq_.canBeHeld(*tx_, flags_, view_, accSle_, byAccountIter_, txToReplaceIter_, *txqLock_);
    // NOLINTEND(bugprone-unchecked-optional-access)

    if (!isTesSuccess(ter))
    {
        // Bail, transaction cannot be held
        JLOG(j_.trace()) << "Transaction " << txID_ << " cannot be held";
        return ApplyResult{ter, false};
    }

    return {};
}

std::optional<ApplyResult>
TxQApplyImpl::processQueueIsFull()
{
    if (!txToReplaceIter_ && txq_.isFull())
    {
        auto lastRIter = txq_.byFee_.rbegin();
        while (lastRIter != txq_.byFee_.rend() && lastRIter->account == accID_)
        {
            ++lastRIter;
        }
        if (lastRIter == txq_.byFee_.rend())
        {
            // The only way this condition can happen is if the entire
            // queue is filled with transactions from this account. This
            // is impossible with default settings - minimum queue size
            // is 2000, and an account can only have 10 transactions
            // queued. However, it can occur if settings are changed,
            // and there is unit test coverage.
            JLOG(j_.info()) << "Queue is full, and transaction " << txID_
                            << " would kick a transaction from the same account (" << accID_
                            << ") out of the queue.";
            return ApplyResult{telCAN_NOT_QUEUE_FULL, false};
        }
        auto const& endAccount = txq_.byAccount_.at(lastRIter->account);
        auto endEffectiveFeeLevel = [&]() {
            // Compute the average of all the txs for the endAccount,
            // but only if the last tx in the queue has a lower fee
            // level than this candidate tx.
            if (lastRIter->feeLevel > feeLevelPaid_ || endAccount.transactions.size() == 1)
                return lastRIter->feeLevel;

            constexpr FeeLevel64 kMax{std::numeric_limits<std::uint64_t>::max()};
            auto endTotal = std::accumulate(
                endAccount.transactions.begin(),
                endAccount.transactions.end(),
                std::pair<FeeLevel64, FeeLevel64>(0, 0),
                [&](auto const& total, auto const& txn) -> std::pair<FeeLevel64, FeeLevel64> {
                    // Check for overflow.
                    auto next = txn.second.feeLevel / endAccount.transactions.size();
                    auto mod = txn.second.feeLevel % endAccount.transactions.size();
                    if (total.first >= kMax - next || total.second >= kMax - mod)
                        return {kMax, FeeLevel64{0}};

                    return {total.first + next, total.second + mod};
                });
            return endTotal.first + endTotal.second / endAccount.transactions.size();
        }();

        if (feeLevelPaid_ > endEffectiveFeeLevel)
        {
            // The queue is full, and this transaction is more
            // valuable, so kick out the cheapest transaction.
            auto dropRIter = endAccount.transactions.rbegin();
            XRPL_ASSERT(
                dropRIter->second.account == lastRIter->account,
                "xrpl::TxQ::apply : cheapest transaction found");
            JLOG(j_.info()) << "Removing last item of account " << lastRIter->account
                            << " from queue with average fee of " << endEffectiveFeeLevel
                            << " in favor of " << txID_ << " with fee of " << feeLevelPaid_;
            txq_.erase(txq_.byFee_.iterator_to(dropRIter->second));
        }
        else
        {
            JLOG(j_.info()) << "Queue is full, and transaction " << txID_
                            << " fee is lower than end item's account average fee";
            return ApplyResult{telCAN_NOT_QUEUE_FULL, false};
        }
    }

    return {};
}

void
TxQApplyImpl::addTxToQueue()
{
    bool created = false;
    if (byAccountIter_ == txq_.byAccount_.end())
    {
        // Create a new TxQAccount object and add the byAccount lookup.
        std::tie(byAccountIter_, created) = txq_.byAccount_.emplace(accID_, TxQ::TxQAccount(tx_));
        XRPL_ASSERT(created, "xrpl::TxQ::apply : account created");
    }
    // Modify the flags for use when coming out of the queue.
    // These changes _may_ cause an extra `preflight`, but as long as
    // the `HashRouter` still knows about the transaction, the signature
    // will not be checked again, so the cost should be minimal.

    // Don't allow soft failures, which can lead to retries
    flags_ &= ~TapRetry;

    auto const& parentTx = parent_ ? parent_->get().tx_ : std::shared_ptr<STTx const>();
    auto& candidate =
        byAccountIter_->second.add({tx_, txID_, feeLevelPaid_, flags_, preflightRes_, parentTx});

    // Then index it into the byFee lookup.
    if (!parent_)
        txq_.byFee_.insert(candidate);

    JLOG(j_.debug()) << "Added transaction " << candidate.txID << " with result "
                     << transToken(preflightRes_.ter) << " from " << (!created ? "existing" : "new")
                     << " account " << candidate.account << " to queue."
                     << " Flags: " << flags_;
}

std::optional<ApplyResult>
TxQApplyImpl::processInnerBatch(std::vector<TxQApplyImpl>& vec)
{
    XRPL_ASSERT(!parent_, "TxQApplyImpl::processInnerBatch double nesting");

    for (auto& innerImpl : vec)
    {
        innerImpl.txqLock_ = txqLock_;
        auto err = innerImpl.applyImpl();
        if (err.ter != terQUEUED)
        {
            err.ter = temINVALID_INNER_BATCH;
            return err;
        }
    }

    return {};
}

std::expected<std::vector<TxQApplyImpl>, ApplyResult>
TxQApplyImpl::processInnerBatchPrelock()
{
    std::vector<TxQApplyImpl> vec;
    vec.reserve(tx_->getBatchTransactions().size());

    for (auto const& tx : tx_->getBatchTransactions())
    {
        // forge PreflightResult for inner tx
        PreflightContext const pfCtx(app_, *tx, view_.rules(), flags_, j_);
        auto const cons = invokeConsequences(pfCtx);
        if (!cons)
            return std::unexpected(ApplyResult{cons.error(), false});
        PreflightResult const pfRes(pfCtx, {tesSUCCESS, *cons});

        // Pseudo recursion
        vec.emplace_back(txq_, app_, view_, std::cref(*this), tx, pfRes, flags_, j_);
        auto& txqImpl = vec.back();

        if (auto const err = txqImpl.applyImplPrelock(); err.has_value())
            return std::unexpected(*err);
    }

    return vec;
}

void
TxQApplyImpl::removeInnerTxs(TxQ::AccountMap& byAccount, STTx const& batchTx)
{
    XRPL_ASSERT(
        batchTx.getTxnType() == ttBATCH, "TxQApplyImpl::removeInnerTxs called on non-batch tx");

    auto const batchID = batchTx.getTransactionID();

    // Remove all inner transactions from their respective accounts in byAccount_
    // They are not present in byFee_.
    for (auto const& tx : batchTx.getBatchTransactions())
    {
        AccountID const acc = tx->at(sfAccount);
        SeqProxy const seq = tx->getSeqProxy();

        auto const accIter = byAccount.find(acc);
        if (accIter == byAccount.end())
            continue;

        TxQ::TxQAccount& txqa = accIter->second;
        auto const txIter = txqa.transactions.find(seq);
        if (txIter == txqa.transactions.end())
            continue;

        // Check if inner tx belongs to Batch (it can be just another tx with the same seq)
        auto const& parentTx = txIter->second.parentTx;
        if (!parentTx || parentTx->getTransactionID() != batchID)
            continue;

        txqa.transactions.erase(txIter);

        if (txqa.empty())
            byAccount.erase(accIter);
    }
}

void
TxQApplyImpl::removeBatchTx()
{
    XRPL_ASSERT(tx_->getTxnType() == ttBATCH, "TxQApplyImpl::removeBatchTx called on non-batch tx");
    XRPL_ASSERT(!parent_, "TxQApplyImpl::removeBatchTx called on inner tx");

    // Remove all inner transactions from the queue
    removeInnerTxs(txq_.byAccount_, *tx_);

    // Now remove batch transaction itself
    // The outer batch is in both byAccount_ and byFee_
    if (byAccountIter_ != txq_.byAccount_.end())
    {
        TxQ::TxQAccount& txqa = byAccountIter_->second;
        auto const txIter = txqa.transactions.find(txSeq_);
        if (txIter != txqa.transactions.end())
        {
            // Remove from byFee_
            auto const byFeeIter = txq_.byFee_.iterator_to(txIter->second);
            txq_.byFee_.erase(byFeeIter);

            // Remove from byAccount_
            txqa.transactions.erase(txIter);
            // clean byAccount_
            if (byAccountIter_->second.empty())
                txq_.byAccount_.erase(byAccountIter_);
        }
    }

    JLOG(j_.debug()) << "Removed batch transaction " << txID_ << " and its "
                     << tx_->getBatchTransactions().size() << " inner transactions from queue";
}

//////////////////////////////////////////////////////////////////////////

std::size_t
TxQ::FeeMetrics::update(
    Application& app,
    ReadView const& view,
    bool timeLeap,
    TxQ::Setup const& setup)
{
    std::vector<FeeLevel64> feeLevels;
    auto const txBegin = view.txs.begin();
    auto const txEnd = view.txs.end();
    auto const size = std::distance(txBegin, txEnd);
    feeLevels.reserve(size);
    std::for_each(txBegin, txEnd, [&](auto const& tx) {
        feeLevels.push_back(getFeeLevelPaid(view, *tx.first));
    });
    std::ranges::sort(feeLevels);
    XRPL_ASSERT(size == feeLevels.size(), "xrpl::TxQ::FeeMetrics::update : fee levels size");

    JLOG((timeLeap ? j_.warn() : j_.debug()))
        << "Ledger " << view.header().seq << " has " << size << " transactions. "
        << "Ledgers are processing " << (timeLeap ? "slowly" : "as expected")
        << ". Expected transactions is currently " << txnsExpected_ << " and multiplier is "
        << escalationMultiplier_;

    if (timeLeap)
    {
        // Ledgers are taking to long to process,
        // so clamp down on limits.
        auto const cutPct = 100 - setup.slowConsensusDecreasePercent;
        // upperLimit must be >= minimumTxnCount_ or std::clamp can give
        // unexpected results
        auto const upperLimit = std::max<std::uint64_t>(
            mulDiv(txnsExpected_, cutPct, 100).value_or(xrpl::kMuldivMax), minimumTxnCount_);
        txnsExpected_ = std::clamp<std::uint64_t>(
            mulDiv(size, cutPct, 100).value_or(xrpl::kMuldivMax), minimumTxnCount_, upperLimit);
        recentTxnCounts_.clear();
    }
    else if (size > txnsExpected_ || size > targetTxnCount_)
    {
        recentTxnCounts_.push_back(mulDiv(size, 100 + setup.normalConsensusIncreasePercent, 100)
                                       .value_or(xrpl::kMuldivMax));
        auto const iter = std::ranges::max_element(recentTxnCounts_);
        BOOST_ASSERT(iter != recentTxnCounts_.end());
        auto const next = [&] {
            // Grow quickly: If the max_element is >= the
            // current size limit, use it.
            if (*iter >= txnsExpected_)
                return *iter;
            // Shrink slowly: If the max_element is < the
            // current size limit, use a limit that is
            // 90% of the way from max_element to the
            // current size limit.
            return ((txnsExpected_ * 9) + *iter) / 10;
        }();
        // Ledgers are processing in a timely manner,
        // so keep the limit high, but don't let it
        // grow without bound.
        txnsExpected_ = std::min(next, maximumTxnCount_.value_or(next));
    }

    if (size == 0)
    {
        escalationMultiplier_ = setup.minimumEscalationMultiplier;
    }
    else
    {
        // In the case of an odd number of elements, this
        // evaluates to the middle element; for an even
        // number of elements, it will add the two elements
        // on either side of the "middle" and average them.
        escalationMultiplier_ =
            (feeLevels[size / 2] + feeLevels[(size - 1) / 2] + FeeLevel64{1}) / 2;
        escalationMultiplier_ = std::max(escalationMultiplier_, setup.minimumEscalationMultiplier);
    }
    JLOG(j_.debug()) << "Expected transactions updated to " << txnsExpected_
                     << " and multiplier updated to " << escalationMultiplier_;

    return size;
}

FeeLevel64
TxQ::FeeMetrics::scaleFeeLevel(Snapshot const& snapshot, OpenView const& view)
{
    // Transactions in the open ledger so far
    auto const current = view.txCount();

    auto const target = snapshot.txnsExpected;
    auto const multiplier = snapshot.escalationMultiplier;

    // Once the open ledger bypasses the target,
    // escalate the fee quickly.
    if (current > target)
    {
        // Compute escalated fee level
        // Don't care about the overflow flag
        return mulDiv(multiplier, current * current, target * target)
            .value_or(static_cast<FeeLevel64>(xrpl::kMuldivMax));
    }

    return kBaseLevel;
}

namespace detail {

static constexpr std::pair<bool, std::uint64_t>
sumOfFirstSquares(std::size_t xIn)
{
    // sum(n = 1->x) : n * n = x(x + 1)(2x + 1) / 6

    // We expect that size_t == std::uint64_t but, just in case, guarantee
    // we lose no bits.
    std::uint64_t const x{xIn};

    // If x is anywhere on the order of 2^^21, it's going
    // to completely dominate the computation and is likely
    // enough to overflow that we're just going to assume
    // it does. If we have anywhere near 2^^21 transactions
    // in a ledger, this is the least of our problems.
    if (x >= (1 << 21))
        return {false, std::numeric_limits<std::uint64_t>::max()};
    return {true, (x * (x + 1) * ((2 * x) + 1)) / 6};
}

// Unit tests for sumOfSquares()
static_assert(sumOfFirstSquares(1).first);
static_assert(sumOfFirstSquares(1).second == 1);

static_assert(sumOfFirstSquares(2).first);
static_assert(sumOfFirstSquares(2).second == 5);

static_assert(sumOfFirstSquares(0x1FFFFF).first);
static_assert(sumOfFirstSquares(0x1FFFFF).second == 0x2AAAA8AAAAB00000ul);

static_assert(!sumOfFirstSquares(0x200000).first);
static_assert(sumOfFirstSquares(0x200000).second == std::numeric_limits<std::uint64_t>::max());

}  // namespace detail

std::pair<bool, FeeLevel64>
TxQ::FeeMetrics::escalatedSeriesFeeLevel(
    Snapshot const& snapshot,
    OpenView const& view,
    std::size_t extraCount,
    std::size_t seriesSize)
{
    /* Transactions in the open ledger so far.
        AKA Transactions that will be in the open ledger when
        the first tx in the series is attempted.
    */
    auto const current = view.txCount() + extraCount;
    /* Transactions that will be in the open ledger when
        the last tx in the series is attempted.
    */
    auto const last = current + seriesSize - 1;

    auto const target = snapshot.txnsExpected;
    auto const multiplier = snapshot.escalationMultiplier;

    XRPL_ASSERT(
        current > target,
        "xrpl::TxQ::FeeMetrics::escalatedSeriesFeeLevel : current over "
        "target");

    /* Calculate (apologies for the terrible notation)
        sum(n = current -> last) : multiplier * n * n / (target * target)
        multiplier / (target * target) * (sum(n = current -> last) : n * n)
        multiplier / (target * target) * ((sum(n = 1 -> last) : n * n) -
            (sum(n = 1 -> current - 1) : n * n))
    */
    auto const sumNlast = detail::sumOfFirstSquares(last);
    auto const sumNcurrent = detail::sumOfFirstSquares(current - 1);
    // because `last` is bigger, if either sum overflowed, then
    // `sumNlast` definitely overflowed. Also the odds of this
    // are nearly nil.
    if (!sumNlast.first)
        return {sumNlast.first, FeeLevel64{sumNlast.second}};
    auto const totalFeeLevel =
        mulDiv(multiplier, sumNlast.second - sumNcurrent.second, target * target);

    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    return {totalFeeLevel.has_value(), *totalFeeLevel};
}

LedgerHash TxQ::MaybeTx::parentHashComp{};

TxQ::MaybeTx::MaybeTx(
    std::shared_ptr<STTx const> const& txn,
    TxID const& txId,
    FeeLevel64 feeLevel,
    ApplyFlags const flags,
    PreflightResult const& pfResult,
    std::shared_ptr<STTx const> const& parent)
    : txn(txn)
    , parentTx(parent)
    , feeLevel(feeLevel)
    , txID(txId)
    , account(txn->getAccountID(sfAccount))
    , lastValid(getLastLedgerSequence(*txn))
    , seqProxy(txn->getSeqProxy())
    , flags(flags)
    , pfResult(pfResult)
{
}

ApplyResult
TxQ::MaybeTx::apply(Application& app, OpenView& view, beast::Journal j)
{
    // If the rules or flags change, preflight again
    XRPL_ASSERT(pfResult, "xrpl::TxQ::MaybeTx::apply : preflight result is set");

    // NOLINTBEGIN(bugprone-unchecked-optional-access) assert above
    if (pfResult->rules != view.rules() || pfResult->flags != flags)
    {
        JLOG(j.debug()) << "Queued transaction " << txID
                        << " rules or flags have changed. Flags from " << pfResult->flags << " to "
                        << flags;

        pfResult.emplace(preflight(app, view.rules(), pfResult->tx, flags, pfResult->j));
    }

    auto pcresult = preclaim(*pfResult, app, view);
    // NOLINTEND(bugprone-unchecked-optional-access)

    return doApply(pcresult, app, view);
}

TxQ::TxQAccount::TxQAccount(std::shared_ptr<STTx const> const& txn)
    : TxQAccount(txn->getAccountID(sfAccount))
{
}

TxQ::TxQAccount::TxQAccount(AccountID const& account) : account(account)
{
}

TxQ::TxQAccount::TxMap::const_iterator
TxQ::TxQAccount::getPrevTx(SeqProxy seqProx) const
{
    // Find the entry that is greater than or equal to the new transaction,
    // then decrement the iterator.
    auto sameOrPrevIter = transactions.lower_bound(seqProx);
    if (sameOrPrevIter != transactions.begin())
        --sameOrPrevIter;
    return sameOrPrevIter;
}

TxQ::MaybeTx&
TxQ::TxQAccount::add(MaybeTx&& txn)
{
    auto const seqProx = txn.seqProxy;
    [[maybe_unused]] auto const* txnPtr = &txn;

    auto result = transactions.emplace(seqProx, std::move(txn));
    XRPL_ASSERT(result.second, "xrpl::TxQ::TxQAccount::add : emplace succeeded");
    XRPL_ASSERT(&result.first->second != txnPtr, "xrpl::TxQ::TxQAccount::add : transaction moved");

    return result.first->second;
}

bool
TxQ::TxQAccount::remove(SeqProxy seqProx)
{
    return transactions.erase(seqProx) != 0;
}

//////////////////////////////////////////////////////////////////////////

TxQ::TxQ(Setup const& setup, beast::Journal j)
    : setup_(setup), j_(j), feeMetrics_(setup, j), maxSize_(std::nullopt)
{
}

TxQ::~TxQ()
{
    byFee_.clear();
}

template <size_t FillPercentage>
bool
TxQ::isFull() const
{
    static_assert(FillPercentage > 0 && FillPercentage <= 100, "Invalid fill percentage");
    return maxSize_ && byFee_.size() >= (*maxSize_ * FillPercentage / 100);
}

TER
TxQ::canBeHeld(
    STTx const& tx,
    ApplyFlags const flags,
    OpenView const& view,
    SLE::const_ref sleAccount,
    AccountMap::const_iterator const& accountIter,
    std::optional<TxQAccount::TxMap::const_iterator> const& replacementIter,
    std::scoped_lock<std::mutex> const& lock) const
{
    // PreviousTxnID is deprecated and should never be used.
    // AccountTxnID is not supported by the transaction
    // queue yet, but should be added in the future.
    // TapFailHard transactions are never held
    if (tx.isFieldPresent(sfPreviousTxnID) || tx.isFieldPresent(sfAccountTxnID) ||
        ((flags & TapFailHard) != 0u))
        return telCAN_NOT_QUEUE;

    // Disallow delegated transactions from being queued.
    if (tx.isFieldPresent(sfDelegate))
        return telCAN_NOT_QUEUE;
    // Disallow fee-sponsored transactions from being queued.
    if (isFeeSponsored(tx))
        return telCAN_NOT_QUEUE;

    {
        // To be queued and relayed, the transaction needs to
        // promise to stick around for long enough that it has
        // a realistic chance of getting into a ledger.
        auto const lastValid = getLastLedgerSequence(tx);
        if (lastValid && *lastValid < view.header().seq + setup_.minimumLastLedgerBuffer)
            return telCAN_NOT_QUEUE;
    }

    // Allow if the account is not in the queue at all.
    if (accountIter == byAccount_.end())
        return tesSUCCESS;

    // Allow this tx to replace another one.
    if (replacementIter)
        return tesSUCCESS;

    // Allow if there are fewer than the limit.
    TxQAccount const& txQAcct = accountIter->second;
    if (txQAcct.getTxnCount() < setup_.maximumTxnPerAccount)
        return tesSUCCESS;

    // If we get here the queue limit is exceeded.  Only allow if this
    // transaction fills the _first_ sequence hole for the account.
    auto const txSeqProx = tx.getSeqProxy();
    if (txSeqProx.isTicket())
    {
        // Tickets always follow sequence-based transactions, so a ticket
        // cannot unblock a sequence-based transaction.
        return telCAN_NOT_QUEUE_FULL;
    }

    // This is the next queuable sequence-based SeqProxy for the account.
    SeqProxy const nextQueuable = nextQueuableSeqImpl(sleAccount, lock);
    if (txSeqProx != nextQueuable)
    {
        // The provided transaction does not fill the next open sequence gap.
        return telCAN_NOT_QUEUE_FULL;
    }

    // Make sure they are not just topping off the account's queued
    // sequence-based transactions.
    if (auto const nextTxIter = txQAcct.transactions.upper_bound(nextQueuable);
        nextTxIter != txQAcct.transactions.end() && nextTxIter->first.isSeq())
    {
        // There is a next transaction and it is sequence based.  They are
        // filling a real gap.  Allow it.
        return tesSUCCESS;
    }

    return telCAN_NOT_QUEUE_FULL;
}

auto
TxQ::erase(TxQ::FeeMultiSet::const_iterator_type candidateIter) -> FeeMultiSet::iterator_type
{
    auto& txQAccount = byAccount_.at(candidateIter->account);
    auto const seqProx = candidateIter->seqProxy;

    XRPL_ASSERT(candidateIter->txn, "xrpl::TxQ::erase : transaction exists");
    if (candidateIter->txn->getTxnType() == ttBATCH)
        TxQApplyImpl::removeInnerTxs(byAccount_, *candidateIter->txn);

    auto const newCandidateIter = byFee_.erase(candidateIter);
    // Now that the candidate has been removed from the
    // intrusive list remove it from the TxQAccount
    // so the memory can be freed.
    [[maybe_unused]] auto const found = txQAccount.remove(seqProx);
    XRPL_ASSERT(found, "xrpl::TxQ::erase : account removed");

    return newCandidateIter;
}

bool
TxQApplyImpl::isFirstInAccount(TxQ::AccountMap const& map, TxQ::MaybeTx const& candidate)
{
    // A single (account, seqProxy) pair is "first" if it is at the front
    // (lowest SeqProxy) of that account's queued transactions.
    auto const checkFirst = [&map](AccountID const& acc, SeqProxy seqProx) {
        auto const accIter = map.find(acc);
        if (accIter == map.end())
            return false;

        XRPL_ASSERT(
            !accIter->second.transactions.empty(),
            "TxQApplyImpl::isFirstInAccount check tx in byAccount_");
        if (accIter->second.transactions.empty())
            return false;

        return accIter->second.transactions.begin()->first == seqProx;
    };

    bool const isFirst = checkFirst(candidate.account, candidate.seqProxy);
    if (candidate.txn->getTxnType() != ttBATCH)
        return isFirst;

    // To return true batch itself and every inner tx should be "first" txs.
    // If inner is not first, check if all previous txs are from the same batch
    auto const canRun = [&map, &candidate](AccountID const& acc, SeqProxy seqProx) {
        auto const accIter = map.find(acc);
        if (accIter == map.end())
            return false;

        XRPL_ASSERT(
            !accIter->second.transactions.empty(),
            "TxQApplyImpl::isFirstInAccount check inner tx in byAccount_");
        if (accIter->second.transactions.empty())
            return false;

        auto const& txMap = accIter->second.transactions;
        for (auto it = txMap.begin(); it != txMap.end() && it->first < seqProx; ++it)
        {
            if (it->second.txn == candidate.txn)
                continue;

            auto const& parentTx = it->second.parentTx;
            if (!parentTx || parentTx->getTransactionID() != candidate.txID)
                return false;
        }
        return true;
    };

    return std::ranges::all_of(candidate.txn->getBatchTransactions(), [&canRun](auto const& inner) {
        return canRun(inner->at(sfAccount), inner->getSeqProxy());
    });
}

auto
TxQ::eraseAndAdvance(TxQ::FeeMultiSet::const_iterator_type candidateIter)
    -> FeeMultiSet::iterator_type
{
    auto& txQAccount = byAccount_.at(candidateIter->account);
    auto const accountIter = txQAccount.transactions.find(candidateIter->seqProxy);
    XRPL_ASSERT(
        accountIter != txQAccount.transactions.end(), "xrpl::TxQ::eraseAndAdvance : account found");

    // Note that sequence-based transactions must be applied in sequence order
    // from smallest to largest.  But ticket-based transactions can be
    // applied in any order.
    XRPL_ASSERT(
        candidateIter->seqProxy.isTicket() || accountIter == txQAccount.transactions.begin(),
        "xrpl::TxQ::eraseAndAdvance : ticket or sequence");
    XRPL_ASSERT(
        byFee_.iterator_to(accountIter->second) == candidateIter,
        "xrpl::TxQ::eraseAndAdvance : found in byFee");

    auto accountNextIter = std::next(accountIter);

    // parentAccNextIter will be equal to accountNextIter in case of regular tx.
    // In case of Batch it will point to parent Batch tx.
    // Every time we blocked by inner tx seq, lets try to execute batch once again
    auto const parentAccNextIter = [&]() {
        for (; accountNextIter != txQAccount.transactions.end(); ++accountNextIter)
        {
            auto const& parentTx = accountNextIter->second.parentTx;
            if (!parentTx)
                return accountNextIter;  // regular tx

            // batch tx
            auto& txqa = byAccount_.at(parentTx->at(sfAccount));
            auto const parentIter = txqa.transactions.find(parentTx->getSeqProxy());
            if (parentIter == txqa.transactions.end())
                return accountNextIter;
            if (parentIter->second.txID != candidateIter->txID)
                return parentIter;
        }
        return accountNextIter;
    }();

    // Check if the next transaction for this account is earlier in the queue, and have higher fee
    // than next byFee_, which means we skipped it earlier, and need to try it again.
    // The fee guard must be evaluated before we erase the current candidate, since
    // feeNextIter is relative to candidateIter in byFee_.
    auto const feeNextIter = std::next(candidateIter);
    bool const hasNext = accountNextIter != txQAccount.transactions.end();
    bool const feeAllowsNext = hasNext &&
        (feeNextIter == byFee_.end() ||
         byFee_.value_comp()(parentAccNextIter->second, *feeNextIter));

    // Delete processed txs.
    XRPL_ASSERT(candidateIter->txn, "xrpl::TxQ::eraseAndAdvance : transaction exists");
    if (candidateIter->txn->getTxnType() == ttBATCH)
        TxQApplyImpl::removeInnerTxs(byAccount_, *candidateIter->txn);
    auto const candidateNextIter = byFee_.erase(candidateIter);
    txQAccount.transactions.erase(accountIter);

    // The seq guard is evaluated after the erase so that isFirstInAccount sees
    // the queue without the just-applied candidate. Only jump to the successor
    // (which for a inner Batch is the parent Batch entry) if it is genuinely first in
    // its account(s) - for a Batch that means the Batch and all its inner txs.
    bool const useAccountNext =
        feeAllowsNext && TxQApplyImpl::isFirstInAccount(byAccount_, parentAccNextIter->second);

    if (useAccountNext)
        return byFee_.iterator_to(parentAccNextIter->second);
    return candidateNextIter;
}

auto
TxQ::erase(
    TxQ::TxQAccount& txQAccount,
    TxQ::TxQAccount::TxMap::const_iterator begin,
    TxQ::TxQAccount::TxMap::const_iterator end) -> TxQAccount::TxMap::iterator
{
    for (auto it = begin; it != end; ++it)
    {
        XRPL_ASSERT(it->second.txn, "xrpl::TxQ::erase : transaction exists");
        XRPL_ASSERT(!it->second.parentTx, "xrpl::TxQ::erase : erase regular tx (not batch inner )");
        if (it->second.txn->getTxnType() == ttBATCH)
            TxQApplyImpl::removeInnerTxs(byAccount_, *it->second.txn);

        byFee_.erase(byFee_.iterator_to(it->second));
    }
    return txQAccount.transactions.erase(begin, end);
}

ApplyResult
TxQ::tryClearAccountQueueUpThruTx(
    Application& app,
    OpenView& view,
    STTx const& tx,
    TxQ::AccountMap::iterator const& accountIter,
    TxQAccount::TxMap::iterator const& beginTxIter,
    FeeLevel64 feeLevelPaid,
    PreflightResult const& pfResult,
    std::size_t const txExtraCount,
    ApplyFlags flags,
    FeeMetrics::Snapshot const& metricsSnapshot,
    beast::Journal j)
{
    SeqProxy const tSeqProx{tx.getSeqProxy()};
    XRPL_ASSERT(
        beginTxIter != accountIter->second.transactions.end(),
        "xrpl::TxQ::tryClearAccountQueueUpThruTx : non-empty accounts input");

    // This check is only concerned with the range from
    // [aSeqProxy, tSeqProxy)
    auto endTxIter = accountIter->second.transactions.lower_bound(tSeqProx);
    auto const dist = std::distance(beginTxIter, endTxIter);

    // Can't clean if there is inner tx in queue
    for (auto it = beginTxIter; it != endTxIter; ++it)
    {
        if (it->second.parentTx)
            return {telINSUF_FEE_P, false};
    }

    auto const requiredTotalFeeLevel =
        FeeMetrics::escalatedSeriesFeeLevel(metricsSnapshot, view, txExtraCount, dist + 1);
    // If the computation for the total manages to overflow (however extremely
    //    unlikely), then there's no way we can confidently verify if the queue
    //    can be cleared.
    if (!requiredTotalFeeLevel.first)
        return {telINSUF_FEE_P, false};

    auto const totalFeeLevelPaid = std::accumulate(
        beginTxIter, endTxIter, feeLevelPaid, [](auto const& total, auto const& txn) {
            return total + txn.second.feeLevel;
        });

    // This transaction did not pay enough, so fall back to the normal process.
    if (totalFeeLevelPaid < requiredTotalFeeLevel.second)
        return {telINSUF_FEE_P, false};

    // This transaction paid enough to clear out the queue.
    // Attempt to apply the queued transactions.
    for (auto it = beginTxIter; it != endTxIter; ++it)
    {
        auto txResult = it->second.apply(app, view, j);
        // Succeed or fail, use up a retry, because if the overall
        // process fails, we want the attempt to count. If it all
        // succeeds, the MaybeTx will be destructed, so it'll be
        // moot.
        --it->second.retriesRemaining;
        it->second.lastResult = txResult.ter;

        // In TxQ::apply we note that it's possible for a transaction with
        // a ticket to both be in the queue and in the ledger.  And, while
        // we're in TxQ::apply, it's too expensive to filter those out.
        //
        // So here in tryClearAccountQueueUpThruTx we just received a batch of
        // queued transactions.  And occasionally one of those is a ticketed
        // transaction that is both in the queue and in the ledger.  When
        // that happens the queued transaction returns tefNO_TICKET.
        //
        // The transaction that returned tefNO_TICKET can never succeed
        // and we'd like to get it out of the queue as soon as possible.
        // The easiest way to do that from here is to treat the transaction
        // as though it succeeded and attempt to clear the remaining
        // transactions in the account queue.  Then, if clearing the account
        // is successful, we will have removed any ticketed transactions
        // that can never succeed.
        if (txResult.ter == tefNO_TICKET)
            continue;

        if (!txResult.applied)
        {
            // Transaction failed to apply. Fall back to the normal process.
            return {txResult.ter, false};
        }
    }
    // Apply the current tx. Because the state of the view has been changed
    // by the queued txs, we also need to preclaim again.
    auto const txResult = doApply(preclaim(pfResult, app, view), app, view);

    if (txResult.applied)
    {
        // All of the queued transactions applied, so remove them from the
        // queue.
        endTxIter = erase(accountIter->second, beginTxIter, endTxIter);
        // If `tx` is replacing a queued tx, delete that one, too.
        if (endTxIter != accountIter->second.transactions.end() && endTxIter->first == tSeqProx)
            erase(accountIter->second, endTxIter, std::next(endTxIter));
    }

    return txResult;
}

// Overview of considerations for when a transaction is accepted into the TxQ:
//
// These rules apply to the transactions in the queue owned by a single
// account.  Briefly, the primary considerations are:
//
// 1. Is the new transaction blocking?
// 2. Is there an expiration gap in the account's sequence-based transactions?
// 3. Does the new transaction replace one that is already in the TxQ?
// 4. Is the transaction's sequence or ticket value acceptable for this account?
// 5. Is the transaction likely to claim a fee?
// 6. Is the queue full?
//
// Here are more details.
//
// 1. A blocking transaction is one that would change the validity of following
//    transactions for the issuing account.  Examples of blocking transactions
//    include SetRegularKey and SignerListSet.
//
//    A blocking transaction can only be added to the queue for an account if:
//
//    a. The queue for that account is empty, or
//
//    b. The blocking transaction replaces the only transaction in the
//       account's queue.
//
//    While a blocker is in the account's queue no additional transactions
//    can be added to the queue.
//
//    As a consequence, any blocker is always alone in the account's queue.
//
// 2. Transactions are given unique identifiers using either Sequence numbers
//    or Tickets.  In general, sequence numbers in the queue are expected to
//    start with the account root sequence and increment from there.  There
//    are two exceptions:
//
//    a. Sequence holes left by ticket creation.  If a transaction creates
//       more than one ticket, then the account sequence number will jump
//       by the number of tickets created.  These holes are fine.
//
//    b. Sequence gaps left by transaction expiration.  If transactions stay
//       in the queue long enough they may expire.  If that happens it leaves
//       gaps in the sequence numbers held by the queue.  These gaps are
//       important because, if left in place, they will block any later
//       sequence-based transactions in the queue from working.  Remember,
//       for any given account sequence numbers must be used consecutively
//       (with the exception of ticket-induced holes).
//
// 3. Transactions in the queue may be replaced.  If a transaction in the
//    queue has the same SeqProxy as the incoming transaction, then the
//    transaction in the queue will be replaced if the following conditions
//    are met:
//
//    a. The replacement must provide a fee that is at least 1.25 times the
//       fee of the transaction it is replacing.
//
//    b. If the transaction being replaced has a sequence number, then
//       the transaction may not be after any expiration-based sequence
//       gaps in the account's queue.
//
//    c. A replacement that is a blocker is only allowed if the transaction
//       it replaces is the only transaction in the account's queue.
//
// 4. The transaction that is not a replacement must have an acceptable
//    sequence or ticket ID:
//
//    Sequence: For a given account's queue configuration there is at most
//    one sequence number that is acceptable to the queue for that account.
//    The rules are:
//
//    a. If there are no sequence-based transactions in the queue and the
//       candidate transaction has a sequence number, that value must match
//       the account root's sequence.
//
//    b. If there are sequence-based transactions in the queue for that
//       account and there are no expiration-based gaps, then the candidate's
//       sequence number must belong at the end of the list of sequences.
//
//    c. If there are expiration-based gaps in the sequence-based
//       transactions in the account's queue, then the candidate's sequence
//       value must go precisely at the front of the first gap.
//
//    Ticket: If there are no blockers or sequence gaps in the account's
//    queue, then there are many tickets that are acceptable to the queue
//    for that account.  The rules are:
//
//    a. If there are no blockers in the account's queue and the ticket
//       required by the transaction is in the ledger then the transaction
//       may be added to the account's queue.
//
//    b. If there is a ticket-based blocker in the account's queue then
//       that blocker can be replaced.
//
//    Note that it is not sufficient for the transaction that would create
//    the necessary ticket to be in the account's queue.  The required ticket
//    must already be in the ledger.  This avoids problems that can occur if
//    a ticket-creating transaction enters the queue but expires out of the
//    queue before its tickets are created.
//
// 5. The transaction must be likely to claim a fee.  In general that is
//    checked by having preclaim return a tes or tec code.
//
//    Extra work is done here to account for funds that other transactions
//    in the queue remove from the account.
//
// 6. The queue must not be full.
//
//    a. Each account can queue up to a maximum of 10 transactions.  Beyond
//       that transactions are rejected.  There is an exception for this case
//       when filling expiration-based sequence gaps.
//
//    b. The entire queue also has a (dynamic) maximum size.  Transactions
//       beyond that limit are rejected.
//

ApplyResult
TxQ::apply(
    Application& app,
    OpenView& view,
    std::shared_ptr<STTx const> const& tx,
    ApplyFlags flags,
    beast::Journal j)
{
    TxQApplyImpl impl(*this, app, view, tx, flags, j);
    return impl.apply();
}

ApplyResult
TxQApplyImpl::apply()
{
    // See if the transaction is valid, properly formed, etc.
    // Before doing potentially expensive queue replace and multi-transaction operations.
    if (!isTesSuccess(preflightRes_.ter))
        return {preflightRes_.ter, false};

    if (auto const err = applyImplPrelock(); err.has_value())
        return *err;

    std::expected<std::vector<TxQApplyImpl>, ApplyResult> prelockRes;
    if (tx_->getTxnType() == ttBATCH)
    {
        prelockRes = processInnerBatchPrelock();
        if (!prelockRes)
            return prelockRes.error();
    }

    std::scoped_lock const lock(txq_.mutex_);
    txqLock_ = lock;

    auto res = applyImpl();
    if (res.ter != terQUEUED)
        return res;

    // NOLINTBEGIN(bugprone-unchecked-optional-access)
    if (tx_->getTxnType() == ttBATCH)
    {
        if (auto const err = processInnerBatch(*prelockRes); err.has_value())
        {
            removeBatchTx();
            return *err;
        }
    }
    // NOLINTEND(bugprone-unchecked-optional-access)

    return res;
}

std::optional<ApplyResult>
TxQApplyImpl::applyImplPrelock()
{
    // See if the transaction paid a high enough fee that it can go straight
    // into the ledger.
    if (!parent_)
    {
        if (auto const directApplied = txq_.tryDirectApply(app_, view_, tx_, flags_, j_))
            return directApplied;
    }

    if ((flags_ & TapDryRun) != 0u)
        return ApplyResult{telCAN_NOT_QUEUE, false};

    // If we get past tryDirectApply() without returning then we expect
    // one of the following to occur:
    //
    //  o We will decide the transaction is unlikely to claim a fee.
    //  o The transaction paid a high enough fee that fee averaging will apply.
    //  o The transaction will be queued.

    // If the account is not currently in the ledger, don't queue its tx.
    accSle_ = view_.read(accKey_);
    if (!accSle_)
        return ApplyResult{terNO_ACCOUNT, false};
    accSeq_ = SeqProxy::rawSequence(accSle_->at(sfSequence));

    // If the transaction needs a Ticket is that Ticket in the ledger?
    if (auto const err = checkTicketInLedger(); err.has_value())
        return err;

    return {};
}

ApplyResult
TxQApplyImpl::applyImpl()
{
    byAccountIter_ = txq_.byAccount_.find(accID_);

    // _If_ the account is in the queue, then ignore any sequence-based
    // queued transactions that slipped into the ledger while we were not
    // watching.  This does actually happen in the wild, but it's uncommon.
    //
    // Note that we _don't_ ignore queued ticket-based transactions that
    // slipped into the ledger while we were not watching.  It would be
    // desirable to do so, but the measured cost was too high since we have
    // to individually check each queued ticket against the ledger.
    retrievePrevTxs();

    // Is tx a blocker?  If so there are very limited conditions when it
    // is allowed in the TxQ:
    //  1. If the account's queue is empty or
    //  2. If the blocker replaces the only entry in the account's queue.
    if (auto const err = checkIsTxBlocker(); err.has_value())
        return *err;

    // If the transaction is intending to replace a transaction in the queue
    // identify the one that might be replaced.
    if (auto const err = retrieveTxToReplace(); err.has_value())
        return *err;

    // We may need the base fee for multiple transactions or transaction
    // replacement, so just pull it up now.
    metricsSnapshot_.emplace(txq_.feeMetrics_.getSnapshot());
    feeLevelPaid_ = getFeeLevelPaid(view_, *tx_);
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    requiredFeeLevel_ = txq_.getRequiredFeeLevel(view_, flags_, *metricsSnapshot_, *txqLock_);

    // Is there a blocker already in the account's queue?  If so, don't
    // allow additional transactions in the queue.
    if (auto const err = checkBlockerInAccQue(); err.has_value())
        return *err;

    if (accTxCount_ == 0)
    {
        // There are no queued transactions for this account.  If the
        // transaction has a sequence make sure it's valid (tickets
        // are checked elsewhere).
        if (txSeq_.isSeq())
        {
            if (accSeq_ > txSeq_)
                return {tefPAST_SEQ, false};
            if (accSeq_ < txSeq_)
                return {terPRE_SEQ, false};
        }
    }
    else
    {
        // Calculate expenses and fees
        // Create multiTxn_ test view from the current view and apply balance and accSeq to it,
        // preclaim(...) will use it.
        if (auto const err = processAccountTxs(); err.has_value())
            return *err;
    }

    // See if the transaction is likely to claim a fee.
    //
    // We assume that if the transaction survives preclaim(), then it
    // is likely to claim a fee.  However we can't allow preclaim to
    // check the sequence/ticket.  Transactions in the queue may be
    // responsible for increasing the sequence, and mocking those up
    // is non-trivially expensive.
    //
    // Note that earlier code has already verified that the sequence/ticket
    // is valid.  So we use a special entry point that runs all of the
    // preclaim checks with the exception of the sequence check.
    //
    // Inner tx doesn't claim a fee, but they already processed preclaim as part of batch
    // preclaim
    if (!parent_)
    {
        auto const pcresult =
            preclaim(preflightRes_, app_, multiTxn_ ? multiTxn_->openView : view_);
        if (!pcresult.likelyToClaimFee)
            return {pcresult.ter, false};

        // Too low of a fee should get caught by preclaim
        XRPL_ASSERT(feeLevelPaid_ >= TxQ::kBaseLevel, "xrpl::TxQ::apply : minimum fee");
    }

    JLOG(j_.trace()) << "Transaction " << txID_ << " from account " << accID_
                     << " has fee level of " << feeLevelPaid_ << " needs at least "
                     << requiredFeeLevel_ << " to get in the open ledger, which has "
                     << view_.txCount() << " entries.";

    // Quick heuristic check to see if it's worth checking that this tx has a high enough fee to
    // clear all the txs in front of it in the queue.
    // Batch tx can't clean, as it can have multiple accounts
    if (!parent_ && tx_->getTxnType() != ttBATCH)
    {
        if (auto const res = tryClearAccountQueueUpThruTx(); res.has_value())
            return *res;
    }

    if (auto const err = canBeHeld(); err.has_value())
        return *err;

    // If the queue is full, decide whether to drop the current
    // transaction or the last transaction for the account with
    // the lowest fee.
    if (auto const err = processQueueIsFull(); err.has_value())
        return *err;

    // Remove existing tx (will be replaced by new in addTxToQueue).
    if (txToReplaceIter_)
    {
        txq_.removeFromByFee(txToReplaceIter_, tx_);
        txToReplaceIter_.reset();
    }

    addTxToQueue();

    return {terQUEUED, false};
}

/*
    1. Update the fee metrics based on the fee levels of the
        txs in the validated ledger and whether consensus is
        slow.
    2. Adjust the maximum queue size to be enough to hold
        `ledgersInQueue` ledgers.
    3. Remove any transactions from the queue for which the
        `LastLedgerSequence` has passed.
    4. Remove any account objects that have no candidates
        under them.

*/
void
TxQ::processClosedLedger(Application& app, ReadView const& view, bool timeLeap)
{
    std::scoped_lock const lock(mutex_);

    feeMetrics_.update(app, view, timeLeap, setup_);
    auto const& snapshot = feeMetrics_.getSnapshot();

    auto ledgerSeq = view.header().seq;

    if (!timeLeap)
        maxSize_ = std::max(snapshot.txnsExpected * setup_.ledgersInQueue, setup_.queueSizeMin);

    // Remove any queued candidates whose LastLedgerSequence has gone by.
    for (auto candidateIter = byFee_.begin(); candidateIter != byFee_.end();)
    {
        if (candidateIter->lastValid && *candidateIter->lastValid <= ledgerSeq)
        {
            byAccount_.at(candidateIter->account).dropPenalty = true;
            candidateIter = erase(candidateIter);
        }
        else
        {
            ++candidateIter;
        }
    }

    // Remove any TxQAccounts that don't have candidates
    // under them
    for (auto txQAccountIter = byAccount_.begin(); txQAccountIter != byAccount_.end();)
    {
        if (txQAccountIter->second.empty())
        {
            txQAccountIter = byAccount_.erase(txQAccountIter);
        }
        else
        {
            ++txQAccountIter;
        }
    }
}

void
TxQ::rebuildQueue(OpenView& view)
{
    LedgerHash const& parentHash = view.header().parentHash;
    if (parentHash == parentHash_)
    {
        JLOG(j_.warn()) << "Parent ledger hash unchanged from " << parentHash;
    }
    else
    {
        parentHash_ = parentHash;
    }

    [[maybe_unused]] auto const startingSize = byFee_.size();
    // byFee_ doesn't "own" the candidate objects inside it, so it's
    // perfectly safe to wipe it and start over, repopulating from
    // byAccount_.
    //
    // In the absence of a "re-sort the list in place" function, this
    // was the fastest method tried to repopulate the list.
    // Other methods included: create a new list and moving items over one at a
    // time, create a new list and merge the old list into it.
    byFee_.clear();

    MaybeTx::parentHashComp = parentHash;

    for (auto& [_, account] : byAccount_)
    {
        for (auto& [_, candidate] : account.transactions)
        {
            if (candidate.parentTx)
                continue;
            byFee_.insert(candidate);
        }
    }
    XRPL_ASSERT(byFee_.size() == startingSize, "xrpl::TxQ::accept : byFee size match");
}

/*
    How the txs are moved from the queue to the new open ledger.

    1. Iterate over the txs from highest fee level to lowest.
        For each tx:
        a) Is this the first tx in the queue for this account?
            No: Skip this tx. We'll come back to it later.
            Yes: Continue to the next sub-step.
        b) Is the tx fee level less than the current required
                fee level?
            Yes: Stop iterating. Continue to the next step.
            No: Try to apply the transaction. Did it apply?
                Yes: Take it out of the queue. Continue with
                    the next appropriate candidate (see below).
                No: Did it get a tef, tem, or tel, or has it
                        retried `MaybeTx::retriesAllowed`
                        times already?
                    Yes: Take it out of the queue. Continue
                        with the next appropriate candidate
                        (see below).
                    No: Leave it in the queue, track the retries,
                        and continue iterating.
    2. Return indicator of whether the open ledger was modified.

    "Appropriate candidate" is defined as the tx that has the
        highest fee level of:
        * the tx for the current account with the next sequence.
        * the next tx in the queue, simply ordered by fee.
*/
bool
TxQ::accept(Application& app, OpenView& view)
{
    /* Move transactions from the queue from largest fee level to smallest.
       As we add more transactions, the required fee level will increase.
       Stop when the transaction fee level gets lower than the required fee
       level.
    */

    bool ledgerChanged = false;

    std::scoped_lock const lock(mutex_);

    auto const metricsSnapshot = feeMetrics_.getSnapshot();

    for (auto candidateIter = byFee_.begin(); candidateIter != byFee_.end();)
    {
        auto& account = byAccount_.at(candidateIter->account);
        auto const beginIter = account.transactions.begin();
        if (candidateIter->seqProxy.isSeq() && candidateIter->seqProxy > beginIter->first)
        {
            // There is a sequence transaction at the front of the queue and
            // candidate has a later sequence, so skip this candidate.  We
            // need to process sequence-based transactions in sequence order.
            if (candidateIter->txn->getTxnType() == ttBATCH)
            {
                std::cerr << "[TxQ.accept diag] BATCH " << candidateIter->txID
                          << " SKIPPED (candidate seqProxy=" << candidateIter->seqProxy.value()
                          << " > account front seqProxy=" << beginIter->first.value() << ")\n";
            }
            JLOG(j_.trace()) << "Skipping queued transaction " << candidateIter->txID
                             << " from account " << candidateIter->account
                             << " as it is not the first.";
            candidateIter++;
            continue;
        }
        auto const requiredFeeLevel = getRequiredFeeLevel(view, TapNone, metricsSnapshot, lock);
        auto const feeLevelPaid = candidateIter->feeLevel;
        JLOG(j_.trace()) << "Queued transaction " << candidateIter->txID << " from account "
                         << candidateIter->account << " has fee level of " << feeLevelPaid
                         << " needs at least " << requiredFeeLevel;
        if (feeLevelPaid >= requiredFeeLevel)
        {
            JLOG(j_.trace()) << "Applying queued transaction " << candidateIter->txID
                             << " to open ledger.";

            auto const [txnResult, didApply, _metadata] = candidateIter->apply(app, view, j_);

            // === DIAGNOSTICS (Trigger 015): trace Batch outcomes in accept ===
            bool const diagIsBatch = candidateIter->txn->getTxnType() == ttBATCH;
            if (diagIsBatch)
            {
                std::cerr << "[TxQ.accept diag] BATCH " << candidateIter->txID
                          << " apply -> ter=" << transToken(txnResult)
                          << " didApply=" << didApply
                          << " retriesRemaining=" << candidateIter->retriesRemaining
                          << " seqProxy=" << candidateIter->seqProxy.value() << "\n";
            }
            // === END DIAGNOSTICS ===

            if (didApply)
            {
                // Remove the candidate from the queue
                JLOG(j_.debug()) << "Queued transaction " << candidateIter->txID
                                 << " applied successfully with " << transToken(txnResult)
                                 << ". Remove from queue.";

                candidateIter = eraseAndAdvance(candidateIter);
                ledgerChanged = true;
            }
            else if (
                isTefFailure(txnResult) || isTemMalformed(txnResult) ||
                candidateIter->retriesRemaining <= 0)
            {
                if (candidateIter->retriesRemaining <= 0)
                {
                    account.retryPenalty = true;
                }
                else
                {
                    account.dropPenalty = true;
                }
                if (diagIsBatch)
                {
                    std::cerr << "[TxQ.accept diag] BATCH " << candidateIter->txID
                              << " DROPPED (ter=" << transToken(txnResult)
                              << ", retriesRemaining=" << candidateIter->retriesRemaining
                              << ", tef=" << isTefFailure(txnResult)
                              << ", tem=" << isTemMalformed(txnResult) << ")\n";
                }
                JLOG(j_.debug()) << "Queued transaction " << candidateIter->txID << " failed with "
                                 << transToken(txnResult) << ". Remove from queue.";
                candidateIter = eraseAndAdvance(candidateIter);
            }
            else
            {
                if (diagIsBatch)
                {
                    std::cerr << "[TxQ.accept diag] BATCH " << candidateIter->txID
                              << " LEFT IN QUEUE (ter=" << transToken(txnResult)
                              << ", retriesRemaining before decrement="
                              << candidateIter->retriesRemaining << ")\n";
                }
                JLOG(j_.debug()) << "Queued transaction " << candidateIter->txID << " failed with "
                                 << transToken(txnResult) << ". Leave in queue."
                                 << " Applied: " << didApply << ". Flags: " << candidateIter->flags;
                if (account.retryPenalty && candidateIter->retriesRemaining > 2)
                {
                    candidateIter->retriesRemaining = 1;
                }
                else
                {
                    --candidateIter->retriesRemaining;
                }
                candidateIter->lastResult = txnResult;
                if (account.dropPenalty && account.transactions.size() > 1 && isFull<95>())
                {
                    // The queue is close to full, this account has multiple
                    // txs queued, and this account has had a transaction
                    // fail.
                    if (candidateIter->seqProxy.isTicket())
                    {
                        // Since the failed transaction has a ticket, order
                        // doesn't matter.  Drop this one.
                        JLOG(j_.info())
                            << "Queue is nearly full, and transaction " << candidateIter->txID
                            << " failed with " << transToken(txnResult)
                            << ". Removing ticketed tx from account " << account.account;
                        candidateIter = eraseAndAdvance(candidateIter);
                    }
                    else
                    {
                        // Even though we're giving this transaction another
                        // chance, chances are it won't recover. To avoid
                        // making things worse, drop the _last_ transaction for
                        // this account.
                        auto dropRIter = account.transactions.rbegin();
                        XRPL_ASSERT(
                            dropRIter->second.account == candidateIter->account,
                            "xrpl::TxQ::accept : account check");

                        JLOG(j_.info())
                            << "Queue is nearly full, and transaction " << candidateIter->txID
                            << " failed with " << transToken(txnResult)
                            << ". Removing last item from account " << account.account;
                        auto endIter = byFee_.iterator_to(dropRIter->second);
                        if (endIter != candidateIter)
                            erase(endIter);
                        ++candidateIter;
                    }
                }
                else
                {
                    ++candidateIter;
                }
            }
        }
        else
        {
            break;
        }
    }

    // All transactions that can be moved out of the queue into the open
    // ledger have been. Rebuild the queue using the open ledger's
    // parent hash, so that transactions paying the same fee are
    // reordered.
    rebuildQueue(view);

    return ledgerChanged;
}

// Public entry point for nextQueuableSeq().
//
// Acquires a lock and calls the implementation.
SeqProxy
TxQ::nextQueuableSeq(SLE::const_ref sleAccount) const
{
    std::scoped_lock const lock(mutex_);
    return nextQueuableSeqImpl(sleAccount, lock);
}

// The goal is to return a SeqProxy for a sequence that will fill the next
// available hole in the queue for the passed in account.
//
// If there are queued transactions for the account then the first viable
// sequence number, that is not used by a transaction in the queue, must
// be found and returned.
SeqProxy
TxQ::nextQueuableSeqImpl(SLE::const_ref sleAccount, std::scoped_lock<std::mutex> const&) const
{
    // If the account is not in the ledger or a non-account was passed
    // then return zero.  We have no idea.
    if (!sleAccount || sleAccount->getType() != ltACCOUNT_ROOT)
        return SeqProxy::rawSequence(0);

    SeqProxy const acctSeqProx = SeqProxy::rawSequence((*sleAccount)[sfSequence]);

    // If the account is not in the queue then acctSeqProx is good enough.
    auto const accountIter = byAccount_.find((*sleAccount)[sfAccount]);
    if (accountIter == byAccount_.end() || accountIter->second.transactions.empty())
        return acctSeqProx;

    TxQAccount::TxMap const& acctTxs = accountIter->second.transactions;

    // Ignore any sequence-based queued transactions that slipped into the
    // ledger while we were not watching.  This does actually happen in the
    // wild, but it's uncommon.
    auto txIter = acctTxs.lower_bound(acctSeqProx);

    if (txIter == acctTxs.end() || !txIter->first.isSeq() || txIter->first != acctSeqProx)
    {
        // Either...
        //   o There are no queued sequence-based transactions equal to or
        //     following acctSeqProx or
        //   o acctSeqProx is not currently in the queue.
        // So acctSeqProx is as good as it gets.
        return acctSeqProx;
    }

    // There are sequence-based transactions queued that follow acctSeqProx.
    // Locate the first opening to put a transaction into.
    SeqProxy attempt = txIter->second.consequences().followingSeq();
    while (++txIter != acctTxs.cend())
    {
        if (attempt < txIter->first)
            break;

        attempt = txIter->second.consequences().followingSeq();
    }
    return attempt;
}

FeeLevel64
TxQ::getRequiredFeeLevel(
    OpenView& view,
    ApplyFlags flags,
    FeeMetrics::Snapshot const& metricsSnapshot,
    std::scoped_lock<std::mutex> const& lock)
{
    return FeeMetrics::scaleFeeLevel(metricsSnapshot, view);
}

std::optional<ApplyResult>
TxQ::tryDirectApply(
    Application& app,
    OpenView& view,
    std::shared_ptr<STTx const> const& tx,
    ApplyFlags flags,
    beast::Journal j)
{
    auto const account = (*tx)[sfAccount];
    auto const sleAccount = view.read(keylet::account(account));

    // Don't attempt to direct apply if the account is not in the ledger.
    if (!sleAccount)
        return {};

    SeqProxy const acctSeqProx = SeqProxy::rawSequence((*sleAccount)[sfSequence]);
    SeqProxy const txSeqProx = tx->getSeqProxy();

    // Can only directly apply if the transaction sequence matches the account
    // sequence or if the transaction uses a ticket.
    if (txSeqProx.isSeq() && txSeqProx != acctSeqProx)
        return {};

    FeeLevel64 const requiredFeeLevel = [this, &view, flags]() {
        std::scoped_lock const lock(mutex_);
        return getRequiredFeeLevel(view, flags, feeMetrics_.getSnapshot(), lock);
    }();

    // If the transaction's fee is high enough we may be able to put the
    // transaction straight into the ledger.
    FeeLevel64 const feeLevelPaid = getFeeLevelPaid(view, *tx);

    if (feeLevelPaid >= requiredFeeLevel)
    {
        // Attempt to apply the transaction directly.
        auto const transactionID = tx->getTransactionID();
        JLOG(j_.trace()) << "Applying transaction " << transactionID << " to open ledger.";

        auto const [txnResult, didApply, metadata] = xrpl::apply(app, view, *tx, flags, j);

        JLOG(j_.trace()) << "New transaction " << transactionID
                         << (didApply ? " applied successfully with " : " failed with ")
                         << transToken(txnResult);

        if (didApply)
        {
            // If the applied transaction replaced a transaction in the
            // queue then remove the replaced transaction.
            std::scoped_lock const lock(mutex_);

            auto const accountIter = byAccount_.find(account);
            if (accountIter != byAccount_.end())
            {
                TxQAccount& txQAcct = accountIter->second;
                if (auto const existingIter = txQAcct.transactions.find(txSeqProx);
                    existingIter != txQAcct.transactions.end())
                {
                    removeFromByFee(existingIter, tx);
                }
            }
        }
        return ApplyResult{txnResult, didApply, metadata};
    }
    return {};
}

void
TxQ::removeFromByFee(
    std::optional<TxQAccount::TxMap::const_iterator> const& replacedTxIter,
    std::shared_ptr<STTx const> const& tx)
{
    if (replacedTxIter && tx)
    {
        // If the transaction we're holding replaces a transaction in the
        // queue, remove the transaction that is being replaced.
        auto deleteIter = byFee_.iterator_to((*replacedTxIter)->second);
        XRPL_ASSERT(deleteIter != byFee_.end(), "xrpl::TxQ::removeFromByFee : found in byFee");
        XRPL_ASSERT(
            &(*replacedTxIter)->second == &*deleteIter,
            "xrpl::TxQ::removeFromByFee : matching transaction");
        XRPL_ASSERT(
            deleteIter->seqProxy == tx->getSeqProxy(),
            "xrpl::TxQ::removeFromByFee : matching sequence");
        XRPL_ASSERT(
            deleteIter->account == (*tx)[sfAccount],
            "xrpl::TxQ::removeFromByFee : matching account");

        erase(deleteIter);
    }
}

TxQ::Metrics
TxQ::getMetrics(OpenView const& view) const
{
    Metrics result;

    std::scoped_lock const lock(mutex_);

    auto const snapshot = feeMetrics_.getSnapshot();

    result.txCount = byFee_.size();
    result.txQMaxSize = maxSize_;
    result.txInLedger = view.txCount();
    result.txPerLedger = snapshot.txnsExpected;
    result.referenceFeeLevel = kBaseLevel;
    result.minProcessingFeeLevel =
        isFull() ? byFee_.rbegin()->feeLevel + FeeLevel64{1} : kBaseLevel;
    result.medFeeLevel = snapshot.escalationMultiplier;
    result.openLedgerFeeLevel = FeeMetrics::scaleFeeLevel(snapshot, view);

    return result;
}

TxQ::FeeAndSeq
TxQ::getTxRequiredFeeAndSeq(OpenView const& view, std::shared_ptr<STTx const> const& tx) const
{
    auto const account = (*tx)[sfAccount];

    std::scoped_lock const lock(mutex_);

    auto const snapshot = feeMetrics_.getSnapshot();
    auto const baseFee = calculateBaseFee(view, *tx);
    auto const fee = FeeMetrics::scaleFeeLevel(snapshot, view);

    auto const sle = view.read(keylet::account(account));

    std::uint32_t const accountSeq = sle ? (*sle)[sfSequence] : 0;
    std::uint32_t const availableSeq = nextQueuableSeqImpl(sle, lock).value();
    return {
        .fee = mulDiv(fee, baseFee, kBaseLevel)
                   .value_or(XRPAmount(std::numeric_limits<std::int64_t>::max())),
        .accountSeq = accountSeq,
        .availableSeq = availableSeq};
}

std::vector<TxQ::TxDetails>
TxQ::getAccountTxs(AccountID const& account) const
{
    std::vector<TxDetails> result;

    std::scoped_lock const lock(mutex_);

    AccountMap::const_iterator const accountIter{byAccount_.find(account)};

    if (accountIter == byAccount_.end() || accountIter->second.transactions.empty())
        return result;

    result.reserve(accountIter->second.transactions.size());
    for (auto const& tx : accountIter->second.transactions)
    {
        result.emplace_back(tx.second.getTxDetails());
    }
    return result;
}

std::vector<TxQ::TxDetails>
TxQ::getTxs() const
{
    std::vector<TxDetails> result;

    std::scoped_lock const lock(mutex_);

    result.reserve(byFee_.size());

    for (auto const& tx : byFee_)
        result.emplace_back(tx.getTxDetails());

    return result;
}

json::Value
TxQ::doRPC(Application& app) const
{
    auto const view = app.getOpenLedger().current();
    if (!view)
    {
        BOOST_ASSERT(false);
        return {};
    }

    auto const metrics = getMetrics(*view);

    json::Value ret(json::ValueType::Object);

    auto& levels = ret[jss::levels] = json::ValueType::Object;

    ret[jss::ledger_current_index] = view->header().seq;
    ret[jss::expected_ledger_size] = std::to_string(metrics.txPerLedger);
    ret[jss::current_ledger_size] = std::to_string(metrics.txInLedger);
    ret[jss::current_queue_size] = std::to_string(metrics.txCount);
    if (metrics.txQMaxSize)
        ret[jss::max_queue_size] = std::to_string(*metrics.txQMaxSize);

    levels[jss::reference_level] = to_string(metrics.referenceFeeLevel);
    levels[jss::minimum_level] = to_string(metrics.minProcessingFeeLevel);
    levels[jss::median_level] = to_string(metrics.medFeeLevel);
    levels[jss::open_ledger_level] = to_string(metrics.openLedgerFeeLevel);

    auto const baseFee = view->fees().base;
    // If the base fee is 0 drops, but escalation has kicked in, treat the
    // base fee as if it is 1 drop, which makes the rest of the math
    // work.
    auto const effectiveBaseFee = [&baseFee, &metrics]() {
        if (!baseFee && metrics.openLedgerFeeLevel != metrics.referenceFeeLevel)
            return XRPAmount{1};
        return baseFee;
    }();
    auto& drops = ret[jss::drops] = json::Value();

    drops[jss::base_fee] = to_string(baseFee);
    drops[jss::median_fee] = to_string(toDrops(metrics.medFeeLevel, baseFee));
    drops[jss::minimum_fee] = to_string(toDrops(
        metrics.minProcessingFeeLevel,
        metrics.txCount >= metrics.txQMaxSize ? effectiveBaseFee : baseFee));
    auto openFee = toDrops(metrics.openLedgerFeeLevel, effectiveBaseFee);
    if (effectiveBaseFee && toFeeLevel(openFee, effectiveBaseFee) < metrics.openLedgerFeeLevel)
        openFee += 1;
    drops[jss::open_ledger_fee] = to_string(openFee);

    return ret;
}

//////////////////////////////////////////////////////////////////////////

TxQ::Setup
setupTxQ(Config const& config)
{
    TxQ::Setup setup;
    auto const& section = config.section(Sections::kTransactionQueue);
    set(setup.ledgersInQueue, Keys::kLedgersInQueue, section);
    set(setup.queueSizeMin, Keys::kMinimumQueueSize, section);
    set(setup.retrySequencePercent, Keys::kRetrySequencePercent, section);
    set(setup.minimumEscalationMultiplier, Keys::kMinimumEscalationMultiplier, section);
    set(setup.minimumTxnInLedger, Keys::kMinimumTxnInLedger, section);
    set(setup.minimumTxnInLedgerSA, Keys::kMinimumTxnInLedgerStandalone, section);
    set(setup.targetTxnInLedger, Keys::kTargetTxnInLedger, section);
    std::uint32_t max = 0;
    if (set(max, Keys::kMaximumTxnInLedger, section))
    {
        if (max < setup.minimumTxnInLedger)
        {
            Throw<std::runtime_error>(
                "The minimum number of low-fee transactions allowed "
                "per ledger (minimum_txn_in_ledger) exceeds "
                "the maximum number of low-fee transactions allowed per "
                "ledger (maximum_txn_in_ledger).");
        }
        if (max < setup.minimumTxnInLedgerSA)
        {
            Throw<std::runtime_error>(
                "The minimum number of low-fee transactions allowed "
                "per ledger (minimum_txn_in_ledger_standalone) exceeds "
                "the maximum number of low-fee transactions allowed per "
                "ledger (maximum_txn_in_ledger).");
        }

        setup.maximumTxnInLedger.emplace(max);
    }

    /* The math works as expected for any value up to and including
       MAXINT, but put a reasonable limit on this percentage so that
       the factor can't be configured to render escalation effectively
       moot. (There are other ways to do that, including
       minimum_txn_in_ledger_.)
    */
    set(setup.normalConsensusIncreasePercent, Keys::kNormalConsensusIncreasePercent, section);
    setup.normalConsensusIncreasePercent =
        std::clamp(setup.normalConsensusIncreasePercent, 0u, 1000u);

    /* If this percentage is outside of the 0-100 range, the results
       are nonsensical (uint overflows happen, so the limit grows
       instead of shrinking). 0 is not recommended.
    */
    set(setup.slowConsensusDecreasePercent, Keys::kSlowConsensusDecreasePercent, section);
    setup.slowConsensusDecreasePercent = std::clamp(setup.slowConsensusDecreasePercent, 0u, 100u);

    set(setup.maximumTxnPerAccount, Keys::kMaximumTxnPerAccount, section);
    set(setup.minimumLastLedgerBuffer, Keys::kMinimumLastLedgerBuffer, section);

    setup.standAlone = config.standalone();
    return setup;
}

}  // namespace xrpl
