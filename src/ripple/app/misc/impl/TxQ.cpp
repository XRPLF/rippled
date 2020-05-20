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

#include <ripple/app/ledger/OpenLedger.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/LoadFeeTrack.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/app/tx/apply.h>
#include <ripple/basics/mulDiv.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>
#include <ripple/protocol/st.h>
#include <boost/algorithm/clamp.hpp>
#include <algorithm>
#include <limits>
#include <numeric>

namespace ripple {

//////////////////////////////////////////////////////////////////////////

static FeeLevel64
getFeeLevelPaid(
    STTx const& tx,
    FeeLevel64 baseRefLevel,
    XRPAmount refTxnCostDrops,
    TxQ::Setup const& setup)
{
    if (refTxnCostDrops == 0)
        // If nothing is required, or the cost is 0,
        // the level is effectively infinite.
        return setup.zeroBaseFeeTransactionFeeLevel;

    // If the math overflows, return the clipped
    // result blindly. This is very unlikely to ever
    // happen.
    return mulDiv(tx[sfFee].xrp(), baseRefLevel, refTxnCostDrops).second;
}

static boost::optional<LedgerIndex>
getLastLedgerSequence(STTx const& tx)
{
    if (!tx.isFieldPresent(sfLastLedgerSequence))
        return boost::none;
    return tx.getFieldU32(sfLastLedgerSequence);
}

static FeeLevel64
increase(FeeLevel64 level, std::uint32_t increasePercent)
{
    return mulDiv(level, 100 + increasePercent, 100).second;
}

//////////////////////////////////////////////////////////////////////////

constexpr FeeLevel64 TxQ::baseLevel;

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
        auto const baseFee =
            view.fees().toDrops(calculateBaseFee(view, *tx.first)).second;
        feeLevels.push_back(
            getFeeLevelPaid(*tx.first, baseLevel, baseFee, setup));
    });
    std::sort(feeLevels.begin(), feeLevels.end());
    assert(size == feeLevels.size());

    JLOG(j_.debug()) << "Ledger " << view.info().seq << " has " << size
                     << " transactions. "
                     << "Ledgers are processing "
                     << (timeLeap ? "slowly" : "as expected")
                     << ". Expected transactions is currently " << txnsExpected_
                     << " and multiplier is " << escalationMultiplier_;

    if (timeLeap)
    {
        // Ledgers are taking to long to process,
        // so clamp down on limits.
        auto const cutPct = 100 - setup.slowConsensusDecreasePercent;
        // upperLimit must be >= minimumTxnCount_ or boost::clamp can give
        // unexpected results
        auto const upperLimit = std::max<std::uint64_t>(
            mulDiv(txnsExpected_, cutPct, 100).second, minimumTxnCount_);
        txnsExpected_ = boost::algorithm::clamp(
            mulDiv(size, cutPct, 100).second, minimumTxnCount_, upperLimit);
        recentTxnCounts_.clear();
    }
    else if (size > txnsExpected_ || size > targetTxnCount_)
    {
        recentTxnCounts_.push_back(
            mulDiv(size, 100 + setup.normalConsensusIncreasePercent, 100)
                .second);
        auto const iter =
            std::max_element(recentTxnCounts_.begin(), recentTxnCounts_.end());
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
            return (txnsExpected_ * 9 + *iter) / 10;
        }();
        // Ledgers are processing in a timely manner,
        // so keep the limit high, but don't let it
        // grow without bound.
        txnsExpected_ = std::min(next, maximumTxnCount_.value_or(next));
    }

    if (!size)
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
            (feeLevels[size / 2] + feeLevels[(size - 1) / 2] + FeeLevel64{1}) /
            2;
        escalationMultiplier_ =
            std::max(escalationMultiplier_, setup.minimumEscalationMultiplier);
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
        return mulDiv(multiplier, current * current, target * target).second;
    }

    return baseLevel;
}

namespace detail {

static std::pair<bool, std::uint64_t>
sumOfFirstSquares(std::size_t x)
{
    // sum(n = 1->x) : n * n = x(x + 1)(2x + 1) / 6

    // If x is anywhere on the order of 2^^21, it's going
    // to completely dominate the computation and is likely
    // enough to overflow that we're just going to assume
    // it does. If we have anywhere near 2^^21 transactions
    // in a ledger, this is the least of our problems.
    if (x >= (1 << 21))
        return std::make_pair(false, std::numeric_limits<std::uint64_t>::max());
    return std::make_pair(true, (x * (x + 1) * (2 * x + 1)) / 6);
}

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

    assert(current > target);

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
    auto const totalFeeLevel = mulDiv(
        multiplier, sumNlast.second - sumNcurrent.second, target * target);

    return totalFeeLevel;
}

TxQ::MaybeTx::MaybeTx(
    std::shared_ptr<STTx const> const& txn_,
    TxID const& txID_,
    FeeLevel64 feeLevel_,
    ApplyFlags const flags_,
    PreflightResult const& pfresult_)
    : txn(txn_)
    , feeLevel(feeLevel_)
    , txID(txID_)
    , account(txn_->getAccountID(sfAccount))
    , sequence(txn_->getSequence())
    , retriesRemaining(retriesAllowed)
    , flags(flags_)
    , pfresult(pfresult_)
{
    lastValid = getLastLedgerSequence(*txn);

    if (txn->isFieldPresent(sfAccountTxnID))
        priorTxID = txn->getFieldH256(sfAccountTxnID);
}

