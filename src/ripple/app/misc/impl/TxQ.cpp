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

#include <ripple/app/misc/TxQ.h>
#include <ripple/app/ledger/OpenLedger.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/LoadFeeTrack.h>
#include <ripple/app/tx/apply.h>
#include <ripple/protocol/st.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/basics/mulDiv.h>
#include <boost/algorithm/clamp.hpp>
#include <limits>
#include <numeric>

namespace ripple {

//////////////////////////////////////////////////////////////////////////

static
std::uint64_t
getFeeLevelPaid(
    STTx const& tx,
    std::uint64_t baseRefLevel,
    std::uint64_t refTxnCostDrops,
    TxQ::Setup const& setup)
{
    if (refTxnCostDrops == 0)
        // If nothing is required, or the cost is 0,
        // the level is effectively infinite.
        return setup.zeroBaseFeeTransactionFeeLevel;

    // If the math overflows, return the clipped
    // result blindly. This is very unlikely to ever
    // happen.
    return mulDiv(tx[sfFee].xrp().drops(),
        baseRefLevel,
            refTxnCostDrops).second;
}

static
boost::optional<LedgerIndex>
getLastLedgerSequence(STTx const& tx)
{
    if (!tx.isFieldPresent(sfLastLedgerSequence))
        return boost::none;
    return tx.getFieldU32(sfLastLedgerSequence);
}

static
std::uint64_t
increase(std::uint64_t level,
    std::uint32_t increasePercent)
{
    return mulDiv(
        level, 100 + increasePercent, 100).second;
}


//////////////////////////////////////////////////////////////////////////

std::size_t
TxQ::FeeMetrics::update(Application& app,
    ReadView const& view, bool timeLeap,
    TxQ::Setup const& setup)
{
    std::size_t txnsExpected;
    std::size_t mimimumTx;
    std::uint64_t escalationMultiplier;
    {
        std::lock_guard <std::mutex> sl(lock_);
        txnsExpected = txnsExpected_;
        mimimumTx = minimumTxnCount_;
        escalationMultiplier = escalationMultiplier_;
    }
    std::vector<uint64_t> feeLevels;
    feeLevels.reserve(txnsExpected);
    for (auto const& tx : view.txs)
    {
        auto const baseFee = calculateBaseFee(app, view,
            *tx.first, j_);
        feeLevels.push_back(getFeeLevelPaid(*tx.first,
            baseLevel, baseFee, setup));
    }
    std::sort(feeLevels.begin(), feeLevels.end());
    auto const size = feeLevels.size();

    JLOG(j_.debug()) << "Ledger " << view.info().seq <<
        " has " << size << " transactions. " <<
        "Ledgers are processing " <<
        (timeLeap ? "slowly" : "as expected") <<
        ". Expected transactions is currently " <<
        txnsExpected << " and multiplier is " <<
        escalationMultiplier;

    if (timeLeap)
    {
        // Ledgers are taking to long to process,
        // so clamp down on limits.
        txnsExpected = boost::algorithm::clamp(feeLevels.size(),
            mimimumTx, targetTxnCount_);
    }
    else if (feeLevels.size() > txnsExpected ||
        feeLevels.size() > targetTxnCount_)
    {
        // Ledgers are processing in a timely manner,
        // so keep the limit high, but don't let it
        // grow without bound.
        txnsExpected = maximumTxnCount_ ?
            std::min(feeLevels.size(), *maximumTxnCount_) :
            feeLevels.size();
    }

    if (feeLevels.empty())
    {
        escalationMultiplier = minimumMultiplier_;
    }
    else
    {
        // In the case of an odd number of elements, this
        // evaluates to the middle element; for an even
        // number of elements, it will add the two elements
        // on either side of the "middle" and average them.
        escalationMultiplier = (feeLevels[size / 2] +
            feeLevels[(size - 1) / 2] + 1) / 2;
        escalationMultiplier = std::max(escalationMultiplier,
            minimumMultiplier_);
    }
    JLOG(j_.debug()) << "Expected transactions updated to " <<
        txnsExpected << " and multiplier updated to " <<
        escalationMultiplier;

    std::lock_guard <std::mutex> sl(lock_);
    txnsExpected_ = txnsExpected;
    escalationMultiplier_ = escalationMultiplier;

    return size;
}

std::uint64_t
TxQ::FeeMetrics::scaleFeeLevel(OpenView const& view,
    std::uint32_t txCountPadding) const
{
    // Transactions in the open ledger so far
    auto const current = view.txCount() + txCountPadding;

    auto const params = [&]
    {
        std::lock_guard <std::mutex> sl(lock_);
        return std::make_pair(txnsExpected_,
            escalationMultiplier_);
    }();
    auto const target = params.first;
    auto const multiplier = params.second;

    // Once the open ledger bypasses the target,
    // escalate the fee quickly.
    if (current > target)
    {
        // Compute escalated fee level
        // Don't care about the overflow flag
        return mulDiv(multiplier, current * current,
            target * target).second;
    }

    return baseLevel;
}

namespace detail {

static
std::pair<bool, std::uint64_t>
sumOfFirstSquares(std::size_t x)
{
    // sum(n = 1->x) : n * n = x(x + 1)(2x + 1) / 6

    // If x is anywhere on the order of 2^^21, it's going
    // to completely dominate the computation and is likely
    // enough to overflow that we're just going to assume
    // it does. If we have anywhere near 2^^21 transactions
    // in a ledger, this is the least of our problems.
    if (x >= (1 << 21))
        return std::make_pair(false,
            std::numeric_limits<std::uint64_t>::max());
    return std::make_pair(true, (x * (x + 1) * (2 * x + 1)) / 6);
}

}

std::pair<bool, std::uint64_t>
TxQ::FeeMetrics::escalatedSeriesFeeLevel(OpenView const& view,
    std::size_t extraCount, std::size_t seriesSize) const
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

