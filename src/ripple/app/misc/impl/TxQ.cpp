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
    return mulDivNoThrow(tx[sfFee].xrp().drops(),
        baseRefLevel * referenceFee,
            refTxnCostDrops * requiredFee);
}

//////////////////////////////////////////////////////////////////////////

namespace detail {

std::size_t
FeeMetrics::updateFeeMetrics(Application& app,
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

    JLOG(j_.debug) << "Ledger " << view.info().seq <<
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
            mimimumTx, targetTxnCount_ - 1);
    }
    else if (feeLevels.size() > txnsExpected ||
        feeLevels.size() > targetTxnCount_)
    {
        // Ledgers are processing in a timely manner,
        // so keep the limit high, but don't let it
        // grow without bound.
        txnsExpected = feeLevels.size();
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
    JLOG(j_.debug) << "Expected transactions updated to " <<
        txnsExpected << " and multiplier updated to " <<
        escalationMultiplier;

    std::lock_guard <std::mutex> sl(lock_);
    txnsExpected_ = txnsExpected;
    escalationMultiplier_ = escalationMultiplier;

    return size;
}

std::uint64_t
FeeMetrics::scaleFeeLevel(OpenView const& view) const
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
        fee = mulDivNoThrow(fee, current * current *
            multiplier, target * target);
    }

    return fee;
}

} // detail

TxQ::CandidateTxn::CandidateTxn(
    std::shared_ptr<STTx const> const& txn_,
        TxID const& txID_, std::uint64_t feeLevel_,
            ApplyFlags const flags_,
                PreflightResult const& pfresult_)
    : txn(txn_)
    , feeLevel(feeLevel_)
    , txID(txID_)
    , account(txn_->getAccountID(sfAccount))
    , sequence(txn_->getSequence())
    , flags(flags_)
    , pfresult(pfresult_)
{
    if (txn->isFieldPresent(sfLastLedgerSequence))
        lastValid = txn->getFieldU32(sfLastLedgerSequence);

    if (txn->isFieldPresent(sfAccountTxnID))
        priorTxID = txn->getFieldH256(sfAccountTxnID);
}

TxQ::TxQAccount::TxQAccount(std::shared_ptr<STTx const> const& txn)
    :TxQAccount(txn->getAccountID(sfAccount))
{
}

TxQ::TxQAccount::TxQAccount(const AccountID& account_)
    : account(account_)
    , totalFees(0)
{
}