std::pair<TER, bool>
TxQ::MaybeTx::apply(Application& app, OpenView& view, beast::Journal j)
{
    // If the rules or flags change, preflight again
    assert(pfresult);
    if (pfresult->rules != view.rules() || pfresult->flags != flags)
    {
        JLOG(j.debug()) << "Queued transaction " << txID
                        << " rules or flags have changed. Flags from "
                        << pfresult->flags << " to " << flags;

        pfresult.emplace(
            preflight(app, view.rules(), pfresult->tx, flags, pfresult->j));
    }

    auto pcresult = preclaim(*pfresult, app, view);

    return doApply(pcresult, app, view);
}

TxQ::TxQAccount::TxQAccount(std::shared_ptr<STTx const> const& txn)
    : TxQAccount(txn->getAccountID(sfAccount))
{
}

TxQ::TxQAccount::TxQAccount(const AccountID& account_) : account(account_)
{
}

auto
TxQ::TxQAccount::add(MaybeTx&& txn) -> MaybeTx&
{
    auto sequence = txn.sequence;

    auto result = transactions.emplace(sequence, std::move(txn));
    assert(result.second);
    assert(&result.first->second != &txn);

    return result.first->second;
}

bool
TxQ::TxQAccount::remove(TxSeq const& sequence)
{
    return transactions.erase(sequence) != 0;
}

//////////////////////////////////////////////////////////////////////////

TxQ::TxQ(Setup const& setup, beast::Journal j)
    : setup_(setup), j_(j), feeMetrics_(setup, j), maxSize_(boost::none)
{
}

TxQ::~TxQ()
{
    byFee_.clear();
}

template <size_t fillPercentage>
bool
TxQ::isFull() const
{
    static_assert(
        fillPercentage > 0 && fillPercentage <= 100, "Invalid fill percentage");
    return maxSize_ && byFee_.size() >= (*maxSize_ * fillPercentage / 100);
}

bool
TxQ::canBeHeld(
    STTx const& tx,
    ApplyFlags const flags,
    OpenView const& view,
    AccountMap::iterator accountIter,
    boost::optional<FeeMultiSet::iterator> replacementIter)
{
    // PreviousTxnID is deprecated and should never be used
    // AccountTxnID is not supported by the transaction
    // queue yet, but should be added in the future
    // tapFAIL_HARD transactions are never held
    bool canBeHeld = !tx.isFieldPresent(sfPreviousTxnID) &&
        !tx.isFieldPresent(sfAccountTxnID) && !(flags & tapFAIL_HARD);
    if (canBeHeld)
    {
        /* To be queued and relayed, the transaction needs to
            promise to stick around for long enough that it has
            a realistic chance of getting into a ledger.
        */
        auto const lastValid = getLastLedgerSequence(tx);
        canBeHeld = !lastValid ||
            *lastValid >= view.info().seq + setup_.minimumLastLedgerBuffer;
    }
    if (canBeHeld)
    {
        /* Limit the number of transactions an individual account
            can queue. Mitigates the lost cost of relaying should
            an early one fail or get dropped.
        */

        // Allow if the account is not in the queue at all
        canBeHeld = accountIter == byAccount_.end();

        if (!canBeHeld)
        {
            // Allow this tx to replace another one
            canBeHeld = replacementIter.is_initialized();
        }

        if (!canBeHeld)
        {
            // Allow if there are fewer than the limit
            canBeHeld =
                accountIter->second.getTxnCount() < setup_.maximumTxnPerAccount;
        }

        if (!canBeHeld)
        {
            // Allow if the transaction goes in front of any
            // queued transactions. Enables recovery of open
            // ledger transactions, and stuck transactions.
            auto const tSeq = tx.getSequence();
            canBeHeld = tSeq < accountIter->second.transactions.rbegin()->first;
        }
    }
    return canBeHeld;
}

auto
TxQ::erase(TxQ::FeeMultiSet::const_iterator_type candidateIter)
    -> FeeMultiSet::iterator_type
{
    auto& txQAccount = byAccount_.at(candidateIter->account);
    auto const sequence = candidateIter->sequence;
    auto const newCandidateIter = byFee_.erase(candidateIter);
    // Now that the candidate has been removed from the
    // intrusive list remove it from the TxQAccount
    // so the memory can be freed.
    auto const found = txQAccount.remove(sequence);
    (void)found;
    assert(found);

    return newCandidateIter;
}

auto
TxQ::eraseAndAdvance(TxQ::FeeMultiSet::const_iterator_type candidateIter)
    -> FeeMultiSet::iterator_type
{
    auto& txQAccount = byAccount_.at(candidateIter->account);
    auto const accountIter =
        txQAccount.transactions.find(candidateIter->sequence);
    assert(accountIter != txQAccount.transactions.end());
    assert(accountIter == txQAccount.transactions.begin());
    assert(byFee_.iterator_to(accountIter->second) == candidateIter);
    auto const accountNextIter = std::next(accountIter);
    /* Check if the next transaction for this account has the
        next sequence number, and a higher fee level, which means
        we skipped it earlier, and need to try it again.
        Edge cases: If the next account tx has a lower fee level,
            it's going to be later in the fee queue, so we haven't
            skipped it yet.
            If the next tx has an equal fee level, it was either
            submitted later, so it's also going to be later in the
            fee queue, OR the current was resubmitted to bump up
            the fee level, and we have skipped that next tx. In
            the latter case, continue through the fee queue anyway
            to head off potential ordering manipulation problems.
    */
    auto const feeNextIter = std::next(candidateIter);
    bool const useAccountNext =
        accountNextIter != txQAccount.transactions.end() &&
        accountNextIter->first == candidateIter->sequence + 1 &&
        (feeNextIter == byFee_.end() ||
         accountNextIter->second.feeLevel > feeNextIter->feeLevel);
    auto const candidateNextIter = byFee_.erase(candidateIter);
    txQAccount.transactions.erase(accountIter);
    return useAccountNext ? byFee_.iterator_to(accountNextIter->second)
                          : candidateNextIter;
}