    auto const params = [&] {
        std::lock_guard <std::mutex> sl(lock_);
        return std::make_pair(txnsExpected_,
            escalationMultiplier_);
    }();
    auto const target = params.first;
    auto const multiplier = params.second;

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
        return sumNlast;
    auto const totalFeeLevel = mulDiv(multiplier,
        sumNlast.second - sumNcurrent.second, target * target);

    return totalFeeLevel;
}


TxQ::MaybeTx::MaybeTx(
    std::shared_ptr<STTx const> const& txn_,
        TxID const& txID_, std::uint64_t feeLevel_,
            ApplyFlags const flags_,
                PreflightResult const& pfresult_)
    : txn(txn_)
    , feeLevel(feeLevel_)
    , txID(txID_)
    , account(txn_->getAccountID(sfAccount))
    , retriesRemaining(retriesAllowed)
    , sequence(txn_->getSequence())
    , flags(flags_)
    , pfresult(pfresult_)
{
    lastValid = getLastLedgerSequence(*txn);

    if (txn->isFieldPresent(sfAccountTxnID))
        priorTxID = txn->getFieldH256(sfAccountTxnID);
}

std::pair<TER, bool>
TxQ::MaybeTx::apply(Application& app, OpenView& view)
{
    // If the rules or flags change, preflight again
    assert(pfresult);
    if (pfresult->rules != view.rules() ||
        pfresult->flags != flags)
    {
        pfresult.emplace(
            preflight(app, view.rules(),
                pfresult->tx,
                flags,
                pfresult->j));
    }

    auto pcresult = preclaim(
        *pfresult, app, view);

    return doApply(pcresult, app, view);
}

TxQ::TxQAccount::TxQAccount(std::shared_ptr<STTx const> const& txn)
    :TxQAccount(txn->getAccountID(sfAccount))
{
}

TxQ::TxQAccount::TxQAccount(const AccountID& account_)
    : account(account_)
{
}

auto
TxQ::TxQAccount::add(MaybeTx&& txn)
    -> MaybeTx&
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

TxQ::TxQ(Setup const& setup,
    beast::Journal j)
    : setup_(setup)
    , j_(j)
    , feeMetrics_(setup, j)
    , maxSize_(boost::none)
{
}

TxQ::~TxQ()
{
    byFee_.clear();
}

template<size_t fillPercentage>
bool
TxQ::isFull() const
{
    static_assert(fillPercentage > 0 &&
        fillPercentage <= 100,
            "Invalid fill percentage");
    return maxSize_ && byFee_.size() >=
        (*maxSize_ * fillPercentage / 100);
}

