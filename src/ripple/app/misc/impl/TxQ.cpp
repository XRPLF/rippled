//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013, 2019 Ripple Labs Inc.

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
getFeeLevelPaid(ReadView const& view, STTx const& tx)
{
    auto const [baseFee, effectiveFeePaid] = [&view, &tx]() {
        XRPAmount baseFee = view.fees().toDrops(calculateBaseFee(view, tx));
        XRPAmount feePaid = tx[sfFee].xrp();

        // If baseFee is 0 then the cost of a basic transaction is free.
        XRPAmount const ref = baseFee.signum() > 0
            ? XRPAmount{0}
            : calculateDefaultBaseFee(view, tx);
        return std::pair{baseFee + ref, feePaid + ref};
    }();

    assert(baseFee.signum() > 0);
    if (effectiveFeePaid.signum() <= 0 || baseFee.signum() <= 0)
    {
        return FeeLevel64(0);
    }

    if (std::pair<bool, FeeLevel64> const feeLevelPaid =
            mulDiv(effectiveFeePaid, TxQ::baseLevel, baseFee);
        feeLevelPaid.first)
        return feeLevelPaid.second;

    return FeeLevel64(std::numeric_limits<std::uint64_t>::max());
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
        feeLevels.push_back(getFeeLevelPaid(view, *tx.first));
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

constexpr static std::pair<bool, std::uint64_t>
sumOfFirstSquares(std::size_t xIn)
{
    // sum(n = 1->x) : n * n = x(x + 1)(2x + 1) / 6

    // We expect that size_t == std::uint64_t but, just in case, guarantee
    // we lose no bits.
    std::uint64_t x{xIn};

    // If x is anywhere on the order of 2^^21, it's going
    // to completely dominate the computation and is likely
    // enough to overflow that we're just going to assume
    // it does. If we have anywhere near 2^^21 transactions
    // in a ledger, this is the least of our problems.
    if (x >= (1 << 21))
        return {false, std::numeric_limits<std::uint64_t>::max()};
    return {true, (x * (x + 1) * (2 * x + 1)) / 6};
}

// Unit tests for sumOfSquares()
static_assert(sumOfFirstSquares(1).first == true);
static_assert(sumOfFirstSquares(1).second == 1);

static_assert(sumOfFirstSquares(2).first == true);
static_assert(sumOfFirstSquares(2).second == 5);

static_assert(sumOfFirstSquares(0x1FFFFF).first == true, "");
static_assert(sumOfFirstSquares(0x1FFFFF).second == 0x2AAAA8AAAAB00000ul, "");

static_assert(sumOfFirstSquares(0x200000).first == false, "");
static_assert(
    sumOfFirstSquares(0x200000).second ==
        std::numeric_limits<std::uint64_t>::max(),
    "");

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
    , lastValid(getLastLedgerSequence(*txn_))
    , seqProxy(txn_->getSeqProxy())
    , retriesRemaining(retriesAllowed)
    , flags(flags_)
    , pfresult(pfresult_)
{
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

    auto result = transactions.emplace(seqProx, std::move(txn));
    assert(result.second);
    assert(&result.first->second != &txn);

    return result.first->second;
}

bool
TxQ::TxQAccount::remove(SeqProxy seqProx)
{
    return transactions.erase(seqProx) != 0;
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

TER
TxQ::canBeHeld(
    STTx const& tx,
    ApplyFlags const flags,
    OpenView const& view,
    std::shared_ptr<SLE const> const& sleAccount,
    AccountMap::iterator const& accountIter,
    boost::optional<TxQAccount::TxMap::iterator> const& replacementIter,
    std::lock_guard<std::mutex> const& lock)
{
    // PreviousTxnID is deprecated and should never be used.
    // AccountTxnID is not supported by the transaction
    // queue yet, but should be added in the future.
    // tapFAIL_HARD transactions are never held
    if (tx.isFieldPresent(sfPreviousTxnID) ||
        tx.isFieldPresent(sfAccountTxnID) || (flags & tapFAIL_HARD))
        return telCAN_NOT_QUEUE;

    {
        // To be queued and relayed, the transaction needs to
        // promise to stick around for long enough that it has
        // a realistic chance of getting into a ledger.
        auto const lastValid = getLastLedgerSequence(tx);
        if (lastValid &&
            *lastValid < view.info().seq + setup_.minimumLastLedgerBuffer)
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
        // Tickets always follow sequence-based transactions, so a ticket
        // cannot unblock a sequence-based transaction.
        return telCAN_NOT_QUEUE_FULL;

    // This is the next queuable sequence-based SeqProxy for the account.
    SeqProxy const nextQueuable = nextQueuableSeqImpl(sleAccount, lock);
    if (txSeqProx != nextQueuable)
        // The provided transaction does not fill the next open sequence gap.
        return telCAN_NOT_QUEUE_FULL;

    // Make sure they are not just topping off the account's queued
    // sequence-based transactions.
    if (auto const nextTxIter = txQAcct.transactions.upper_bound(nextQueuable);
        nextTxIter != txQAcct.transactions.end() && nextTxIter->first.isSeq())
        // There is a next transaction and it is sequence based.  They are
        // filling a real gap.  Allow it.
        return tesSUCCESS;

    return telCAN_NOT_QUEUE_FULL;
}

auto
TxQ::erase(TxQ::FeeMultiSet::const_iterator_type candidateIter)
    -> FeeMultiSet::iterator_type
{
    auto& txQAccount = byAccount_.at(candidateIter->account);
    auto const seqProx = candidateIter->seqProxy;
    auto const newCandidateIter = byFee_.erase(candidateIter);
    // Now that the candidate has been removed from the
    // intrusive list remove it from the TxQAccount
    // so the memory can be freed.
    auto const found = txQAccount.remove(seqProx);
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
        txQAccount.transactions.find(candidateIter->seqProxy);
    assert(accountIter != txQAccount.transactions.end());

    // Note that sequence-based transactions must be applied in sequence order
    // from smallest to largest.  But ticket-based transactions can be
    // applied in any order.
    assert(
        candidateIter->seqProxy.isTicket() ||
        accountIter == txQAccount.transactions.begin());
    assert(byFee_.iterator_to(accountIter->second) == candidateIter);
    auto const accountNextIter = std::next(accountIter);

    // Check if the next transaction for this account has a greater
    // SeqProxy, and a higher fee level, which means we skipped it
    // earlier, and need to try it again.
    //
    // Edge cases:
    //  o If the next account tx has a lower fee level, it's going to be
    //    later in the fee queue, so we haven't skipped it yet.
    //
    //  o If the next tx has an equal fee level, it was...
    //
    //     * EITHER submitted later, so it's also going to be later in the
    //       fee queue,
    //
    //     * OR the current was resubmitted to bump up the fee level, and
    //       we have skipped that next tx.
    //
    //    In the latter case, continue through the fee queue anyway
    //    to head off potential ordering manipulation problems.
    auto const feeNextIter = std::next(candidateIter);
    bool const useAccountNext =
        accountNextIter != txQAccount.transactions.end() &&
        accountNextIter->first > candidateIter->seqProxy &&
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
TxQ::tryClearAccountQueueUpThruTx(
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
    SeqProxy const tSeqProx{tx.getSeqProxy()};
    assert(beginTxIter != accountIter->second.transactions.end());

    // This check is only concerned with the range from
    // [aSeqProxy, tSeqProxy)
    auto endTxIter = accountIter->second.transactions.lower_bound(tSeqProx);
    auto const dist = std::distance(beginTxIter, endTxIter);

    auto const requiredTotalFeeLevel = FeeMetrics::escalatedSeriesFeeLevel(
        metricsSnapshot, view, txExtraCount, dist + 1);
    // If the computation for the total manages to overflow (however extremely
    //    unlikely), then there's no way we can confidently verify if the queue
    //    can be cleared.
    if (!requiredTotalFeeLevel.first)
        return {telINSUF_FEE_P, false};

    auto const totalFeeLevelPaid = std::accumulate(
        beginTxIter,
        endTxIter,
        feeLevelPaid,
        [](auto const& total, auto const& txn) {
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
        it->second.lastResult = txResult.first;

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
        if (txResult.first == tefNO_TICKET)
            continue;

        if (!txResult.second)
        {
            // Transaction failed to apply. Fall back to the normal process.
            return {txResult.first, false};
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
            endTxIter->first == tSeqProx)
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
std::pair<TER, bool>
TxQ::apply(
    Application& app,
    OpenView& view,
    std::shared_ptr<STTx const> const& tx,
    ApplyFlags flags,
    beast::Journal j)
{
    // See if the transaction paid a high enough fee that it can go straight
    // into the ledger.
    if (auto directApplied = tryDirectApply(app, view, tx, flags, j))
        return *directApplied;

    // If we get past tryDirectApply() without returning then we expect
    // one of the following to occur:
    //
    //  o We will decide the transaction is unlikely to claim a fee.
    //  o The transaction paid a high enough fee that fee averaging will apply.
    //  o The transaction will be queued.

    // See if the transaction is valid, properly formed,
    // etc. before doing potentially expensive queue
    // replace and multi-transaction operations.
    auto const pfresult = preflight(app, view.rules(), *tx, flags, j);
    if (pfresult.ter != tesSUCCESS)
        return {pfresult.ter, false};

    // If the account is not currently in the ledger, don't queue its tx.
    auto const account = (*tx)[sfAccount];
    Keylet const accountKey{keylet::account(account)};
    auto const sleAccount = view.read(accountKey);
    if (!sleAccount)
        return {terNO_ACCOUNT, false};

    // If the transaction needs a Ticket is that Ticket in the ledger?
    SeqProxy const acctSeqProx = SeqProxy::sequence((*sleAccount)[sfSequence]);
    SeqProxy const txSeqProx = tx->getSeqProxy();
    if (txSeqProx.isTicket() &&
        !view.exists(keylet::ticket(account, txSeqProx)))
    {
        if (txSeqProx.value() < acctSeqProx.value())
            // The ticket number is low enough that it should already be
            // in the ledger if it were ever going to exist.
            return {tefNO_TICKET, false};

        // We don't queue transactions that use Tickets unless
        // we can find the Ticket in the ledger.
        return {terPRE_TICKET, false};
    }

    std::lock_guard lock(mutex_);

    // accountIter is not const because it may be updated further down.
    AccountMap::iterator accountIter = byAccount_.find(account);
    bool const accountIsInQueue = accountIter != byAccount_.end();

    // _If_ the account is in the queue, then ignore any sequence-based
    // queued transactions that slipped into the ledger while we were not
    // watching.  This does actually happen in the wild, but it's uncommon.
    //
    // Note that we _don't_ ignore queued ticket-based transactions that
    // slipped into the ledger while we were not watching.  It would be
    // desirable to do so, but the measured cost was too high since we have
    // to individually check each queued ticket against the ledger.
    struct TxIter
    {
        TxIter(
            TxQAccount::TxMap::iterator first_,
            TxQAccount::TxMap::iterator end_)
            : first(first_), end(end_)
        {
        }

        TxQAccount::TxMap::iterator first;
        TxQAccount::TxMap::iterator end;
    };

    boost::optional<TxIter> const txIter =
        [accountIter,
         accountIsInQueue,
         acctSeqProx]() -> boost::optional<TxIter> {
        if (!accountIsInQueue)
            return {};

        // Find the first transaction in the queue that we might apply.
        TxQAccount::TxMap& acctTxs = accountIter->second.transactions;
        TxQAccount::TxMap::iterator const firstIter =
            acctTxs.lower_bound(acctSeqProx);

        if (firstIter == acctTxs.end())
            // Even though there may be transactions in the queue, there are
            // none that we should pay attention to.
            return {};

        return {TxIter{firstIter, acctTxs.end()}};
    }();

    auto const acctTxCount{
        !txIter ? 0 : std::distance(txIter->first, txIter->end)};

    // Is tx a blocker?  If so there are very limited conditions when it
    // is allowed in the TxQ:
    //  1. If the account's queue is empty or
    //  2. If the blocker replaces the only entry in the account's queue.
    auto const transactionID = tx->getTransactionID();
    if (pfresult.consequences.isBlocker())
    {
        if (acctTxCount > 1)
        {
            // A blocker may not be co-resident with other transactions in
            // the account's queue.
            JLOG(j_.trace())
                << "Rejecting blocker transaction " << transactionID
                << ".  Account has other queued transactions.";
            return {telCAN_NOT_QUEUE_BLOCKS, false};
        }
        if (acctTxCount == 1 && (txSeqProx != txIter->first->first))
        {
            // The blocker is not replacing the lone queued transaction.
            JLOG(j_.trace())
                << "Rejecting blocker transaction " << transactionID
                << ".  Blocker does not replace lone queued transaction.";
            return {telCAN_NOT_QUEUE_BLOCKS, false};
        }
    }

    // If the transaction is intending to replace a transaction in the queue
    // identify the one that might be replaced.
    auto replacedTxIter = [accountIsInQueue, &accountIter, txSeqProx]()
        -> boost::optional<TxQAccount::TxMap::iterator> {
        if (accountIsInQueue)
        {
            TxQAccount& txQAcct = accountIter->second;
            if (auto const existingIter = txQAcct.transactions.find(txSeqProx);
                existingIter != txQAcct.transactions.end())
                return existingIter;
        }
        return {};
    }();

    // We may need the base fee for multiple transactions or transaction
    // replacement, so just pull it up now.
    auto const metricsSnapshot = feeMetrics_.getSnapshot();
    auto const feeLevelPaid = getFeeLevelPaid(view, *tx);
    auto const requiredFeeLevel =
        getRequiredFeeLevel(view, flags, metricsSnapshot, lock);

    // Is there a blocker already in the account's queue?  If so, don't
    // allow additional transactions in the queue.
    if (acctTxCount > 0)
    {
        // Allow tx to replace a blocker.  Otherwise, if there's a
        // blocker, we can't queue tx.
        //
        // We only need to check if txIter->first is a blocker because we
        // require that a blocker be alone in the account's queue.
        if (acctTxCount == 1 &&
            txIter->first->second.consequences().isBlocker() &&
            (txIter->first->first != txSeqProx))
        {
            return {telCAN_NOT_QUEUE_BLOCKED, false};
        }

        // Is there a transaction for the same account with the same
        // SeqProxy already in the queue?  If so we may replace the
        // existing entry with this new transaction.
        if (replacedTxIter)
        {
            // We are attempting to replace a transaction in the queue.
            //
            // Is the current transaction's fee higher than
            // the queued transaction's fee + a percentage
            TxQAccount::TxMap::iterator const& existingIter = *replacedTxIter;
            auto requiredRetryLevel = increase(
                existingIter->second.feeLevel, setup_.retrySequencePercent);
            JLOG(j_.trace())
                << "Found transaction in queue for account " << account
                << " with " << txSeqProx << " new txn fee level is "
                << feeLevelPaid << ", old txn fee level is "
                << existingIter->second.feeLevel
                << ", new txn needs fee level of " << requiredRetryLevel;
            if (feeLevelPaid > requiredRetryLevel)
            {
                // Continue, leaving the queued transaction marked for removal.
                // DO NOT REMOVE if the new tx fails, because there may
                // be other txs dependent on it in the queue.
                JLOG(j_.trace()) << "Removing transaction from queue "
                                 << existingIter->second.txID << " in favor of "
                                 << transactionID;
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

    struct MultiTxn
    {
        ApplyViewImpl applyView;
        OpenView openView;

        MultiTxn(OpenView& view, ApplyFlags flags)
            : applyView(&view, flags), openView(&applyView)
        {
        }
    };

    boost::optional<MultiTxn> multiTxn;

    if (acctTxCount == 0)
    {
        // There are no queued transactions for this account.  If the
        // transaction has a sequence make sure it's valid (tickets
        // are checked elsewhere).
        if (txSeqProx.isSeq())
        {
            if (acctSeqProx > txSeqProx)
                return {tefPAST_SEQ, false};
            if (acctSeqProx < txSeqProx)
                return {terPRE_SEQ, false};
        }
    }
    else
    {
        // There are probably other transactions in the queue for this
        // account.  Make sure the new transaction can work with the others
        // in the queue.
        TxQAccount const& txQAcct = accountIter->second;

        if (acctSeqProx > txSeqProx)
            return {tefPAST_SEQ, false};

        // Determine if we need a multiTxn object.  Assuming the account
        // is in the queue, there are two situations where we need to
        // build multiTx:
        //  1. If there are two or more transactions in the account's queue, or
        //  2. If the account has a single queue entry, we may still need
        //     multiTxn, but only if that lone entry will not be replaced by tx.
        bool requiresMultiTxn = false;
        if (acctTxCount > 1 || !replacedTxIter)
        {
            // If the transaction is queueable, create the multiTxn
            // object to hold the info we need to adjust for prior txns.
            TER const ter{canBeHeld(
                *tx,
                flags,
                view,
                sleAccount,
                accountIter,
                replacedTxIter,
                lock)};
            if (!isTesSuccess(ter))
                return {ter, false};

            requiresMultiTxn = true;
        }

        if (requiresMultiTxn)
        {
            // See if adding this entry to the queue makes sense.
            //
            //  o Transactions with sequences should start with the
            //    account's Sequence.
            //
            //  o Additional transactions with Sequences should
            //    follow preceding sequence-based transactions with no
            //    gaps (except for those required by CreateTicket
            //    transactions).

            // Find the entry in the queue that precedes the new
            // transaction, if one does.
            TxQAccount::TxMap::const_iterator const prevIter =
                txQAcct.getPrevTx(txSeqProx);

            // Does the new transaction go to the front of the queue?
            // This can happen if:
            //  o A transaction in the queue with a Sequence expired, or
            //  o The current first thing in the queue has a Ticket and
            //    * The tx has a Ticket that precedes it or
            //    * txSeqProx == acctSeqProx.
            assert(prevIter != txIter->end);
            if (prevIter == txIter->end || txSeqProx < prevIter->first)
            {
                // The first Sequence number in the queue must be the
                // account's sequence.
                if (txSeqProx.isSeq())
                {
                    if (txSeqProx < acctSeqProx)
                        return {tefPAST_SEQ, false};
                    else if (txSeqProx > acctSeqProx)
                        return {terPRE_SEQ, false};
                }
            }
            else if (!replacedTxIter)
            {
                // The current transaction is not replacing a transaction
                // in the queue.  So apparently there's a transaction in
                // front of this one in the queue.  Make sure the current
                // transaction fits in proper sequence order with the
                // previous transaction or is a ticket.
                if (txSeqProx.isSeq() &&
                    nextQueuableSeqImpl(sleAccount, lock) != txSeqProx)
                    return {telCAN_NOT_QUEUE, false};
            }

            // Sum fees and spending for all of the queued transactions
            // so we know how much to remove from the account balance
            // for the trial preclaim.
            XRPAmount potentialSpend = beast::zero;
            XRPAmount totalFee = beast::zero;
            for (auto iter = txIter->first; iter != txIter->end; ++iter)
            {
                // If we're replacing this transaction don't include
                // the replaced transaction's XRP spend.  Otherwise add
                // it to potentialSpend.
                if (iter->first != txSeqProx)
                {
                    totalFee += iter->second.consequences().fee();
                    potentialSpend +=
                        iter->second.consequences().potentialSpend();
                }
                else if (std::next(iter) != txIter->end)
                {
                    // The fee for the candidate transaction _should_ be
                    // counted if it's replacing a transaction in the middle
                    // of the queue.
                    totalFee += pfresult.consequences.fee();
                    potentialSpend += pfresult.consequences.potentialSpend();
                }
            }

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
            auto const balance = (*sleAccount)[sfBalance].xrp();
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
            if (totalFee >= balance || totalFee >= reserve)
            {
                // Drop the current transaction
                JLOG(j_.trace()) << "Ignoring transaction " << transactionID
                                 << ". Total fees in flight too high.";
                return {telCAN_NOT_QUEUE_BALANCE, false};
            }

            // Create the test view from the current view.
            multiTxn.emplace(view, flags);

            auto const sleBump = multiTxn->applyView.peek(accountKey);
            if (!sleBump)
                return {tefINTERNAL, false};

            // Subtract the fees and XRP spend from all of the other
            // transactions in the queue.  That prevents a transaction
            // inserted in the middle from fouling up later transactions.
            auto const potentialTotalSpend = totalFee +
                std::min(balance - std::min(balance, reserve), potentialSpend);
            assert(potentialTotalSpend > XRPAmount{0});
            sleBump->setFieldAmount(sfBalance, balance - potentialTotalSpend);
        }
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
    auto const pcresult = ForTxQ::preclaimWithoutSeqCheck(
        pfresult, app, multiTxn ? multiTxn->openView : view);
    if (!pcresult.likelyToClaimFee)
        return {pcresult.ter, false};

    // Too low of a fee should get caught by preclaim
    assert(feeLevelPaid >= baseLevel);

    JLOG(j_.trace()) << "Transaction " << transactionID << " from account "
                     << account << " has fee level of " << feeLevelPaid
                     << " needs at least " << requiredFeeLevel
                     << " to get in the open ledger, which has "
                     << view.txCount() << " entries.";

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
    if (!(flags & tapPREFER_QUEUE) && txSeqProx.isSeq() && txIter &&
        multiTxn.is_initialized() &&
        txIter->first->second.retriesRemaining == MaybeTx::retriesAllowed &&
        feeLevelPaid > requiredFeeLevel && requiredFeeLevel > baseLevel)
    {
        OpenView sandbox(open_ledger, &view, view.rules());

        auto result = tryClearAccountQueueUpThruTx(
            app,
            sandbox,
            *tx,
            accountIter,
            txIter->first,
            feeLevelPaid,
            pfresult,
            view.txCount(),
            flags,
            metricsSnapshot,
            j);
        if (result.second)
        {
            sandbox.apply(view);
            /* Can't erase (*replacedTxIter) here because success
                implies that it has already been deleted.
            */
            return result;
        }
    }

    // If `multiTxn` has a value, then `canBeHeld` has already been verified
    if (!multiTxn)
    {
        TER const ter{canBeHeld(
            *tx, flags, view, sleAccount, accountIter, replacedTxIter, lock)};
        if (!isTesSuccess(ter))
        {
            // Bail, transaction cannot be held
            JLOG(j_.trace())
                << "Transaction " << transactionID << " cannot be held";
            return {ter, false};
        }
    }

    // If the queue is full, decide whether to drop the current
    // transaction or the last transaction for the account with
    // the lowest fee.
    if (!replacedTxIter && isFull())
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
                [&](auto const& total,
                    auto const& txn) -> std::pair<FeeLevel64, FeeLevel64> {
                    // Check for overflow.
                    auto next =
                        txn.second.feeLevel / endAccount.transactions.size();
                    auto mod =
                        txn.second.feeLevel % endAccount.transactions.size();
                    if (total.first >= max - next || total.second >= max - mod)
                        return {max, FeeLevel64{0}};

                    return {total.first + next, total.second + mod};
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
    if (replacedTxIter)
    {
        replacedTxIter = removeFromByFee(replacedTxIter, tx);
    }

    if (!accountIsInQueue)
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

    // Then index it into the byFee lookup.
    byFee_.insert(candidate);
    JLOG(j_.debug()) << "Added transaction " << candidate.txID
                     << " with result " << transToken(pfresult.ter) << " from "
                     << (accountIsInQueue ? "existing" : "new") << " account "
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

    auto const metricsSnapshot = feeMetrics_.getSnapshot();

    for (auto candidateIter = byFee_.begin(); candidateIter != byFee_.end();)
    {
        auto& account = byAccount_.at(candidateIter->account);
        auto const beginIter = account.transactions.begin();
        if (candidateIter->seqProxy.isSeq() &&
            candidateIter->seqProxy > beginIter->first)
        {
            // There is a sequence transaction at the front of the queue and
            // candidate has a later sequence, so skip this candidate.  We
            // need to process sequence-based transactions in sequence order.
            JLOG(j_.trace())
                << "Skipping queued transaction " << candidateIter->txID
                << " from account " << candidateIter->account
                << " as it is not the first.";
            candidateIter++;
            continue;
        }
        auto const requiredFeeLevel =
            getRequiredFeeLevel(view, tapNONE, metricsSnapshot, lock);
        auto const feeLevelPaid = candidateIter->feeLevel;
        JLOG(j_.trace()) << "Queued transaction " << candidateIter->txID
                         << " from account " << candidateIter->account
                         << " has fee level of " << feeLevelPaid
                         << " needs at least " << requiredFeeLevel;
        if (feeLevelPaid >= requiredFeeLevel)
        {
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
                    // The queue is close to full, this account has multiple
                    // txs queued, and this account has had a transaction
                    // fail.
                    if (candidateIter->seqProxy.isTicket())
                    {
                        // Since the failed transaction has a ticket, order
                        // doesn't matter.  Drop this one.
                        JLOG(j_.warn())
                            << "Queue is nearly full, and transaction "
                            << candidateIter->txID << " failed with "
                            << transToken(txnResult)
                            << ". Removing ticketed tx from account "
                            << account.account;
                        candidateIter = eraseAndAdvance(candidateIter);
                    }
                    else
                    {
                        // Even though we're giving this transaction another
                        // chance, chances are it won't recover. To avoid
                        // making things worse, drop the _last_ transaction for
                        // this account.
                        auto dropRIter = account.transactions.rbegin();
                        assert(
                            dropRIter->second.account ==
                            candidateIter->account);

                        JLOG(j_.warn())
                            << "Queue is nearly full, and transaction "
                            << candidateIter->txID << " failed with "
                            << transToken(txnResult)
                            << ". Removing last item from account "
                            << account.account;
                        auto endIter = byFee_.iterator_to(dropRIter->second);
                        if (endIter != candidateIter)
                            erase(endIter);
                        ++candidateIter;
                    }
                }
                else
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

// Public entry point for nextQueuableSeq().
//
// Acquires a lock and calls the implementation.
SeqProxy
TxQ::nextQueuableSeq(std::shared_ptr<SLE const> const& sleAccount) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return nextQueuableSeqImpl(sleAccount, lock);
}

// The goal is to return a SeqProxy for a sequence that will fill the next
// available hole in the queue for the passed in account.
//
// If there are queued transactions for the account then the first viable
// sequence number, that is not used by a transaction in the queue, must
// be found and returned.
SeqProxy
TxQ::nextQueuableSeqImpl(
    std::shared_ptr<SLE const> const& sleAccount,
    std::lock_guard<std::mutex> const&) const
{
    // If the account is not in the ledger or a non-account was passed
    // then return zero.  We have no idea.
    if (!sleAccount || sleAccount->getType() != ltACCOUNT_ROOT)
        return SeqProxy::sequence(0);

    SeqProxy const acctSeqProx = SeqProxy::sequence((*sleAccount)[sfSequence]);

    // If the account is not in the queue then acctSeqProx is good enough.
    auto const accountIter = byAccount_.find((*sleAccount)[sfAccount]);
    if (accountIter == byAccount_.end() ||
        accountIter->second.transactions.empty())
        return acctSeqProx;

    TxQAccount::TxMap const& acctTxs = accountIter->second.transactions;

    // Ignore any sequence-based queued transactions that slipped into the
    // ledger while we were not watching.  This does actually happen in the
    // wild, but it's uncommon.
    TxQAccount::TxMap::const_iterator txIter = acctTxs.lower_bound(acctSeqProx);

    if (txIter == acctTxs.end() || !txIter->first.isSeq() ||
        txIter->first != acctSeqProx)
        // Either...
        //   o There are no queued sequence-based transactions equal to or
        //     following acctSeqProx or
        //   o acctSeqProx is not currently in the queue.
        // So acctSeqProx is as good as it gets.
        return acctSeqProx;

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
    std::lock_guard<std::mutex> const& lock) const
{
    FeeLevel64 const feeLevel =
        FeeMetrics::scaleFeeLevel(metricsSnapshot, view);

    if ((flags & tapPREFER_QUEUE) && !byFee_.empty())
        return std::max(feeLevel, byFee_.begin()->feeLevel);

    return feeLevel;
}

std::optional<std::pair<TER, bool>>
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

    SeqProxy const acctSeqProx = SeqProxy::sequence((*sleAccount)[sfSequence]);
    SeqProxy const txSeqProx = tx->getSeqProxy();

    // Can only directly apply if the transaction sequence matches the account
    // sequence or if the transaction uses a ticket.
    if (txSeqProx.isSeq() && txSeqProx != acctSeqProx)
        return {};

    FeeLevel64 const requiredFeeLevel = [this, &view, flags]() {
        std::lock_guard lock(mutex_);
        return getRequiredFeeLevel(
            view, flags, feeMetrics_.getSnapshot(), lock);
    }();

    // If the transaction's fee is high enough we may be able to put the
    // transaction straight into the ledger.
    FeeLevel64 const feeLevelPaid = getFeeLevelPaid(view, *tx);

    if (feeLevelPaid >= requiredFeeLevel)
    {
        // Attempt to apply the transaction directly.
        auto const transactionID = tx->getTransactionID();
        JLOG(j_.trace()) << "Applying transaction " << transactionID
                         << " to open ledger.";

        auto const [txnResult, didApply] =
            ripple::apply(app, view, *tx, flags, j);

        JLOG(j_.trace()) << "New transaction " << transactionID
                         << (didApply ? " applied successfully with "
                                      : " failed with ")
                         << transToken(txnResult);

        if (didApply)
        {
            // If the applied transaction replaced a transaction in the
            // queue then remove the replaced transaction.
            std::lock_guard lock(mutex_);

            AccountMap::iterator accountIter = byAccount_.find(account);
            if (accountIter != byAccount_.end())
            {
                TxQAccount& txQAcct = accountIter->second;
                if (auto const existingIter =
                        txQAcct.transactions.find(txSeqProx);
                    existingIter != txQAcct.transactions.end())
                {
                    removeFromByFee(existingIter, tx);
                }
            }
        }
        return {std::pair(txnResult, didApply)};
    }
    return {};
}

boost::optional<TxQ::TxQAccount::TxMap::iterator>
TxQ::removeFromByFee(
    boost::optional<TxQAccount::TxMap::iterator> const& replacedTxIter,
    std::shared_ptr<STTx const> const& tx)
{
    if (replacedTxIter && tx)
    {
        // If the transaction we're holding replaces a transaction in the
        // queue, remove the transaction that is being replaced.
        auto deleteIter = byFee_.iterator_to((*replacedTxIter)->second);
        assert(deleteIter != byFee_.end());
        assert(&(*replacedTxIter)->second == &*deleteIter);
        assert(deleteIter->seqProxy == tx->getSeqProxy());
        assert(deleteIter->account == (*tx)[sfAccount]);

        erase(deleteIter);
    }
    return boost::none;
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
    auto const baseFee = view.fees().toDrops(calculateBaseFee(view, *tx));
    auto const fee = FeeMetrics::scaleFeeLevel(snapshot, view);

    auto const sle = view.read(keylet::account(account));

    std::uint32_t const accountSeq = sle ? (*sle)[sfSequence] : 0;
    std::uint32_t const availableSeq = nextQueuableSeqImpl(sle, lock).value();

    return {mulDiv(fee, baseFee, baseLevel).second, accountSeq, availableSeq};
}

std::vector<TxQ::TxDetails>
TxQ::getAccountTxs(AccountID const& account, ReadView const& view) const
{
    std::vector<TxDetails> result;

    std::lock_guard lock(mutex_);

    AccountMap::const_iterator const accountIter{byAccount_.find(account)};

    if (accountIter == byAccount_.end() ||
        accountIter->second.transactions.empty())
        return result;

    result.reserve(accountIter->second.transactions.size());
    for (auto const& tx : accountIter->second.transactions)
    {
        result.emplace_back(tx.second.getTxDetails());
    }
    return result;
}

std::vector<TxQ::TxDetails>
TxQ::getTxs(ReadView const& view) const
{
    std::vector<TxDetails> result;

    std::lock_guard lock(mutex_);

    result.reserve(byFee_.size());

    for (auto const& tx : byFee_)
        result.emplace_back(tx.getTxDetails());

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

    drops[jss::base_fee] =
        to_string(toDrops(metrics.referenceFeeLevel, baseFee));
    drops[jss::minimum_fee] =
        to_string(toDrops(metrics.minProcessingFeeLevel, baseFee));
    drops[jss::median_fee] = to_string(toDrops(metrics.medFeeLevel, baseFee));
    drops[jss::open_ledger_fee] = to_string(
        toDrops(metrics.openLedgerFeeLevel - FeeLevel64{1}, baseFee) + 1);

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

    setup.standAlone = config.standalone();
    return setup;
}

}  // namespace ripple