auto
TxQ::erase(
    TxQ::TxQAccount& txQAccount,
    TxQ::TxQAccount::TxMap::const_iterator begin,
    TxQ::TxQAccount::TxMap::const_iterator end) -> TxQAccount::TxMap::iterator
{
    for (auto it = begin; it != end; ++it)
    {
        byFee_.erase(byFee_.iterator_to(it->second));
    }
    return txQAccount.transactions.erase(begin, end);
}

std::pair<TER, bool>
TxQ::tryClearAccountQueue(
    Application& app,
    OpenView& view,
    STTx const& tx,
    TxQ::AccountMap::iterator const& accountIter,
    TxQAccount::TxMap::iterator beginTxIter,
    FeeLevel64 feeLevelPaid,
    PreflightResult const& pfresult,
    std::size_t const txExtraCount,
    ApplyFlags flags,
    FeeMetrics::Snapshot const& metricsSnapshot,
    beast::Journal j)
{
    auto const tSeq = tx.getSequence();
    assert(beginTxIter != accountIter->second.transactions.end());
    auto const aSeq = beginTxIter->first;

    auto const requiredTotalFeeLevel = FeeMetrics::escalatedSeriesFeeLevel(
        metricsSnapshot, view, txExtraCount, tSeq - aSeq + 1);
    /* If the computation for the total manages to overflow (however extremely
        unlikely), then there's no way we can confidently verify if the queue
        can be cleared.
    */
    if (!requiredTotalFeeLevel.first)
        return std::make_pair(telINSUF_FEE_P, false);

    // Unlike multiTx, this check is only concerned with the range
    // from [aSeq, tSeq)
    auto endTxIter = accountIter->second.transactions.lower_bound(tSeq);

    auto const totalFeeLevelPaid = std::accumulate(
        beginTxIter,
        endTxIter,
        feeLevelPaid,
        [](auto const& total, auto const& txn) {
            return total + txn.second.feeLevel;
        });

    // This transaction did not pay enough, so fall back to the normal process.
    if (totalFeeLevelPaid < requiredTotalFeeLevel.second)
        return std::make_pair(telINSUF_FEE_P, false);

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
        it->second.lastResult = txResult.first;
        if (!txResult.second)
        {
            // Transaction failed to apply. Fall back to the normal process.
            return std::make_pair(txResult.first, false);
        }
    }
    // Apply the current tx. Because the state of the view has been changed
    // by the queued txs, we also need to preclaim again.
    auto const txResult = doApply(preclaim(pfresult, app, view), app, view);

    if (txResult.second)
    {
        // All of the queued transactions applied, so remove them from the
        // queue.
        endTxIter = erase(accountIter->second, beginTxIter, endTxIter);
        // If `tx` is replacing a queued tx, delete that one, too.
        if (endTxIter != accountIter->second.transactions.end() &&
            endTxIter->first == tSeq)
            erase(accountIter->second, endTxIter, std::next(endTxIter));
    }

    return txResult;
}