auto
TxQ::TxQAccount::addCandidate(CandidateTxn&& txn)
    -> CandidateTxn&
{
    auto fee = txn.feeLevel;
    totalFees += fee;
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

auto
TxQ::TxQAccount::findCandidateAt(TxSeq const& sequence) const
    -> CandidateTxn const*
{
    auto iter = transactions.find(sequence);
    return iter == transactions.end() ?
        nullptr : &iter->second;
}

//////////////////////////////////////////////////////////////////////////

TxQ::TxQ(Setup const& setup,
    beast::Journal j)
    : setup_(setup)
    , j_(j)
    , feeMetrics_(setup.standAlone, j)
    , maxSize_(boost::none)
{
}

TxQ::~TxQ()
{
    byFee_.clear();
}

/** Used by tests only.
*/
std::size_t
TxQ::setMinimumTx(int m)
{
    return feeMetrics_.setMinimumTx(m);
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
    if (canBeHeld)
    {
        auto accountIter = byAccount_.find(tx->getAccountID(sfAccount));
        canBeHeld = accountIter == byAccount_.end()
            || accountIter->second.empty();
    }
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

std::pair<TER, bool>
TxQ::apply(Application& app, OpenView& view,
    std::shared_ptr<STTx const> const& tx,
        ApplyFlags flags, beast::Journal j)
{
    auto const allowEscalation =
        (flags & tapENABLE_TESTING) ||
            (view.rules().enabled(featureFeeEscalation,
                app.config().features));
    if (!allowEscalation)
    {
        return ripple::apply(app, view, *tx, flags, j);
    }

    auto const account = (*tx)[sfAccount];
    auto currentSeq = true;

    std::lock_guard<std::mutex> lock(mutex_);

    // If there are other transactions in the queue
    // for this account, account for that before the pre-checks,
    // so we don't get a false terPRE_SEQ.
    auto accountIter = byAccount_.find(account);
    if (accountIter != byAccount_.end())
    {
        auto const sle = view.read(
            keylet::account(account));

        if (sle)
        {
            auto const& txQAcct = accountIter->second;
            auto const t_seq = tx->getSequence();
            auto const a_seq = sle->getFieldU32(sfSequence);

            currentSeq = a_seq == t_seq;
            for (auto seq = a_seq; !currentSeq && seq < t_seq; ++seq)
            {
                auto const existingCandidate =
                    txQAcct.findCandidateAt(seq);
                currentSeq = !existingCandidate;
            }
        }
    }

    // See if the transaction is likely to claim a fee.
    auto const pfresult = preflight(app, view.rules(),
        *tx, flags | (currentSeq ? tapNONE: tapPOST_SEQ), j);
    auto const pcresult = preclaim(pfresult, app, view);
    if (!pcresult.likelyToClaimFee)
        return{ pcresult.ter, false };

    auto const baseFee = pcresult.baseFee;
    auto const feeLevelPaid = getFeeLevelPaid(*tx,
        feeMetrics_.baseLevel, baseFee);
    auto const requiredFeeLevel = feeMetrics_.scaleFeeLevel(view);
    auto const transactionID = tx->getTransactionID();

    // Too low of a fee should get caught by preclaim
    assert(feeLevelPaid >= feeMetrics_.baseLevel);

    // Is there a transaction for the same account with the
    // same sequence number already in the queue?
    if (accountIter != byAccount_.end())
    {
        auto const sequence = tx->getSequence();
        auto& txQAcct = accountIter->second;
        auto existingCandidate = txQAcct.findCandidateAt(sequence);
        if (existingCandidate)
        {
            // Is the current transaction's fee higher than
            // the queued transaction's fee?
            auto requiredRetryLevel = mulDivNoThrow(
                existingCandidate->feeLevel,
                setup_.retrySequencePercent, 100);
            JLOG(j_.trace) << "Found transaction in queue for account " <<
                account << " with sequence number " << sequence <<
                " new txn fee level is " << feeLevelPaid <<
                ", old txn fee level is " <<
                existingCandidate->feeLevel <<
                ", new txn needs fee level of " <<
                requiredRetryLevel;
            if (feeLevelPaid > requiredRetryLevel
                || (existingCandidate->feeLevel < requiredFeeLevel &&
                    feeLevelPaid >= requiredFeeLevel))
            {
                // The fee is high enough to either retry or
                // the prior txn could not get into the open ledger,
                //  but this one can.
                // Remove the queued transaction and continue
                JLOG(j_.trace) <<
                    "Removing transaction from queue " <<
                    existingCandidate->txID <<
                    " in favor of " << transactionID;
                auto byFeeIter = byFee_.iterator_to(*existingCandidate);
                assert(byFeeIter != byFee_.end());
                assert(existingCandidate == &*byFeeIter);
                assert(byFeeIter->sequence == sequence);
                assert(byFeeIter->account == txQAcct.account);
                erase(byFeeIter);
            }
            else
            {
                // Drop the current transaction
                JLOG(j_.trace) <<
                    "Ignoring transaction " <<
                    transactionID <<
                    " in favor of queued " <<
                    existingCandidate->txID;
                return { telINSUF_FEE_P, false };
            }
        }
    }

    JLOG(j_.trace) << "Transaction " <<
        transactionID <<
        " from account " << account <<
        " has fee level of " << feeLevelPaid <<
        " needs at least " << requiredFeeLevel <<
        " to get in the open ledger, which has " <<
        view.txCount() << " entries.";

    // Can transaction go in open ledger?
    if (currentSeq && feeLevelPaid >= requiredFeeLevel)
    {
        // Transaction fee is sufficient to go in open ledger immediately

        JLOG(j_.trace) << "Applying transaction " <<
            transactionID <<
            " to open ledger.";
        ripple::TER txnResult;
        bool didApply;

        std::tie(txnResult, didApply) = doApply(pcresult, app, view);

        JLOG(j_.trace) << "Transaction " <<
            transactionID <<
                (didApply ? " applied successfully with " :
                    " failed with ") <<
                        transToken(txnResult);

        return { txnResult, didApply };
    }

    if (! canBeHeld(tx))
    {
        // Bail, transaction cannot be held
        JLOG(j_.trace) << "Transaction " <<
            transactionID <<
            " can not be held";
        return { feeLevelPaid >= requiredFeeLevel ?
            terPRE_SEQ : telINSUF_FEE_P, false };
    }

    // It's pretty unlikely that the queue will be "overfilled",
    // but should it happen, take the opportunity to fix it now.
    while (isFull())
    {
        auto lastRIter = byFee_.rbegin();
        if (feeLevelPaid > lastRIter->feeLevel)
        {
            // The queue is full, and this transaction is more
            // valuable, so kick out the cheapest transaction.
            JLOG(j_.warning) <<
                "Removing end item from queue with fee of" <<
                lastRIter->feeLevel << " in favor of " <<
                transactionID << " with fee of " <<
                feeLevelPaid;
            erase(byFee_.iterator_to(*lastRIter));
        }
        else
        {
            JLOG(j_.warning) << "Queue is full, and transaction " <<
                transactionID <<
                " fee is lower than end item";
            return { telINSUF_FEE_P, false };
        }
    }

    // Hold the transaction.
    // accountIter was already set when looking for duplicate seq.
    std::string op = "existing";
    if (accountIter == byAccount_.end())
    {
        // Create a new TxQAccount object and add the byAccount lookup.
        bool created;
        std::tie(accountIter, created) = byAccount_.emplace(
            account, TxQAccount(tx));
        (void)created;
        assert(created);
        op = "new";
    }
    auto& candidate = accountIter->second.addCandidate(
    { tx, transactionID, feeLevelPaid, flags, pfresult });
    // Then index it into the byFee lookup.
    byFee_.insert(candidate);
    JLOG(j_.debug) << "Added transaction " << candidate.txID <<
        " from " << op << " account " << candidate.account <<
        " to queue.";

    return { terQUEUED, false };
}

void
TxQ::processValidatedLedger(Application& app,
    OpenView const& view, bool timeLeap,
        ApplyFlags flags)
{
    auto const allowEscalation =
        (flags & tapENABLE_TESTING) ||
        (view.rules().enabled(featureFeeEscalation,
            app.config().features));
    if (!allowEscalation)
    {
        return;
    }

    feeMetrics_.updateFeeMetrics(app, view, timeLeap);

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
    for (; candidateIter != byFee_.end(); ++candidateIter)
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
    OpenView& view, ApplyFlags flags)
{
    auto const allowEscalation =
        (flags & tapENABLE_TESTING) ||
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
        auto const requiredFeeLevel = feeMetrics_.scaleFeeLevel(view);
        auto const feeLevelPaid = candidateIter->feeLevel;
        JLOG(j_.trace) << "Queued transaction " <<
            candidateIter->txID << " from account " <<
            candidateIter->account << " has fee level of " <<
            feeLevelPaid << " needs at least " <<
            requiredFeeLevel;
        if (feeLevelPaid >= requiredFeeLevel)
        {
            auto firstTxn = candidateIter->txn;

            JLOG(j_.trace) << "Applying queued transaction " <<
                candidateIter->txID << " to open ledger.";

            // If the rules or flags change, preflight again
            assert(candidateIter->pfresult);
            if (candidateIter->pfresult->rules != view.rules() ||
                candidateIter->pfresult->flags != candidateIter->flags)
            {
                candidateIter->pfresult.emplace(
                    preflight(app, view.rules(),
                        candidateIter->pfresult->tx,
                            candidateIter->flags,
                                candidateIter->pfresult->j));
            }

            auto pcresult = preclaim(
                *candidateIter->pfresult, app, view);

            TER txnResult;
            bool didApply;
            std::tie(txnResult, didApply) = doApply(pcresult, app, view);

            if (didApply)
            {
                // Remove the candidate from the queue
                JLOG(j_.debug) << "Queued transaction " <<
                    candidateIter->txID <<
                    " applied successfully. Remove from queue.";

                candidateIter = erase(candidateIter);
                ledgerChanged = true;
            }
            else if (isTefFailure(txnResult) || isTemMalformed(txnResult) ||
                isTelLocal(txnResult))
            {
                JLOG(j_.debug) << "Queued transaction " <<
                    candidateIter->txID << " failed with " <<
                    transToken(txnResult) << ". Remove from queue.";
                candidateIter = erase(candidateIter);
            }
            else
            {
                JLOG(j_.debug) << "Transaction " <<
                    candidateIter->txID << " failed with " <<
                    transToken(txnResult) << ". Leave in queue.";
                candidateIter++;
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

    drops[jss::base_fee] = to_string(mulDivNoThrow(
        metrics.referenceFeeLevel, baseFee,
            metrics.referenceFeeLevel));
    drops[jss::minimum_fee] = to_string(mulDivNoThrow(
        metrics.minFeeLevel, baseFee,
            metrics.referenceFeeLevel));
    drops[jss::median_fee] = to_string(mulDivNoThrow(
        metrics.medFeeLevel, baseFee,
            metrics.referenceFeeLevel));
    drops[jss::open_ledger_fee] = to_string(mulDivNoThrow(
        metrics.expFeeLevel, baseFee,
            metrics.referenceFeeLevel));

    return ret;
}

XRPAmount
TxQ::openLedgerFee(OpenView const& view) const
{
    auto metrics = getMetrics(view);

    return mulDivNoThrow(metrics.expFeeLevel,
        view.fees().base, metrics.referenceFeeLevel) + 1;
}

//////////////////////////////////////////////////////////////////////////

TxQ::Setup
setup_TxQ(Config const& config)
{
    TxQ::Setup setup;
    auto const& section = config.section("transaction_queue");
    set(setup.ledgersInQueue, "ledgers_in_queue", section);
    set(setup.retrySequencePercent, "retry_sequence_percent", section);
    setup.standAlone = config.RUN_STANDALONE;
    return setup;
}


std::unique_ptr<TxQ>
make_TxQ(TxQ::Setup const& setup, beast::Journal j)
{
    return std::make_unique<TxQ>(setup, std::move(j));
}

} // ripple