bool
TxQ::canBeHeld(STTx const& tx, OpenView const& view,
    AccountMap::iterator accountIter,
        boost::optional<FeeMultiSet::iterator> replacementIter)
{
    // PreviousTxnID is deprecated and should never be used
    // AccountTxnID is not supported by the transaction
    // queue yet, but should be added in the future
    bool canBeHeld =
        ! tx.isFieldPresent(sfPreviousTxnID) &&
        ! tx.isFieldPresent(sfAccountTxnID);
    if (canBeHeld)
    {
        /* To be queued and relayed, the transaction needs to
            promise to stick around for long enough that it has
            a realistic chance of getting into a ledger.
        */
        auto lastValid = getLastLedgerSequence(tx);
        canBeHeld = !lastValid || *lastValid >=
            view.info().seq + setup_.minimumLastLedgerBuffer;
    }
    if (canBeHeld)
    {
        /* Limit the number of transactions an individual account
            can queue. Mitigates the lost cost of relaying should
            an early one fail or get dropped.
        */
        canBeHeld = accountIter == byAccount_.end() ||
            replacementIter ||
                accountIter->second.getTxnCount() <
                    setup_.maximumTxnPerAccount;
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
    auto const accountIter = txQAccount.transactions.find(
        candidateIter->sequence);
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
    bool const useAccountNext = accountNextIter != txQAccount.transactions.end() &&
        accountNextIter->first == candidateIter->sequence + 1 &&
            (feeNextIter == byFee_.end() ||
                accountNextIter->second.feeLevel > feeNextIter->feeLevel);
    auto const candidateNextIter = byFee_.erase(candidateIter);
    txQAccount.transactions.erase(accountIter);
    return useAccountNext ?
        byFee_.iterator_to(accountNextIter->second) :
            candidateNextIter;

}

auto
TxQ::erase(TxQ::TxQAccount& txQAccount,
    TxQ::TxQAccount::TxMap::const_iterator begin,
        TxQ::TxQAccount::TxMap::const_iterator end)
            -> TxQAccount::TxMap::iterator
{
    for (auto it = begin; it != end; ++it)
    {
        byFee_.erase(byFee_.iterator_to(it->second));
    }
    return txQAccount.transactions.erase(begin, end);
}

std::pair<TER, bool>
TxQ::tryClearAccountQueue(Application& app, OpenView& view,
    STTx const& tx, TxQ::AccountMap::iterator const& accountIter,
        TxQAccount::TxMap::iterator beginTxIter, std::uint64_t feeLevelPaid,
            PreflightResult const& pfresult, std::size_t const txExtraCount,
                ApplyFlags flags, beast::Journal j)
{
    auto const tSeq = tx.getSequence();
    assert(beginTxIter != accountIter->second.transactions.end());
    auto const aSeq = beginTxIter->first;

    auto const requiredTotalFeeLevel = feeMetrics_.escalatedSeriesFeeLevel(
        view, txExtraCount, tSeq - aSeq + 1);
    /* If the computation for the total manages to overflow (however extremely
        unlikely), then there's no way we can confidently verify if the queue
        can be cleared.
    */
    if (!requiredTotalFeeLevel.first)
        return std::make_pair(telINSUF_FEE_P, false);

    // Unlike multiTx, this check is only concerned with the range
    // from [aSeq, tSeq)
    auto endTxIter = accountIter->second.transactions.lower_bound(tSeq);

    auto const totalFeeLevelPaid = std::accumulate(beginTxIter, endTxIter,
        feeLevelPaid,
        [](auto const& total, auto const& tx)
        {
            return total + tx.second.feeLevel;
        });

    // This transaction did not pay enough, so fall back to the normal process.
    if (totalFeeLevelPaid < requiredTotalFeeLevel.second)
        return std::make_pair(telINSUF_FEE_P, false);

    // This transaction paid enough to clear out the queue.
    // Attempt to apply the queued transactions.
    for (auto it = beginTxIter; it != endTxIter; ++it)
    {
        auto txResult = it->second.apply(app, view);
        // Succeed or fail, use up a retry, because if the overall
        // process fails, we want the attempt to count. If it all
        // succeeds, the MaybeTx will be destructed, so it'll be
        // moot.
        --it->second.retriesRemaining;
        if (!txResult.second)
        {
            // Transaction failed to apply. Fall back to the normal process.
            return std::make_pair(txResult.first, false);
        }
    }
    // Apply the current tx. Because the state of the view has been changed
    // by the queued txs, we also need to preclaim again.
    auto const pcresult = preclaim(pfresult, app, view);
    auto txResult = doApply(pcresult, app, view);

    if (txResult.second)
    {
        // All of the queued transactions applied, so remove them from the queue.
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
    0. Is `featureFeeEscalation` enabled?
        Yes: Continue to next step.
        No: Fallback to `ripple::apply`. Stop.
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
            No: Reject `txn` with `telINSUF_FEE_P` or
                `telCAN_NOT_QUEUE`. Stop.
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
                    Yes: Reject with `telCAN_NOT_QUEUE`. Stop.
                    No: Are the fees in-flight of the other
                            queued txs >= than the account
                            balance or minimum account reserve?
                        Yes: Reject with `telCAN_NOT_QUEUE`. Stop.
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
            No: Reject `txn` with `telINSUF_FEE_P` if this tx
                has the current sequence, or `telCAN_NOT_QUEUE`
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
TxQ::apply(Application& app, OpenView& view,
    std::shared_ptr<STTx const> const& tx,
        ApplyFlags flags, beast::Journal j)
{
    auto const allowEscalation =
        (view.rules().enabled(featureFeeEscalation,
            app.config().features));
    if (!allowEscalation)
    {
        return ripple::apply(app, view, *tx, flags, j);
    }

    auto const account = (*tx)[sfAccount];
    auto const transactionID = tx->getTransactionID();
    auto const tSeq = tx->getSequence();

    // See if the transaction is valid, properly formed,
    // etc. before doing potentially expensive queue
    // replace and multi-transaction operations.
    auto const pfresult = preflight(app, view.rules(),
        *tx, flags, j);
    if (pfresult.ter != tesSUCCESS)
        return{ pfresult.ter, false };

    struct MultiTxn
    {
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

    std::lock_guard<std::mutex> lock(mutex_);

    // We may need the base fee for multiple transactions
    // or transaction replacement, so just pull it up now.
    // TODO: Do we want to avoid doing it again during
    //   preclaim?
    auto const baseFee = calculateBaseFee(app, view, *tx, j);
    auto const feeLevelPaid = getFeeLevelPaid(*tx,
        baseLevel, baseFee, setup_);
    auto const requiredFeeLevel = feeMetrics_.scaleFeeLevel(view);

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
                existingIter->second.feeLevel,
                    setup_.retrySequencePercent);
            JLOG(j_.trace()) << "Found transaction in queue for account " <<
                account << " with sequence number " << tSeq <<
                " new txn fee level is " << feeLevelPaid <<
                ", old txn fee level is " <<
                existingIter->second.feeLevel <<
                ", new txn needs fee level of " <<
                requiredRetryLevel;
            if (feeLevelPaid > requiredRetryLevel
                || (existingIter->second.feeLevel < requiredFeeLevel &&
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
                        consequences.emplace(calculateConsequences(
                            pfresult));
                        if (consequences->category ==
                            TxConsequences::blocker)
                        {
                            // Can't replace a normal transaction in the
                            // middle of the queue with a blocker.
                            JLOG(j_.trace()) <<
                                "Ignoring blocker transaction " <<
                                transactionID <<
                                " in favor of normal queued " <<
                                existingIter->second.txID;
                            return{existingIter == txQAcct.transactions.begin() ?
                                telINSUF_FEE_P : telCAN_NOT_QUEUE, false };
                        }
                    }
                }


                // Remove the queued transaction and continue
                JLOG(j_.trace()) <<
                    "Removing transaction from queue " <<
                    existingIter->second.txID <<
                    " in favor of " << transactionID;
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
                JLOG(j_.trace()) <<
                    "Ignoring transaction " <<
                    transactionID <<
                    " in favor of queued " <<
                    existingIter->second.txID;
                return{ telINSUF_FEE_P, false };
            }
        }
    }

    // If there are other transactions in the queue
    // for this account, account for that before the pre-checks,
    // so we don't get a false terPRE_SEQ.
    if (accountExists)
    {
        auto const sle = view.read(keylet::account(account));

        if (sle)
        {
            auto& txQAcct = accountIter->second;
            auto const aSeq = (*sle)[sfSequence];

            if (aSeq < tSeq)
            {
                // If the transaction is queueable, create the multiTxn
                // object to hold the info we need to adjust for
                // prior txns. Otherwise, let preclaim fail as if
                // we didn't have the queue at all.
                if (canBeHeld(*tx, view, accountIter, replacedItemDeleteIter))
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
                    if (workingSeq < tSeq &&
                        workingIter->first != workingSeq)
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
                            JLOG(j_.trace()) <<
                                "Ignoring transaction " <<
                                transactionID <<
                                ". Needs fee level of " <<

                                requiredMultiLevel <<
                                ". Only paid " <<
                                feeLevelPaid;
                            return{ telINSUF_FEE_P, false };
                        }
                    }
                    if (workingIter->first == tSeq)
                    {
                        // If we're replacing this transaction, don't
                        // count it.
                        assert(replacedItemDeleteIter);
                        multiTxn->includeCurrentFee =
                            std::next(workingIter) !=
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
                        JLOG(j_.trace()) <<
                            "Ignoring transaction " <<
                            transactionID <<
                            ". A blocker-type transaction " <<
                            "is in the queue.";
                        return{ telCAN_NOT_QUEUE, false };
                    }
                    multiTxn->fee +=
                        workingIter->second.consequences->fee;
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
                    transaction with a telCAN_NOT_QUEUE result.
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
                auto totalFee = multiTxn->fee;
                if (multiTxn->includeCurrentFee)
                    totalFee += (*tx)[sfFee].xrp();
                if (totalFee >= balance ||
                    totalFee >= view.fees().accountReserve(0))
                {
                    // Drop the current transaction
                    JLOG(j_.trace()) <<
                        "Ignoring transaction " <<
                        transactionID <<
                        ". Total fees in flight too high.";
                    return{ telCAN_NOT_QUEUE, false };
                }

                // Create the test view from the current view
                multiTxn->applyView.emplace(&view, flags);
                multiTxn->openView.emplace(&*multiTxn->applyView);

                auto const sleBump = multiTxn->applyView->peek(
                    keylet::account(account));

                sleBump->setFieldAmount(sfBalance,
                    balance - (multiTxn->fee +
                        multiTxn->potentialSpend));
                sleBump->setFieldU32(sfSequence, tSeq);
            }
        }
    }

    // See if the transaction is likely to claim a fee.
    assert(!multiTxn || multiTxn->openView);
    auto const pcresult = preclaim(pfresult, app,
        multiTxn ? *multiTxn->openView : view);
    if (!pcresult.likelyToClaimFee)
        return{ pcresult.ter, false };

    // Too low of a fee should get caught by preclaim
    assert(feeLevelPaid >= baseLevel);

    JLOG(j_.trace()) << "Transaction " <<
        transactionID <<
        " from account " << account <<
        " has fee level of " << feeLevelPaid <<
        " needs at least " << requiredFeeLevel <<
        " to get in the open ledger, which has " <<
        view.txCount() << " entries.";

    /* Quick heuristic check to see if it's worth checking that this
        tx has a high enough fee to clear all the txs in the queue.
        1) Must be an account already in the queue.
        2) Must be have passed the multiTxn checks (tx is not the next
            account seq, the skipped seqs are in the queue, the reserve
            doesn't get exhausted, etc).
        3) The next transaction must not have previously tried and failed
            to apply to an open ledger.
        4) Tx must be paying more than just the required fee level to
            get itself into the queue.
        5) Fee level must be escalated above the default (if it's not,
            then the first tx _must_ have failed to process in `accept`
            for some other reason. Tx is allowed to queue in case
            conditions change, but don't waste the effort to clear).
        6) Tx is not a 0-fee / free transaction, regardless of fee level.
    */
    if (accountExists && multiTxn.is_initialized() &&
        multiTxn->nextTxIter->second.retriesRemaining == MaybeTx::retriesAllowed &&
        feeLevelPaid > requiredFeeLevel &&
            requiredFeeLevel > baseLevel && baseFee != 0)
    {
        OpenView sandbox(open_ledger, &view, view.rules());

        auto result = tryClearAccountQueue(app, sandbox, *tx, accountIter,
            multiTxn->nextTxIter, feeLevelPaid, pfresult, view.txCount(),
                flags, j);
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

        JLOG(j_.trace()) << "Applying transaction " <<
            transactionID <<
            " to open ledger.";
        ripple::TER txnResult;
        bool didApply;

        std::tie(txnResult, didApply) = doApply(pcresult, app, view);

        JLOG(j_.trace()) << "Transaction " <<
            transactionID <<
                (didApply ? " applied successfully with " :
                    " failed with ") <<
                        transToken(txnResult);

        if (didApply && replacedItemDeleteIter)
            erase(*replacedItemDeleteIter);
        return { txnResult, didApply };
    }

    // If `multiTxn` has a value, then `canBeHeld` has already been verified
    if (! multiTxn &&
        ! canBeHeld(*tx, view, accountIter, replacedItemDeleteIter))
    {
        // Bail, transaction cannot be held
        JLOG(j_.trace()) << "Transaction " <<
            transactionID <<
            " can not be held";
        return { feeLevelPaid >= requiredFeeLevel ?
            telCAN_NOT_QUEUE : telINSUF_FEE_P, false };
    }

    // If the queue is full, decide whether to drop the current
    // transaction or the last transaction for the account with
    // the lowest fee.
    if (!replacedItemDeleteIter && isFull())
    {
        auto lastRIter = byFee_.rbegin();
        if (lastRIter->account == account)
        {
            JLOG(j_.warn()) << "Queue is full, and transaction " <<
                transactionID <<
                " would kick a transaction from the same account (" <<
                account << ") out of the queue.";
            return { telCAN_NOT_QUEUE, false };
        }
        auto const& endAccount = byAccount_.at(lastRIter->account);
        auto endEffectiveFeeLevel = [&]()
        {
            // Compute the average of all the txs for the endAccount,
            // but only if the last tx in the queue has a lower fee
            // level than this candidate tx.
            if (lastRIter->feeLevel > feeLevelPaid
                || endAccount.transactions.size() == 1)
                return lastRIter->feeLevel;

            constexpr std::uint64_t max =
                std::numeric_limits<std::uint64_t>::max();
            auto endTotal = std::accumulate(endAccount.transactions.begin(),
                endAccount.transactions.end(),
                    std::pair<std::uint64_t, std::uint64_t>(0, 0),
                        [&](auto const& total, auto const& tx)
                        {
                            // Check for overflow.
                            auto next = tx.second.feeLevel /
                                endAccount.transactions.size();
                            auto mod = tx.second.feeLevel %
                                endAccount.transactions.size();
                            if (total.first >= max - next ||
                                    total.second >= max - mod)
                                return std::make_pair(max, std::uint64_t(0));
                            return std::make_pair(total.first + next, total.second + mod);
                        });
            return endTotal.first + endTotal.second /
                endAccount.transactions.size();
        }();
        if (feeLevelPaid > endEffectiveFeeLevel)
        {
            // The queue is full, and this transaction is more
            // valuable, so kick out the cheapest transaction.
            auto dropRIter = endAccount.transactions.rbegin();
            assert(dropRIter->second.account == lastRIter->account);
            JLOG(j_.warn()) <<
                "Removing last item of account " <<
                lastRIter->account <<
                "from queue with average fee of" <<
                endEffectiveFeeLevel << " in favor of " <<
                transactionID << " with fee of " <<
                feeLevelPaid;
            erase(byFee_.iterator_to(dropRIter->second));
        }
        else
        {
            JLOG(j_.warn()) << "Queue is full, and transaction " <<
                transactionID <<
                " fee is lower than end item's account average fee";
            return { telINSUF_FEE_P, false };
        }
    }

    // Hold the transaction in the queue.
    if (replacedItemDeleteIter)
        erase(*replacedItemDeleteIter);
    if (!accountExists)
    {
        // Create a new TxQAccount object and add the byAccount lookup.
        bool created;
        std::tie(accountIter, created) = byAccount_.emplace(
            account, TxQAccount(tx));
        (void)created;
        assert(created);
    }
    auto& candidate = accountIter->second.add(
        { tx, transactionID, feeLevelPaid, flags, pfresult });
    /* Normally we defer figuring out the consequences until
        something later requires us to, but if we know the
        consequences now, save them for later.
    */
    if (consequences)
        candidate.consequences.emplace(*consequences);
    // Then index it into the byFee lookup.
    byFee_.insert(candidate);
    JLOG(j_.debug()) << "Added transaction " << candidate.txID <<
        " from " << (accountExists ? "existing" : "new") <<
            " account " << candidate.account << " to queue.";

    return { terQUEUED, false };
}