/*
    How the decision to apply, queue, or reject is made:
    1. Does `preflight` indicate that the tx is valid?
        No: Return the `TER` from `preflight`. Stop.
        Yes: Continue to next step.
    2. Is there already a tx for the same account with the
            same sequence number in the queue?
        Yes: Is `txn`'s fee `retrySequencePercent` higher than the
                queued transaction's fee? And is this the last tx
                in the queue for that account, or are both txs
                non-blockers?
            Yes: Remove the queued transaction. Continue to next
                step.
            No: Reject `txn` with `telCAN_NOT_QUEUE_FEE`. Stop.
        No: Continue to next step.
    3. Does this tx have the expected sequence number for the
            account?
        Yes: Continue to next step.
        No: Are all the intervening sequence numbers also in the
                queue?
            No: Continue to the next step. (We expect the next
                step to return `terPRE_SEQ`, but won't short
                circuit that logic.)
            Yes: Is the fee more than `multiTxnPercent` higher
                    than the previous tx?
                No: Reject with `telINSUF_FEE_P`. Stop.
                Yes: Are any of the prior sequence txs blockers?
                    Yes: Reject with `telCAN_NOT_QUEUE_BLOCKED`. Stop.
                    No: Are the fees in-flight of the other
                            queued txs >= than the account
                            balance or minimum account reserve?
                        Yes: Reject with `telCAN_NOT_QUEUE_BALANCE`. Stop.
                        No: Create a throwaway sandbox `View`. Modify
                            the account's sequence number to match
                            the tx (avoid `terPRE_SEQ`), and decrease
                            the account balance by the total fees and
                            maximum spend of the other in-flight txs.
                            Continue to the next step.
    4. Does `preclaim` indicate that the account is likely to claim
            a fee (using the throwaway sandbox `View` created above,
            if appropriate)?
        No: Return the `TER` from `preclaim`. Stop.
        Yes: Continue to the next step.
    5. Did we create a throwaway sandbox `View`?
        Yes: Continue to the next step.
        No: Is the `txn`s fee level >= the required fee level?
            Yes: `txn` can be applied to the open ledger. Pass
                it to `doApply()` and return that result.
            No: Continue to the next step.
    6. Can the tx be held in the queue? (See TxQ::canBeHeld).
            No: Reject `txn` with `telCAN_NOT_QUEUE_FULL`
                if not. Stop.
            Yes: Continue to the next step.
    7. Is the queue full?
        No: Continue to the next step.
        Yes: Is the `txn`'s fee level higher than the end /
                lowest fee level item's fee level?
            Yes: Remove the end item. Continue to the next step.
            No: Reject `txn` with a low fee TER code.
    8. Put `txn` in the queue.
*/
std::pair<TER, bool>
TxQ::apply(
    Application& app,
    OpenView& view,
    std::shared_ptr<STTx const> const& tx,
    ApplyFlags flags,
    beast::Journal j)
{
    auto const account = (*tx)[sfAccount];
    auto const transactionID = tx->getTransactionID();
    auto const tSeq = tx->getSequence();
    // See if the transaction is valid, properly formed,
    // etc. before doing potentially expensive queue
    // replace and multi-transaction operations.
    auto const pfresult = preflight(app, view.rules(), *tx, flags, j);
    if (pfresult.ter != tesSUCCESS)
        return {pfresult.ter, false};

    struct MultiTxn
    {
        explicit MultiTxn() = default;

        boost::optional<ApplyViewImpl> applyView;
        boost::optional<OpenView> openView;

        TxQAccount::TxMap::iterator nextTxIter;

        XRPAmount fee = beast::zero;
        XRPAmount potentialSpend = beast::zero;
        bool includeCurrentFee = false;
    };

    boost::optional<MultiTxn> multiTxn;
    boost::optional<TxConsequences const> consequences;
    boost::optional<FeeMultiSet::iterator> replacedItemDeleteIter;

    std::lock_guard lock(mutex_);

    auto const metricsSnapshot = feeMetrics_.getSnapshot();

    // We may need the base fee for multiple transactions
    // or transaction replacement, so just pull it up now.
    // TODO: Do we want to avoid doing it again during
    //   preclaim?
    auto const baseFee =
        view.fees().toDrops(calculateBaseFee(view, *tx)).second;
    auto const feeLevelPaid = getFeeLevelPaid(*tx, baseLevel, baseFee, setup_);
    auto const requiredFeeLevel = [&]() {
        auto feeLevel = FeeMetrics::scaleFeeLevel(metricsSnapshot, view);
        if ((flags & tapPREFER_QUEUE) && byFee_.size())
        {
            return std::max(feeLevel, byFee_.begin()->feeLevel);
        }
        return feeLevel;
    }();

    auto accountIter = byAccount_.find(account);
    bool const accountExists = accountIter != byAccount_.end();

    // Is there a transaction for the same account with the
    // same sequence number already in the queue?
    if (accountExists)
    {
        auto& txQAcct = accountIter->second;
        auto existingIter = txQAcct.transactions.find(tSeq);
        if (existingIter != txQAcct.transactions.end())
        {
            // Is the current transaction's fee higher than
            // the queued transaction's fee + a percentage
            auto requiredRetryLevel = increase(
                existingIter->second.feeLevel, setup_.retrySequencePercent);
            JLOG(j_.trace())
                << "Found transaction in queue for account " << account
                << " with sequence number " << tSeq << " new txn fee level is "
                << feeLevelPaid << ", old txn fee level is "
                << existingIter->second.feeLevel
                << ", new txn needs fee level of " << requiredRetryLevel;
            if (feeLevelPaid > requiredRetryLevel ||
                (existingIter->second.feeLevel < requiredFeeLevel &&
                 feeLevelPaid >= requiredFeeLevel &&
                 existingIter == txQAcct.transactions.begin()))
            {
                /* Either the fee is high enough to retry or
                    the prior txn is the first for this account, and
                    could not get into the open ledger, but this one can.
                */

                /* A normal tx can't be replaced by a blocker, unless it's
                    the last tx in the queue for the account.
                */
                if (std::next(existingIter) != txQAcct.transactions.end())
                {
                    // Normally, only the last tx in the queue will have
                    // !consequences, but an expired transaction can be
                    // replaced, and that replacement won't have it set,
                    // and that's ok.
                    if (!existingIter->second.consequences)
                        existingIter->second.consequences.emplace(
                            calculateConsequences(
                                *existingIter->second.pfresult));

                    if (existingIter->second.consequences->category ==
                        TxConsequences::normal)
                    {
                        assert(!consequences);
                        consequences.emplace(calculateConsequences(pfresult));
                        if (consequences->category == TxConsequences::blocker)
                        {
                            // Can't replace a normal transaction in the
                            // middle of the queue with a blocker.
                            JLOG(j_.trace()) << "Ignoring blocker transaction "
                                             << transactionID
                                             << " in favor of normal queued "
                                             << existingIter->second.txID;
                            return {telCAN_NOT_QUEUE_BLOCKS, false};
                        }
                    }
                }

                // Remove the queued transaction and continue
                JLOG(j_.trace()) << "Removing transaction from queue "
                                 << existingIter->second.txID << " in favor of "
                                 << transactionID;
                // Then save the queued tx to remove from the queue if
                // the new tx succeeds or gets queued. DO NOT REMOVE
                // if the new tx fails, because there may be other txs
                // dependent on it in the queue.
                auto deleteIter = byFee_.iterator_to(existingIter->second);
                assert(deleteIter != byFee_.end());
                assert(&existingIter->second == &*deleteIter);
                assert(deleteIter->sequence == tSeq);
                assert(deleteIter->account == txQAcct.account);
                replacedItemDeleteIter = deleteIter;
            }
            else
            {
                // Drop the current transaction
                JLOG(j_.trace())
                    << "Ignoring transaction " << transactionID
                    << " in favor of queued " << existingIter->second.txID;
                return {telCAN_NOT_QUEUE_FEE, false};
            }
        }
    }

    // If there are other transactions in the queue
    // for this account, account for that before the pre-checks,
    // so we don't get a false terPRE_SEQ.
    if (accountExists)
    {
        if (auto const sle = view.read(keylet::account(account)); sle)
        {
            auto& txQAcct = accountIter->second;
            auto const aSeq = (*sle)[sfSequence];

            if (aSeq < tSeq)
            {
                // If the transaction is queueable, create the multiTxn
                // object to hold the info we need to adjust for
                // prior txns. Otherwise, let preclaim fail as if
                // we didn't have the queue at all.
                if (canBeHeld(
                        *tx, flags, view, accountIter, replacedItemDeleteIter))
                    multiTxn.emplace();
            }

            if (multiTxn)
            {
                /* See if the queue has entries for all the
                    seq's in [aSeq, tSeq). Total up all the
                    consequences while we're checking. If one
                    turns up missing or is a blocker, abort.
                */
                multiTxn->nextTxIter = txQAcct.transactions.find(aSeq);
                auto workingIter = multiTxn->nextTxIter;
                auto workingSeq = aSeq;
                for (; workingIter != txQAcct.transactions.end();
                     ++workingIter, ++workingSeq)
                {
                    if (workingSeq < tSeq && workingIter->first != workingSeq)
                    {
                        // If any transactions are missing before `tx`, abort.
                        multiTxn.reset();
                        break;
                    }
                    if (workingIter->first == tSeq - 1)
                    {
                        // Is the current transaction's fee higher than
                        // the previous transaction's fee + a percentage
                        auto requiredMultiLevel = increase(
                            workingIter->second.feeLevel,
                            setup_.multiTxnPercent);

                        if (feeLevelPaid <= requiredMultiLevel)
                        {
                            // Drop the current transaction
                            JLOG(j_.trace())
                                << "Ignoring transaction " << transactionID
                                << ". Needs fee level of " <<

                                requiredMultiLevel << ". Only paid "
                                << feeLevelPaid;
                            return {telINSUF_FEE_P, false};
                        }
                    }
                    if (workingIter->first == tSeq)
                    {
                        // If we're replacing this transaction, don't
                        // count it.
                        assert(replacedItemDeleteIter);
                        multiTxn->includeCurrentFee = std::next(workingIter) !=
                            txQAcct.transactions.end();
                        continue;
                    }
                    if (!workingIter->second.consequences)
                        workingIter->second.consequences.emplace(
                            calculateConsequences(
                                *workingIter->second.pfresult));
                    // Don't worry about the blocker status of txs
                    // after the current.
                    if (workingIter->first < tSeq &&
                        workingIter->second.consequences->category ==
                            TxConsequences::blocker)
                    {
                        // Drop the current transaction, because it's
                        // blocked by workingIter.
                        JLOG(j_.trace())
                            << "Ignoring transaction " << transactionID
                            << ". A blocker-type transaction "
                            << "is in the queue.";
                        return {telCAN_NOT_QUEUE_BLOCKED, false};
                    }
                    multiTxn->fee += workingIter->second.consequences->fee;
                    multiTxn->potentialSpend +=
                        workingIter->second.consequences->potentialSpend;
                }
                if (workingSeq < tSeq)
                    // Transactions are missing before `tx`.
                    multiTxn.reset();
            }

            if (multiTxn)
            {
                /* Check if the total fees in flight are greater
                    than the account's current balance, or the
                    minimum reserve. If it is, then there's a risk
                    that the fees won't get paid, so drop this
                    transaction with a telCAN_NOT_QUEUE_BALANCE result.
                    TODO: Decide whether to count the current txn fee
                        in this limit if it's the last transaction for
                        this account. Currently, it will not count,
                        for the same reason that it is not checked on
                        the first transaction.
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
                auto const balance = (*sle)[sfBalance].xrp();
                /* Get the minimum possible reserve. If fees exceed
                   this amount, the transaction can't be queued.
                   Considering that typical fees are several orders
                   of magnitude smaller than any current or expected
                   future reserve, this calculation is simpler than
                   trying to figure out the potential changes to
                   the ownerCount that may occur to the account
                   as a result of these transactions, and removes
                   any need to account for other transactions that
                   may affect the owner count while these are queued.
                */
                auto const reserve = view.fees().accountReserve(0);
                auto totalFee = multiTxn->fee;
                if (multiTxn->includeCurrentFee)
                    totalFee += (*tx)[sfFee].xrp();
                if (totalFee >= balance || totalFee >= reserve)
                {
                    // Drop the current transaction
                    JLOG(j_.trace()) << "Ignoring transaction " << transactionID
                                     << ". Total fees in flight too high.";
                    return {telCAN_NOT_QUEUE_BALANCE, false};
                }

                // Create the test view from the current view
                multiTxn->applyView.emplace(&view, flags);
                multiTxn->openView.emplace(&*multiTxn->applyView);

                auto const sleBump =
                    multiTxn->applyView->peek(keylet::account(account));
                if (!sleBump)
                    return {tefINTERNAL, false};

                auto const potentialTotalSpend = multiTxn->fee +
                    std::min(balance - std::min(balance, reserve),
                             multiTxn->potentialSpend);
                assert(potentialTotalSpend > XRPAmount{0});
                sleBump->setFieldAmount(
                    sfBalance, balance - potentialTotalSpend);
                sleBump->setFieldU32(sfSequence, tSeq);
            }
        }
    }

    // See if the transaction is likely to claim a fee.
    assert(!multiTxn || multiTxn->openView);
    auto const pcresult =
        preclaim(pfresult, app, multiTxn ? *multiTxn->openView : view);
    if (!pcresult.likelyToClaimFee)
        return {pcresult.ter, false};

    // Too low of a fee should get caught by preclaim
    assert(feeLevelPaid >= baseLevel);

    JLOG(j_.trace()) << "Transaction " << transactionID << " from account "
                     << account << " has fee level of " << feeLevelPaid
                     << " needs at least " << requiredFeeLevel
                     << " to get in the open ledger, which has "
                     << view.txCount() << " entries.";

    /* Quick heuristic check to see if it's worth checking that this
        tx has a high enough fee to clear all the txs in the queue.
        1) Transaction is trying to get into the open ledger
        2) Must be an account already in the queue.
        3) Must be have passed the multiTxn checks (tx is not the next
            account seq, the skipped seqs are in the queue, the reserve
            doesn't get exhausted, etc).
        4) The next transaction must not have previously tried and failed
            to apply to an open ledger.
        5) Tx must be paying more than just the required fee level to
            get itself into the queue.
        6) Fee level must be escalated above the default (if it's not,
            then the first tx _must_ have failed to process in `accept`
            for some other reason. Tx is allowed to queue in case
            conditions change, but don't waste the effort to clear).
        7) Tx is not a 0-fee / free transaction, regardless of fee level.
    */
    if (!(flags & tapPREFER_QUEUE) && accountExists &&
        multiTxn.is_initialized() &&
        multiTxn->nextTxIter->second.retriesRemaining ==
            MaybeTx::retriesAllowed &&
        feeLevelPaid > requiredFeeLevel && requiredFeeLevel > baseLevel &&
        baseFee != 0)
    {
        OpenView sandbox(open_ledger, &view, view.rules());

        auto result = tryClearAccountQueue(
            app,
            sandbox,
            *tx,
            accountIter,
            multiTxn->nextTxIter,
            feeLevelPaid,
            pfresult,
            view.txCount(),
            flags,
            metricsSnapshot,
            j);
        if (result.second)
        {
            sandbox.apply(view);
            /* Can't erase(*replacedItemDeleteIter) here because success
                implies that it has already been deleted.
            */
            return result;
        }
    }

    // Can transaction go in open ledger?
    if (!multiTxn && feeLevelPaid >= requiredFeeLevel)
    {
        // Transaction fee is sufficient to go in open ledger immediately

        JLOG(j_.trace()) << "Applying transaction " << transactionID
                         << " to open ledger.";

        auto const [txnResult, didApply] = doApply(pcresult, app, view);

        JLOG(j_.trace()) << "New transaction " << transactionID
                         << (didApply ? " applied successfully with "
                                      : " failed with ")
                         << transToken(txnResult);

        if (didApply && replacedItemDeleteIter)
            erase(*replacedItemDeleteIter);
        return {txnResult, didApply};
    }

    // If `multiTxn` has a value, then `canBeHeld` has already been verified
    if (!multiTxn &&
        !canBeHeld(*tx, flags, view, accountIter, replacedItemDeleteIter))
    {
        // Bail, transaction cannot be held
        JLOG(j_.trace()) << "Transaction " << transactionID
                         << " can not be held";
        return {telCAN_NOT_QUEUE, false};
    }

    // If the queue is full, decide whether to drop the current
    // transaction or the last transaction for the account with
    // the lowest fee.
    if (!replacedItemDeleteIter && isFull())
    {
        auto lastRIter = byFee_.rbegin();
        if (lastRIter->account == account)
        {
            JLOG(j_.warn())
                << "Queue is full, and transaction " << transactionID
                << " would kick a transaction from the same account ("
                << account << ") out of the queue.";
            return {telCAN_NOT_QUEUE_FULL, false};
        }
        auto const& endAccount = byAccount_.at(lastRIter->account);
        auto endEffectiveFeeLevel = [&]() {
            // Compute the average of all the txs for the endAccount,
            // but only if the last tx in the queue has a lower fee
            // level than this candidate tx.
            if (lastRIter->feeLevel > feeLevelPaid ||
                endAccount.transactions.size() == 1)
                return lastRIter->feeLevel;

            constexpr FeeLevel64 max{std::numeric_limits<std::uint64_t>::max()};
            auto endTotal = std::accumulate(
                endAccount.transactions.begin(),
                endAccount.transactions.end(),
                std::pair<FeeLevel64, FeeLevel64>(0, 0),
                [&](auto const& total, auto const& txn) {
                    // Check for overflow.
                    auto next =
                        txn.second.feeLevel / endAccount.transactions.size();
                    auto mod =
                        txn.second.feeLevel % endAccount.transactions.size();
                    if (total.first >= max - next || total.second >= max - mod)
                        return std::make_pair(max, FeeLevel64{0});
                    return std::make_pair(
                        total.first + next, total.second + mod);
                });
            return endTotal.first +
                endTotal.second / endAccount.transactions.size();
        }();
        if (feeLevelPaid > endEffectiveFeeLevel)
        {
            // The queue is full, and this transaction is more
            // valuable, so kick out the cheapest transaction.
            auto dropRIter = endAccount.transactions.rbegin();
            assert(dropRIter->second.account == lastRIter->account);
            JLOG(j_.warn())
                << "Removing last item of account " << lastRIter->account
                << " from queue with average fee of " << endEffectiveFeeLevel
                << " in favor of " << transactionID << " with fee of "
                << feeLevelPaid;
            erase(byFee_.iterator_to(dropRIter->second));
        }
        else
        {
            JLOG(j_.warn())
                << "Queue is full, and transaction " << transactionID
                << " fee is lower than end item's account average fee";
            return {telCAN_NOT_QUEUE_FULL, false};
        }
    }

    // Hold the transaction in the queue.
    if (replacedItemDeleteIter)
        erase(*replacedItemDeleteIter);
    if (!accountExists)
    {
        // Create a new TxQAccount object and add the byAccount lookup.
        bool created;
        std::tie(accountIter, created) =
            byAccount_.emplace(account, TxQAccount(tx));
        (void)created;
        assert(created);
    }
    // Modify the flags for use when coming out of the queue.
    // These changes _may_ cause an extra `preflight`, but as long as
    // the `HashRouter` still knows about the transaction, the signature
    // will not be checked again, so the cost should be minimal.

    // Don't allow soft failures, which can lead to retries
    flags &= ~tapRETRY;

    // Don't queue because we're already in the queue
    flags &= ~tapPREFER_QUEUE;

    auto& candidate = accountIter->second.add(
        {tx, transactionID, feeLevelPaid, flags, pfresult});
    /* Normally we defer figuring out the consequences until
        something later requires us to, but if we know the
        consequences now, save them for later.
    */
    if (consequences)
        candidate.consequences.emplace(*consequences);
    // Then index it into the byFee lookup.
    byFee_.insert(candidate);
    JLOG(j_.debug()) << "Added transaction " << candidate.txID
                     << " with result " << transToken(pfresult.ter) << " from "
                     << (accountExists ? "existing" : "new") << " account "
                     << candidate.account << " to queue."
                     << " Flags: " << flags;

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
    std::lock_guard lock(mutex_);

    feeMetrics_.update(app, view, timeLeap, setup_);
    auto const& snapshot = feeMetrics_.getSnapshot();

    auto ledgerSeq = view.info().seq;

    if (!timeLeap)
        maxSize_ = std::max(
            snapshot.txnsExpected * setup_.ledgersInQueue, setup_.queueSizeMin);

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
    for (auto txQAccountIter = byAccount_.begin();
         txQAccountIter != byAccount_.end();)
    {
        if (txQAccountIter->second.empty())
            txQAccountIter = byAccount_.erase(txQAccountIter);
        else
            ++txQAccountIter;
    }
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

    auto ledgerChanged = false;

    std::lock_guard lock(mutex_);

    auto const metricSnapshot = feeMetrics_.getSnapshot();

    for (auto candidateIter = byFee_.begin(); candidateIter != byFee_.end();)
    {
        auto& account = byAccount_.at(candidateIter->account);
        if (candidateIter->sequence > account.transactions.begin()->first)
        {
            // This is not the first transaction for this account, so skip it.
            // It can not succeed yet.
            JLOG(j_.trace())
                << "Skipping queued transaction " << candidateIter->txID
                << " from account " << candidateIter->account
                << " as it is not the first.";
            candidateIter++;
            continue;
        }
        auto const requiredFeeLevel =
            FeeMetrics::scaleFeeLevel(metricSnapshot, view);
        auto const feeLevelPaid = candidateIter->feeLevel;
        JLOG(j_.trace()) << "Queued transaction " << candidateIter->txID
                         << " from account " << candidateIter->account
                         << " has fee level of " << feeLevelPaid
                         << " needs at least " << requiredFeeLevel;
        if (feeLevelPaid >= requiredFeeLevel)
        {
            auto firstTxn = candidateIter->txn;

            JLOG(j_.trace()) << "Applying queued transaction "
                             << candidateIter->txID << " to open ledger.";

            auto const [txnResult, didApply] =
                candidateIter->apply(app, view, j_);

            if (didApply)
            {
                // Remove the candidate from the queue
                JLOG(j_.debug())
                    << "Queued transaction " << candidateIter->txID
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
                    account.retryPenalty = true;
                else
                    account.dropPenalty = true;
                JLOG(j_.debug()) << "Queued transaction " << candidateIter->txID
                                 << " failed with " << transToken(txnResult)
                                 << ". Remove from queue.";
                candidateIter = eraseAndAdvance(candidateIter);
            }
            else
            {
                JLOG(j_.debug()) << "Queued transaction " << candidateIter->txID
                                 << " failed with " << transToken(txnResult)
                                 << ". Leave in queue."
                                 << " Applied: " << didApply
                                 << ". Flags: " << candidateIter->flags;
                if (account.retryPenalty && candidateIter->retriesRemaining > 2)
                    candidateIter->retriesRemaining = 1;
                else
                    --candidateIter->retriesRemaining;
                candidateIter->lastResult = txnResult;
                if (account.dropPenalty && account.transactions.size() > 1 &&
                    isFull<95>())
                {
                    /* The queue is close to full, this account has multiple
                        txs queued, and this account has had a transaction
                        fail. Even though we're giving this transaction another
                        chance, chances are it won't recover. So we don't make
                        things worse, drop the _last_ transaction for this
                       account.
                    */
                    auto dropRIter = account.transactions.rbegin();
                    assert(dropRIter->second.account == candidateIter->account);
                    JLOG(j_.warn()) << "Queue is nearly full, and transaction "
                                    << candidateIter->txID << " failed with "
                                    << transToken(txnResult)
                                    << ". Removing last item of account "
                                    << account.account;
                    auto endIter = byFee_.iterator_to(dropRIter->second);
                    assert(endIter != candidateIter);
                    erase(endIter);
                }
                ++candidateIter;
            }
        }
        else
        {
            break;
        }
    }

    return ledgerChanged;
}

