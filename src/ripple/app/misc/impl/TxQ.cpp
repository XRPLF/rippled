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
#include <ripple/app/tx/apply.h>
#include <ripple/protocol/st.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/basics/mulDiv.h>
#include <boost/algorithm/clamp.hpp>
#include <limits>

namespace ripple {

//////////////////////////////////////////////////////////////////////////

static
std::uint64_t
getRequiredFeeLevel(TxType txType)
{
    // For now, all valid non-pseudo transactions have a level of 256,
    // and no pseudo transactions should ever be seen by the open
    // ledger (and if one somehow is, it will have a 0 fee).
    // This code can be changed to support variable transaction fees
    // based on txType.
    return 256;
}

static
std::uint64_t
getFeeLevelPaid(
    STTx const& tx,
    std::uint64_t baseRefLevel,
    std::uint64_t refTxnCostDrops)
{
    // Compute the minimum XRP fee the transaction could pay
    auto requiredFee = getRequiredFeeLevel(tx.getTxnType());

    if (requiredFee == 0 ||
        refTxnCostDrops == 0)
        // If nothing is required, or the cost is 0,
        // the level is effectively infinite.
        return std::numeric_limits<std::uint64_t>::max();

    // TODO: getRequiredFeeLevel(ttREFERENCE)?
    auto referenceFee =
        ripple::getRequiredFeeLevel(ttACCOUNT_SET);
    // Don't care about the overflow flag
    return mulDiv(tx[sfFee].xrp().drops(),
        baseRefLevel * referenceFee,
            refTxnCostDrops * requiredFee).second;
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
    ReadView const& view, bool timeLeap)
{
    std::vector<uint64_t> feeLevels;
    std::size_t txnsExpected;
    std::size_t mimimumTx;
    std::uint32_t escalationMultiplier;
    {
        std::lock_guard <std::mutex> sl(lock_);
        feeLevels.reserve(txnsExpected_);
        txnsExpected = txnsExpected_;
        mimimumTx = minimumTxnCount_;
        escalationMultiplier = escalationMultiplier_;
    }
    for (auto const& tx : view.txs)
    {
        auto const baseFee = calculateBaseFee(app, view,
            *tx.first, j_);
        feeLevels.push_back(getFeeLevelPaid(*tx.first,
            baseLevel, baseFee));
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
TxQ::FeeMetrics::scaleFeeLevel(OpenView const& view) const
{
    auto fee = baseLevel;

    // Transactions in the open ledger so far
    auto const current = view.txCount();

    std::size_t target;
    std::uint32_t multiplier;
    {
        std::lock_guard <std::mutex> sl(lock_);

        // Target number of transactions allowed
        target = txnsExpected_;
        multiplier = escalationMultiplier_;
    }

    // Once the open ledger bypasses the target,
    // escalate the fee quickly.
    if (current > target)
    {
        // Compute escalated fee level
        // Don't care about the overflow flag
        fee = mulDiv(fee, current * current *
            multiplier, target * target).second;
    }

    return fee;
}

TxQ::CandidateTxn::CandidateTxn(
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
    if (txn->isFieldPresent(sfLastLedgerSequence))
        lastValid = txn->getFieldU32(sfLastLedgerSequence);

    if (txn->isFieldPresent(sfAccountTxnID))
        priorTxID = txn->getFieldH256(sfAccountTxnID);
}

std::pair<TER, bool>
TxQ::CandidateTxn::apply(Application& app, OpenView& view)
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
TxQ::TxQAccount::addCandidate(CandidateTxn&& txn)
    -> CandidateTxn&
{
    auto sequence = txn.sequence;

    auto result = transactions.emplace(sequence, std::move(txn));
    assert(result.second);
    assert(&result.first->second != &txn);

    return result.first->second;
}

bool
TxQ::TxQAccount::removeCandidate(TxSeq const& sequence)
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

bool
TxQ::canBeHeld(std::shared_ptr<STTx const> const& tx)
{
    // PreviousTxnID is deprecated and should never be used
    // AccountTxnID is not supported by the transaction
    // queue yet, but should be added in the future
    bool canBeHeld =
        ! tx->isFieldPresent(sfPreviousTxnID) &&
        ! tx->isFieldPresent(sfAccountTxnID);
    /*
    if (canBeHeld)
    {
        auto accountIter = byAccount_.find(tx->getAccountID(sfAccount));
        canBeHeld = accountIter == byAccount_.end()
            || accountIter->second.empty();
    }
    */
    return canBeHeld;
}

auto
TxQ::erase(TxQ::FeeMultiSet::const_iterator_type candidateIter)
    -> FeeMultiSet::iterator_type
{
    auto& txQAccount = byAccount_.at(candidateIter->account);
    auto sequence = candidateIter->sequence;
    auto newCandidateIter = byFee_.erase(candidateIter);
    // Now that the candidate has been removed from the
    // intrusive list remove it from the TxQAccount
    // so the memory can be freed.
    auto found = txQAccount.removeCandidate(sequence);
    (void)found;
    assert(found);

    return newCandidateIter;
}

auto
TxQ::eraseAndAdvance(TxQ::FeeMultiSet::const_iterator_type candidateIter)
    -> FeeMultiSet::iterator_type
{
    auto& txQAccount = byAccount_.at(candidateIter->account);
    auto accountIter = txQAccount.transactions.find(
        candidateIter->sequence);
    assert(accountIter != txQAccount.transactions.end());
    assert(accountIter == txQAccount.transactions.begin());
    assert(byFee_.iterator_to(accountIter->second) == candidateIter);
    auto accountNextIter = std::next(accountIter);
    // Check if the next transaction for this account has the
    // next sequence number, and a higher fee, which means we
    // skipped it earlier, and need to try it again.
    bool useAccountNext =
        accountNextIter != txQAccount.transactions.end() &&
            accountNextIter->first == candidateIter->sequence + 1 &&
                accountNextIter->second.feeLevel > candidateIter->feeLevel;
    auto candidateNextIter = byFee_.erase(candidateIter);
    txQAccount.transactions.erase(accountIter);
    return useAccountNext ?
        byFee_.iterator_to(accountNextIter->second) :
            candidateNextIter;

}

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
    auto const t_seq = tx->getSequence();

    // See if the transaction is valid, properly formed,
    // etc. before doing potentially expensive queue
    // replace and multi-transaction operations.
    auto const pfresult = preflight(app, view.rules(),
        *tx, flags, j);
    if (pfresult.ter != tesSUCCESS)
        return{ pfresult.ter, false };

    struct MultiTxn
    {
        TxQAccount::TxMap::iterator nextAcctIter;
        TxQAccount::TxMap::iterator prevTxnIter;
        boost::optional<ApplyViewImpl> applyView;
        boost::optional<OpenView> openView;

        XRPAmount fee = beast::zero;
        XRPAmount potentialSpend = beast::zero;

        MultiTxn(TxQAccount::TxMap::iterator nextAcctIter_,
            TxQAccount::TxMap::iterator prevTxnIter_)
            : nextAcctIter(nextAcctIter_)
            , prevTxnIter(prevTxnIter_)
        { }
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
        feeMetrics_.baseLevel, baseFee);
    auto const requiredFeeLevel = feeMetrics_.scaleFeeLevel(view);

    auto accountIter = byAccount_.find(account);
    bool const accountExists = accountIter != byAccount_.end();

    // Is there a transaction for the same account with the
    // same sequence number already in the queue?
    if (accountExists)
    {
        auto& txQAcct = accountIter->second;
        auto existingIter = txQAcct.transactions.find(t_seq);
        if (existingIter != txQAcct.transactions.end())
        {
            // Is the current transaction's fee higher than
            // the queued transaction's fee + a percentage
            auto requiredRetryLevel = increase(
                existingIter->second.feeLevel,
                    setup_.retrySequencePercent);
            JLOG(j_.trace()) << "Found transaction in queue for account " <<
                account << " with sequence number " << t_seq <<
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
                    // Only the last tx in the queue should have
                    // !consequences, and this can't be the last tx.
                    assert(existingIter->second.consequences);
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
                assert(deleteIter->sequence == t_seq);
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
            auto const a_seq = (*sle)[sfSequence];

            if (a_seq != t_seq)
            {
                // Only if the queue has entries for all the
                // seq's in [a_seq, t_seq), create the multiTxn
                // object containing the info we need to adjust for
                // prior txns. Otherwise, let preclaim fail as if
                // we didn't have the queue at all.
                auto nextAcctIter = txQAcct.transactions.find(a_seq);
                auto prevTxnIter = txQAcct.transactions.find(t_seq - 1);
                if ((nextAcctIter != txQAcct.transactions.end()) &&
                    (prevTxnIter != txQAcct.transactions.end()) &&
                        std::distance(nextAcctIter, prevTxnIter) ==
                            t_seq - a_seq - 1)
                    multiTxn.emplace(nextAcctIter, prevTxnIter);
            }

            if (multiTxn)
            {
                // Is the current transaction's fee higher than
                // the previous transaction's fee + a percentage
                auto requiredMultiLevel = increase(
                    multiTxn->prevTxnIter->second.feeLevel,
                        setup_.multiTxnPercent);

                if (feeLevelPaid > requiredMultiLevel)
                {
                    // Sum up the consequences of the queued txs.
                    // Abort if a blocker is found.
                    auto workingIter = multiTxn->nextAcctIter;
                    for (auto const end = std::next(multiTxn->prevTxnIter);
                        workingIter != end; ++workingIter)
                    {
                        if (!workingIter->second.consequences)
                            workingIter->second.consequences.emplace(
                                calculateConsequences(
                                    *workingIter->second.pfresult));
                        if (workingIter->second.consequences->category ==
                            TxConsequences::blocker)
                        {
                            // Drop the current transaction, because it's
                            // blocked by this one.
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
                    /* If there are any transactions AFTER this one, include their
                        fees in the in-flight total.
                    */
                    if (workingIter != txQAcct.transactions.end()
                        && workingIter->first == t_seq)
                    {
                        ++workingIter;
                    }
                    for (; workingIter != txQAcct.transactions.end();
                        ++workingIter)
                    {
                        if (!workingIter->second.consequences)
                            workingIter->second.consequences.emplace(
                                calculateConsequences(
                                    *workingIter->second.pfresult));
                        // Don't worry about the blocker status, since this
                        // one comes first. (And its blocker status was
                        // already checked.)
                        multiTxn->fee +=
                            workingIter->second.consequences->fee;
                        multiTxn->potentialSpend +=
                            workingIter->second.consequences->potentialSpend;
                    }
                }
                else
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
                    LastLedgerSeq and CandidateTxn::retriesRemaining.
                */
                auto const balance = (*sle)[sfBalance].xrp();
                auto totalFee = multiTxn->fee;
                if (replacedItemDeleteIter
                        && std::next(multiTxn->prevTxnIter, 2) !=
                            txQAcct.transactions.end())
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
                sleBump->setFieldU32(sfSequence, t_seq);
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
    assert(feeLevelPaid >= feeMetrics_.baseLevel);

    JLOG(j_.trace()) << "Transaction " <<
        transactionID <<
        " from account " << account <<
        " has fee level of " << feeLevelPaid <<
        " needs at least " << requiredFeeLevel <<
        " to get in the open ledger, which has " <<
        view.txCount() << " entries.";

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

    if (! canBeHeld(tx))
    {
        // Bail, transaction cannot be held
        JLOG(j_.trace()) << "Transaction " <<
            transactionID <<
            " can not be held";
        return { feeLevelPaid >= requiredFeeLevel ?
            telCAN_NOT_QUEUE : telINSUF_FEE_P, false };
    }

    // It's pretty unlikely that the queue will be "overfilled",
    // but should it happen, take the opportunity to fix it now,
    // unless the transaction is replacing one already there.
    /* TODO: Rethink how a full queue is handled. If account1
        has several transactions queued, and account2 pushes
        an early one out, that could orphan the remainder of
        account1's, and no fees will be claimed for account1.
    */
    while (!replacedItemDeleteIter && isFull())
    {
        auto lastRIter = byFee_.rbegin();
        if (lastRIter->account == account &&
            lastRIter->sequence < t_seq)
        {
            JLOG(j_.warn()) << "Queue is full, and transaction " <<
                transactionID <<
                " would kick a transaction from the same account (" <<
                account << ") out of the queue.";
            return { telCAN_NOT_QUEUE, false };
        }
        if (feeLevelPaid > lastRIter->feeLevel)
        {
            // The queue is full, and this transaction is more
            // valuable, so kick out the cheapest transaction.
            JLOG(j_.warn()) <<
                "Removing end item from queue with fee of" <<
                lastRIter->feeLevel << " in favor of " <<
                transactionID << " with fee of " <<
                feeLevelPaid;
            auto endIter = byFee_.iterator_to(*lastRIter);
            erase(endIter);
        }
        else
        {
            JLOG(j_.warn()) << "Queue is full, and transaction " <<
                transactionID <<
                " fee is lower than end item";
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
    auto& candidate = accountIter->second.addCandidate(
        { tx, transactionID, feeLevelPaid, flags, pfresult });
    /* Normally we defer figuring out the consequences until
        something later requires us to, but if we did, save
        the result for later.
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

void
TxQ::processValidatedLedger(Application& app,
    OpenView const& view, bool timeLeap)
{
    auto const allowEscalation =
        (view.rules().enabled(featureFeeEscalation,
            app.config().features));
    if (!allowEscalation)
    {
        return;
    }

    feeMetrics_.update(app, view, timeLeap);

    auto ledgerSeq = view.info().seq;

    std::lock_guard<std::mutex> lock(mutex_);

    if (!timeLeap)
        maxSize_ = feeMetrics_.getTxnsExpected() * setup_.ledgersInQueue;

    // Remove any queued candidates whose LastLedgerSequence has gone by.
    // Stop if we leave maxSize_ candidates.
    size_t keptCandidates = 0;
    auto candidateIter = byFee_.begin();
    while (candidateIter != byFee_.end()
        && (!maxSize_ || keptCandidates < *maxSize_))
    {
        if (candidateIter->lastValid
            && *candidateIter->lastValid <= ledgerSeq)
        {
            candidateIter = erase(candidateIter);
        }
        else
        {
            ++keptCandidates;
            ++candidateIter;
        }
    }
    // Erase any candidates more than maxSize_.
    // This can help keep the queue from getting overfull.
    while (candidateIter != byFee_.end())
        candidateIter = erase(candidateIter);

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

    /* Move transactions from the queue from largest fee to smallest.
       As we add more transactions, the required fee will increase.
       Stop when the transaction fee gets lower than the required fee.
    */

    auto ledgerChanged = false;

    std::lock_guard<std::mutex> lock(mutex_);

    for (auto candidateIter = byFee_.begin(); candidateIter != byFee_.end();)
    {
        if (candidateIter->sequence >
            byAccount_.at(candidateIter->account).transactions.begin()->first)
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
                isTelLocal(txnResult) || candidateIter->retriesRemaining <= 0)
            {
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
                --candidateIter->retriesRemaining;
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

    std::lock_guard<std::mutex> lock(mutex_);

    result.txCount = byFee_.size();
    result.txQMaxSize = maxSize_;
    result.txInLedger = view.txCount();
    result.txPerLedger = feeMetrics_.getTxnsExpected();
    result.referenceFeeLevel = feeMetrics_.baseLevel;
    result.minFeeLevel = isFull() ? byFee_.rbegin()->feeLevel + 1 :
        feeMetrics_.baseLevel;
    result.medFeeLevel = feeMetrics_.getEscalationMultiplier();
    result.expFeeLevel = feeMetrics_.scaleFeeLevel(view);

    return result;
}

Json::Value
TxQ::doRPC(Application& app) const
{
    using std::to_string;

    Json::Value ret(Json::objectValue);

    auto& levels = ret[jss::levels] = Json::objectValue;

    auto const view = app.openLedger().current();
    auto const metrics = getMetrics(*view);

    ret[jss::expected_ledger_size] = to_string(metrics.txPerLedger);
    ret[jss::current_ledger_size] = to_string(metrics.txInLedger);
    ret[jss::current_queue_size] = to_string(metrics.txCount);
    if (metrics.txQMaxSize)
        ret[jss::max_queue_size] = to_string(*metrics.txQMaxSize);

    levels[jss::reference_level] = to_string(metrics.referenceFeeLevel);
    levels[jss::minimum_level] = to_string(metrics.minFeeLevel);
    levels[jss::median_level] = to_string(metrics.medFeeLevel);
    levels[jss::open_ledger_level] = to_string(metrics.expFeeLevel);

    auto const baseFee = view->fees().base;
    auto& drops = ret[jss::drops] = Json::Value();

    // Don't care about the overflow flags
    drops[jss::base_fee] = to_string(mulDiv(
        metrics.referenceFeeLevel, baseFee,
            metrics.referenceFeeLevel).second);
    drops[jss::minimum_fee] = to_string(mulDiv(
        metrics.minFeeLevel, baseFee,
            metrics.referenceFeeLevel).second);
    drops[jss::median_fee] = to_string(mulDiv(
        metrics.medFeeLevel, baseFee,
            metrics.referenceFeeLevel).second);
    drops[jss::open_ledger_fee] = to_string(mulDiv(
        metrics.expFeeLevel, baseFee,
            metrics.referenceFeeLevel).second);

    return ret;
}

XRPAmount
TxQ::openLedgerFee(OpenView const& view) const
{
    auto metrics = getMetrics(view);

    // Don't care about the overflow flag
    return mulDiv(metrics.expFeeLevel,
        view.fees().base, metrics.referenceFeeLevel).second + 1;
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
    set(setup.minimumEscalationMultiplier, "minimum_escalation_multiplier", section);
    set(setup.minimumTxnInLedger, "minimum_txn_in_ledger", section);
    set(setup.minimumTxnInLedgerSA, "minimum_txn_in_ledger_standalone", section);
    set(setup.targetTxnInLedger, "target_txn_in_ledger", section);
    std::uint32_t max;
    if (set(max, "maximum_txn_in_ledger", section))
        setup.maximumTxnInLedger.emplace(max);
    setup.standAlone = config.RUN_STANDALONE;
    return setup;
}


std::unique_ptr<TxQ>
make_TxQ(TxQ::Setup const& setup, beast::Journal j)
{
    return std::make_unique<TxQ>(setup, std::move(j));
}

} // ripple