/*
    0. Is `featureFeeEscalation` enabled?
        Yes: Continue to next step.
        No: Stop.
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
TxQ::processClosedLedger(Application& app,
    OpenView const& view, bool timeLeap)
{
    auto const allowEscalation =
        (view.rules().enabled(featureFeeEscalation,
            app.config().features));
    if (!allowEscalation)
    {
        return;
    }

    feeMetrics_.update(app, view, timeLeap, setup_);

    auto ledgerSeq = view.info().seq;

    std::lock_guard<std::mutex> lock(mutex_);

    if (!timeLeap)
        maxSize_ = feeMetrics_.getTxnsExpected() * setup_.ledgersInQueue;

    // Remove any queued candidates whose LastLedgerSequence has gone by.
    for(auto candidateIter = byFee_.begin(); candidateIter != byFee_.end(); )
    {
        if (candidateIter->lastValid
            && *candidateIter->lastValid <= ledgerSeq)
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

    0. Is `featureFeeEscalation` enabled?
        Yes: Continue to next step.
        No: Don't do anything to the open ledger. Stop.
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
TxQ::accept(Application& app,
    OpenView& view)
{
    auto const allowEscalation =
        (view.rules().enabled(featureFeeEscalation,
            app.config().features));
    if (!allowEscalation)
    {
        return false;
    }

    /* Move transactions from the queue from largest fee level to smallest.
       As we add more transactions, the required fee level will increase.
       Stop when the transaction fee level gets lower than the required fee
       level.
    */

    auto ledgerChanged = false;

    std::lock_guard<std::mutex> lock(mutex_);

    for (auto candidateIter = byFee_.begin(); candidateIter != byFee_.end();)
    {
        auto& account = byAccount_.at(candidateIter->account);
        if (candidateIter->sequence >
            account.transactions.begin()->first)
        {
            // This is not the first transaction for this account, so skip it.
            // It can not succeed yet.
            JLOG(j_.trace()) << "Skipping queued transaction " <<
                candidateIter->txID << " from account " <<
                candidateIter->account << " as it is not the first.";
            candidateIter++;
            continue;
        }
        auto const requiredFeeLevel = feeMetrics_.scaleFeeLevel(view);
        auto const feeLevelPaid = candidateIter->feeLevel;
        JLOG(j_.trace()) << "Queued transaction " <<
            candidateIter->txID << " from account " <<
            candidateIter->account << " has fee level of " <<
            feeLevelPaid << " needs at least " <<
            requiredFeeLevel;
        if (feeLevelPaid >= requiredFeeLevel)
        {
            auto firstTxn = candidateIter->txn;

            JLOG(j_.trace()) << "Applying queued transaction " <<
                candidateIter->txID << " to open ledger.";

            TER txnResult;
            bool didApply;
            std::tie(txnResult, didApply) = candidateIter->apply(app, view);

            if (didApply)
            {
                // Remove the candidate from the queue
                JLOG(j_.debug()) << "Queued transaction " <<
                    candidateIter->txID <<
                    " applied successfully. Remove from queue.";

                candidateIter = eraseAndAdvance(candidateIter);
                ledgerChanged = true;
            }
            else if (isTefFailure(txnResult) || isTemMalformed(txnResult) ||
                candidateIter->retriesRemaining <= 0)
            {
                if (candidateIter->retriesRemaining <= 0)
                    account.retryPenalty = true;
                else
                    account.dropPenalty = true;
                JLOG(j_.debug()) << "Queued transaction " <<
                    candidateIter->txID << " failed with " <<
                    transToken(txnResult) << ". Remove from queue.";
                candidateIter = eraseAndAdvance(candidateIter);
            }
            else
            {
                JLOG(j_.debug()) << "Transaction " <<
                    candidateIter->txID << " failed with " <<
                    transToken(txnResult) << ". Leave in queue.";
                if (account.retryPenalty &&
                        candidateIter->retriesRemaining > 2)
                    candidateIter->retriesRemaining = 1;
                else
                    --candidateIter->retriesRemaining;
                if (account.dropPenalty &&
                    account.transactions.size() > 1 && isFull<95>())
                {
                    /* The queue is close to full, this account has multiple
                        txs queued, and this account has had a transaction
                        fail. Even though we're giving this transaction another
                        chance, chances are it won't recover. So we don't make
                        things worse, drop the _last_ transaction for this account.
                    */
                    auto dropRIter = account.transactions.rbegin();
                    assert(dropRIter->second.account == candidateIter->account);
                    JLOG(j_.warn()) <<
                        "Queue is nearly full, and transaction " <<
                        candidateIter->txID << " failed with " <<
                        transToken(txnResult) <<
                        ". Removing last item of account " <<
                        account.account;
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

auto
TxQ::getMetrics(Config const& config, OpenView const& view,
    std::uint32_t txCountPadding) const
        -> boost::optional<Metrics>
{
    auto const allowEscalation =
        (view.rules().enabled(featureFeeEscalation,
            config.features));
    if (!allowEscalation)
        return boost::none;

    Metrics result;

    std::lock_guard<std::mutex> lock(mutex_);

    result.txCount = byFee_.size();
    result.txQMaxSize = maxSize_;
    result.txInLedger = view.txCount();
    result.txPerLedger = feeMetrics_.getTxnsExpected();
    result.referenceFeeLevel = baseLevel;
    result.minFeeLevel = isFull() ? byFee_.rbegin()->feeLevel + 1 :
        baseLevel;
    result.medFeeLevel = feeMetrics_.getEscalationMultiplier();
    result.expFeeLevel = feeMetrics_.scaleFeeLevel(view, txCountPadding);

    return result;
}

auto
TxQ::getAccountTxs(AccountID const& account, Config const& config,
    ReadView const& view) const
        -> boost::optional<std::map<TxSeq, AccountTxDetails>>
{
    auto const allowEscalation =
        (view.rules().enabled(featureFeeEscalation,
            config.features));
    if (!allowEscalation)
        return boost::none;

    std::lock_guard<std::mutex> lock(mutex_);

    auto accountIter = byAccount_.find(account);
    if (accountIter == byAccount_.end() ||
        accountIter->second.transactions.empty())
        return boost::none;

    std::map<TxSeq, AccountTxDetails> result;

    for (auto const& tx : accountIter->second.transactions)
    {
        auto& resultTx = result[tx.first];
        resultTx.feeLevel = tx.second.feeLevel;
        if(tx.second.lastValid)
            resultTx.lastValid.emplace(*tx.second.lastValid);
        if(tx.second.consequences)
            resultTx.consequences.emplace(*tx.second.consequences);
    }
    return result;
}

Json::Value
TxQ::doRPC(Application& app) const
{
    using std::to_string;

    auto const view = app.openLedger().current();
    auto const metrics = getMetrics(app.config(), *view);

    if (!metrics)
        return{};

    Json::Value ret(Json::objectValue);

    auto& levels = ret[jss::levels] = Json::objectValue;

    ret[jss::ledger_current_index] = view->info().seq;
    ret[jss::expected_ledger_size] = to_string(metrics->txPerLedger);
    ret[jss::current_ledger_size] = to_string(metrics->txInLedger);
    ret[jss::current_queue_size] = to_string(metrics->txCount);
    if (metrics->txQMaxSize)
        ret[jss::max_queue_size] = to_string(*metrics->txQMaxSize);

    levels[jss::reference_level] = to_string(metrics->referenceFeeLevel);
    levels[jss::minimum_level] = to_string(metrics->minFeeLevel);
    levels[jss::median_level] = to_string(metrics->medFeeLevel);
    levels[jss::open_ledger_level] = to_string(metrics->expFeeLevel);

    auto const baseFee = view->fees().base;
    auto& drops = ret[jss::drops] = Json::Value();

    // Don't care about the overflow flags
    drops[jss::base_fee] = to_string(mulDiv(
        metrics->referenceFeeLevel, baseFee,
            metrics->referenceFeeLevel).second);
    drops[jss::minimum_fee] = to_string(mulDiv(
        metrics->minFeeLevel, baseFee,
            metrics->referenceFeeLevel).second);
    drops[jss::median_fee] = to_string(mulDiv(
        metrics->medFeeLevel, baseFee,
            metrics->referenceFeeLevel).second);
    auto escalatedFee = mulDiv(
        metrics->expFeeLevel, baseFee,
            metrics->referenceFeeLevel).second;
    if (mulDiv(escalatedFee, metrics->referenceFeeLevel,
            baseFee).second < metrics->expFeeLevel)
        ++escalatedFee;

    drops[jss::open_ledger_fee] = to_string(escalatedFee);

    return ret;
}

//////////////////////////////////////////////////////////////////////////

TxQ::Setup
setup_TxQ(Config const& config)
{
    TxQ::Setup setup;
    auto const& section = config.section("transaction_queue");
    set(setup.ledgersInQueue, "ledgers_in_queue", section);
    set(setup.retrySequencePercent, "retry_sequence_percent", section);
    set(setup.multiTxnPercent, "multi_txn_percent", section);
    set(setup.minimumEscalationMultiplier,
        "minimum_escalation_multiplier", section);
    set(setup.minimumTxnInLedger, "minimum_txn_in_ledger", section);
    set(setup.minimumTxnInLedgerSA,
        "minimum_txn_in_ledger_standalone", section);
    set(setup.targetTxnInLedger, "target_txn_in_ledger", section);
    std::uint32_t max;
    if (set(max, "maximum_txn_in_ledger", section))
        setup.maximumTxnInLedger.emplace(max);
    set(setup.maximumTxnPerAccount, "maximum_txn_per_account", section);
    set(setup.minimumLastLedgerBuffer,
        "minimum_last_ledger_buffer", section);
    set(setup.zeroBaseFeeTransactionFeeLevel,
        "zero_basefee_transaction_feelevel", section);

    setup.standAlone = config.standalone();
    return setup;
}


std::unique_ptr<TxQ>
make_TxQ(TxQ::Setup const& setup, beast::Journal j)
{
    return std::make_unique<TxQ>(setup, std::move(j));
}

} // ripple