TxQ::Metrics
TxQ::getMetrics(OpenView const& view) const
{
    Metrics result;

    std::lock_guard lock(mutex_);

    auto const snapshot = feeMetrics_.getSnapshot();

    result.txCount = byFee_.size();
    result.txQMaxSize = maxSize_;
    result.txInLedger = view.txCount();
    result.txPerLedger = snapshot.txnsExpected;
    result.referenceFeeLevel = baseLevel;
    result.minProcessingFeeLevel =
        isFull() ? byFee_.rbegin()->feeLevel + FeeLevel64{1} : baseLevel;
    result.medFeeLevel = snapshot.escalationMultiplier;
    result.openLedgerFeeLevel = FeeMetrics::scaleFeeLevel(snapshot, view);

    return result;
}

TxQ::FeeAndSeq
TxQ::getTxRequiredFeeAndSeq(
    OpenView const& view,
    std::shared_ptr<STTx const> const& tx) const
{
    auto const account = (*tx)[sfAccount];

    std::lock_guard lock(mutex_);

    auto const snapshot = feeMetrics_.getSnapshot();
    auto const baseFee =
        view.fees().toDrops(calculateBaseFee(view, *tx)).second;
    auto const fee = FeeMetrics::scaleFeeLevel(snapshot, view);

    auto const accountSeq = [&view, &account]() -> std::uint32_t {
        auto const sle = view.read(keylet::account(account));
        if (sle)
            return (*sle)[sfSequence];
        return 0;
    }();

    auto availableSeq = accountSeq;

    if (auto iter{byAccount_.find(account)}; iter != byAccount_.end())
    {
        auto& txQAcct = iter->second;
        for (auto const& [seq, _] : txQAcct.transactions)
        {
            (void)_;
            if (seq >= availableSeq)
                availableSeq = seq + 1;
        }
    }

    return {mulDiv(fee, baseFee, baseLevel).second, accountSeq, availableSeq};
}

auto
TxQ::getAccountTxs(AccountID const& account, ReadView const& view) const
    -> std::map<TxSeq, AccountTxDetails const>
{
    std::lock_guard lock(mutex_);

    auto accountIter = byAccount_.find(account);
    if (accountIter == byAccount_.end() ||
        accountIter->second.transactions.empty())
        return {};

    std::map<TxSeq, AccountTxDetails const> result;

    for (auto const& tx : accountIter->second.transactions)
    {
        result.emplace(tx.first, [&] {
            AccountTxDetails resultTx;
            resultTx.feeLevel = tx.second.feeLevel;
            if (tx.second.lastValid)
                resultTx.lastValid.emplace(*tx.second.lastValid);
            if (tx.second.consequences)
                resultTx.consequences.emplace(*tx.second.consequences);
            return resultTx;
        }());
    }
    return result;
}

auto
TxQ::getTxs(ReadView const& view) const -> std::vector<TxDetails>
{
    std::lock_guard lock(mutex_);

    if (byFee_.empty())
        return {};

    std::vector<TxDetails> result;
    result.reserve(byFee_.size());

    for (auto const& tx : byFee_)
    {
        result.emplace_back([&] {
            TxDetails resultTx;
            resultTx.feeLevel = tx.feeLevel;
            if (tx.lastValid)
                resultTx.lastValid.emplace(*tx.lastValid);
            if (tx.consequences)
                resultTx.consequences.emplace(*tx.consequences);
            resultTx.account = tx.account;
            resultTx.txn = tx.txn;
            resultTx.retriesRemaining = tx.retriesRemaining;
            BOOST_ASSERT(tx.pfresult);
            resultTx.preflightResult = tx.pfresult->ter;
            if (tx.lastResult)
                resultTx.lastResult.emplace(*tx.lastResult);
            return resultTx;
        }());
    }
    return result;
}

Json::Value
TxQ::doRPC(Application& app) const
{
    auto const view = app.openLedger().current();
    if (!view)
    {
        BOOST_ASSERT(false);
        return {};
    }

    auto const metrics = getMetrics(*view);

    Json::Value ret(Json::objectValue);

    auto& levels = ret[jss::levels] = Json::objectValue;

    ret[jss::ledger_current_index] = view->info().seq;
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
    auto& drops = ret[jss::drops] = Json::Value();

    // Don't care about the overflow flags
    drops[jss::base_fee] =
        to_string(toDrops(metrics.referenceFeeLevel, baseFee).second);
    drops[jss::minimum_fee] =
        to_string(toDrops(metrics.minProcessingFeeLevel, baseFee).second);
    drops[jss::median_fee] =
        to_string(toDrops(metrics.medFeeLevel, baseFee).second);
    drops[jss::open_ledger_fee] = to_string(
        toDrops(metrics.openLedgerFeeLevel - FeeLevel64{1}, baseFee).second +
        1);

    return ret;
}

//////////////////////////////////////////////////////////////////////////

TxQ::Setup
setup_TxQ(Config const& config)
{
    TxQ::Setup setup;
    auto const& section = config.section("transaction_queue");
    set(setup.ledgersInQueue, "ledgers_in_queue", section);
    set(setup.queueSizeMin, "minimum_queue_size", section);
    set(setup.retrySequencePercent, "retry_sequence_percent", section);
    set(setup.multiTxnPercent, "multi_txn_percent", section);
    set(setup.minimumEscalationMultiplier,
        "minimum_escalation_multiplier",
        section);
    set(setup.minimumTxnInLedger, "minimum_txn_in_ledger", section);
    set(setup.minimumTxnInLedgerSA,
        "minimum_txn_in_ledger_standalone",
        section);
    set(setup.targetTxnInLedger, "target_txn_in_ledger", section);
    std::uint32_t max;
    if (set(max, "maximum_txn_in_ledger", section))
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
       minimum_txn_in_ledger.)
    */
    set(setup.normalConsensusIncreasePercent,
        "normal_consensus_increase_percent",
        section);
    setup.normalConsensusIncreasePercent =
        boost::algorithm::clamp(setup.normalConsensusIncreasePercent, 0, 1000);

    /* If this percentage is outside of the 0-100 range, the results
       are nonsensical (uint overflows happen, so the limit grows
       instead of shrinking). 0 is not recommended.
    */
    set(setup.slowConsensusDecreasePercent,
        "slow_consensus_decrease_percent",
        section);
    setup.slowConsensusDecreasePercent =
        boost::algorithm::clamp(setup.slowConsensusDecreasePercent, 0, 100);

    set(setup.maximumTxnPerAccount, "maximum_txn_per_account", section);
    set(setup.minimumLastLedgerBuffer, "minimum_last_ledger_buffer", section);
    set(setup.zeroBaseFeeTransactionFeeLevel,
        "zero_basefee_transaction_feelevel",
        section);

    setup.standAlone = config.standalone();
    return setup;
}

}  // namespace ripple
