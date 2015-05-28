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

#include <ripple/app/ledger/TxHold.h>
#include <ripple/app/tx/TransactionEngine.h>
#include <ripple/app/ledger/LedgerConsensus.h>
#include <numeric>

namespace ripple {

class CandidateTxn
{
    friend class TxQImpl;

protected:
    boost::intrusive::set_member_hook<> byFee_;

private:
    STTx::pointer txn_;

     uint64_t      feeLevel_;
     uint32_t      lastRelayed_;
     uint256       txID_;
     boost::optional<uint256> priorTxID_;
     Account       account_;
     boost::optional<uint32_t> lastValid_;
     uint32_t      sequence_;
     TransactionEngineParams transactionParams_;

public:
    CandidateTxn (STTx::ref, TransactionEngine& engine,
        std::uint32_t loadBase, TransactionEngineParams params);

    STTx::pointer getTransaction()
    {
        return txn_;
    }

    uint64_t getFeeLevel() const
    {
        return feeLevel_;
    }

    uint32_t getSequence() const
    {
        return sequence_;
    }
};

CandidateTxn::CandidateTxn(
    STTx::ref txn,
    TransactionEngine& engine,
    std::uint32_t loadBase,
    TransactionEngineParams params)
    : txn_(txn)
    , lastRelayed_(0), txID_(txn->getTransactionID())
    , account_(txn->getSourceAccount().getAccountID())
    , sequence_(txn->getSequence())
    , transactionParams_(params)
{
    feeLevel_ = txn->getFeeLevelPaid(loadBase, engine.getLedger()->getBaseFee());

    if (txn->isFieldPresent(sfLastLedgerSequence))
    {
        lastValid_ = txn->getFieldU32(sfLastLedgerSequence);
    }

    if (txn->isFieldPresent(sfAccountTxnID))
    {
        priorTxID_ = txn->getFieldH256(sfAccountTxnID);
    }
}

class TxQAccount
{
    friend class TxQImpl;

protected:
    //boost::intrusive::set_member_hook<> byAccount_;

private:

    Account                    account_;
    //uint64_t                   feeLevel_;
    uint64_t                   totalFees_;
    // Sequence number will be used as the key.
    std::map <uint32_t, CandidateTxn> transactions_;

public:
    TxQAccount(STTx::ref txn);
    TxQAccount(const Account& account);

    //int getFeeLevel() const
    //{
    //    return feeLevel_;
    //}

    int getTxnCount() const
    {
        return transactions_.size();
    }

    bool empty() const
    {
        return !getTxnCount();
    }

    // Returns true if this is the new head transaction
    // for this account
    CandidateTxn&  addCandidate (CandidateTxn&&);
    bool removeCandidate(uint32_t const&);

    std::pair<bool, CandidateTxn const*> findCandidateAt(uint32_t const&);
};

TxQAccount::TxQAccount(STTx::ref txn)
    :TxQAccount(txn->getFieldAccount160(sfAccount))
{
}

TxQAccount::TxQAccount(const Account& account)
    : account_(account)
    //, feeLevel_(0)
    , totalFees_(0)
{ 
}

CandidateTxn&
TxQAccount::addCandidate(CandidateTxn&& txn)
{
    auto fee = txn.getFeeLevel();
    totalFees_ += fee;
    auto sequence = txn.getSequence();

    transactions_.emplace(std::make_pair(sequence, std::move(txn)));
    assert(&transactions_.at(sequence) != &txn);

    return transactions_.at(sequence);
}

bool
TxQAccount::removeCandidate(uint32_t const& sequence)
{
    return transactions_.erase(sequence) != 0;
}

std::pair<bool, CandidateTxn const*>
TxQAccount::findCandidateAt(uint32_t const& sequence)
{
    auto iter = transactions_.find(sequence);
    if (iter == transactions_.end())
        return std::make_pair(false, nullptr);

    return std::make_pair(true, &iter->second);
}


class GreaterFee
{
public:
    bool operator() (const CandidateTxn& lhs, const CandidateTxn& rhs) const
    {
        return lhs.getFeeLevel() > rhs.getFeeLevel();
    }
};

/*
class LessAccount
{
public:
    bool operator() (const TxQAccount& lhs, const TxQAccount& rhs) const
    {
        return lhs.getAccount() < rhs.getAccount();
    }
};

class CompareAccount
{
public:
    bool operator() (const Account& lhs, const Account& rhs) const
    {
        return lhs == rhs;
    }
};
*/

class TxQImpl : public TxQ
{
public:

    TxQImpl(Setup const& setup, LoadFeeTrack &lft,
        beast::Journal journal)
        : setup_(setup)
        , journal_(journal)
        , loadFeeTrack_(lft)
    { }

    virtual ~TxQImpl()
    {
        byFee_.clear();
    }

    std::pair <TxDisposition, TER>
        addTransaction (
            STTx::ref txn,
            TransactionEngineParams params,
            TransactionEngine& engine) override;

    void fillOpenLedger(TransactionEngine&) override;

    void processValidatedLedger (Ledger::ref) override;

    struct TxQ::TxFeeMetrics getFeeMetrics () override;

    Json::Value doRPC(Json::Value const& query) override;

protected:

    using FeeHook = boost::intrusive::member_hook
        <CandidateTxn, boost::intrusive::set_member_hook<>,
        &CandidateTxn::byFee_>;

    using FeeMultiSet = boost::intrusive::multiset
        < CandidateTxn, FeeHook, boost::intrusive::compare <GreaterFee> >;

    using AccountSet = std::map < Account, TxQAccount > ;

    const Setup setup_;
    beast::Journal journal_;
    
    LoadFeeTrack& loadFeeTrack_;
    FeeMultiSet   byFee_;
    AccountSet    byAccount_;
    boost::optional<size_t> maxSize_;

    // Used to compute maxSize_
    std::map<uint32_t, size_t> ledgerTransactionCounts_;

    std::mutex mutable mutex_;

private:
    bool isFull() const
    {
        return maxSize_.is_initialized() && byFee_.size() >= maxSize_.value();
    }

    bool canBeHeld(STTx::ref);

    FeeMultiSet::iterator_type erase(FeeMultiSet::const_iterator_type);

};

TxQ::Setup
setup_TxQ(Config const& config)
{
    TxQ::Setup setup;
    auto const& section = config.section("transaction_queue");
    set(setup.ledgersInQueue_, "ledgers_in_queue", section);
    set(setup.minLedgersToComputeSizeLimit_, "min_ledgers_to_compute_size_limit",
        section);
    set(setup.maxLedgerCountsToStore_, "max_ledger_counts_to_store", section);
    return setup;
}


std::unique_ptr<TxQ>
make_TxQ(TxQ::Setup const& setup, 
    LoadFeeTrack& lft, beast::Journal journal)
{
    return std::make_unique<TxQImpl>(setup, lft, std::move(journal));
}

const TER TxQ::txnResultHeld = terQUEUED;
const TER TxQ::txnResultLowFee = telINSUF_FEE_P;

bool TxQImpl::canBeHeld (STTx::ref tx)
{
    // PreviousTxnID is deprecated and should never be used
    // AccountTxnID is not supported by the transaction
    // queue yet, but should be added in the future
    bool canBeHeld =
        ! tx->isFieldPresent (sfPreviousTxnID) &&
        ! tx->isFieldPresent (sfAccountTxnID);
    if (canBeHeld)
    {
        auto accountIter = byAccount_.find(tx->getFieldAccount160(sfAccount));
        canBeHeld = accountIter == byAccount_.end()
            || accountIter->second.empty();
    }
    return canBeHeld;
}

TxQImpl::FeeMultiSet::iterator_type
TxQImpl::erase(TxQImpl::FeeMultiSet::const_iterator_type candidateIter)
{
    auto& txQAccount = byAccount_.at(candidateIter->account_);
    auto sequence = candidateIter->sequence_;
    auto newCandidateIter = byFee_.erase(candidateIter);
    // Now that the candidate has been removed from the 
    // intrusive list remove it from the TxQAccount
    // so the memory can be freed.
    auto found = txQAccount.removeCandidate(sequence);
    (void)found;
    assert(found);

    return newCandidateIter;
}

std::pair <TxQ::TxDisposition, TER>
    TxQImpl::addTransaction (
        STTx::ref txn,
        TransactionEngineParams params,
        TransactionEngine& engine)
{
    auto loadBase = loadFeeTrack_.getLoadBase();
    auto account = txn->getFieldAccount160(sfAccount);
    auto fee_level = txn->getFeeLevelPaid
        (loadBase, engine.getLedger()->getBaseFee());

    if (fee_level < loadBase)
    {
        // This transaction can never succeed. Don't even bother.
        journal_.trace << "Transaction for " << account
            << " has fee level " << fee_level 
            << " which is below minimum of " << loadBase;
        return { TD_low_fee, txnResultLowFee };
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Is there a transaction for the same account with the
    // same sequence number already in the queue?
    auto accountIter = byAccount_.find(account);
    if (accountIter != byAccount_.end())
    {
        auto sequence = txn->getSequence();
        auto& txQAcct = accountIter->second;
        auto existingCandidate =
            txQAcct.findCandidateAt(sequence);
        if (existingCandidate.first)
        {
            // Is the current transaction's fee higher than
            // the queued transaction's fee?
            journal_.trace << "Found transaction in queue for account "
                << account << " with sequence number " << sequence
                << " new txn fee level is " << fee_level
                << " old txn fee level is "
                << existingCandidate.second->feeLevel_;
            if (fee_level > existingCandidate.second->feeLevel_)
            {
                // Remove the queued transaction and continue
                journal_.trace
                    << "Removing transaction from queue "
                    << existingCandidate.second->txID_
                    << " in favor of "
                    << txn->getTransactionID();
                auto byFeeIter = byFee_.iterator_to(*existingCandidate.second);
                assert(byFeeIter != byFee_.end());
                assert(existingCandidate.second == &*byFeeIter);
                assert(byFeeIter->sequence_ == sequence);
                assert(byFeeIter->account_ == txQAcct.account_);
                erase(byFeeIter);
            }
            else
            {
                // Drop the current transaction
                journal_.trace
                    << "Ignoring transaction "
                    << txn->getTransactionID()
                    << " in favor of queued "
                    << existingCandidate.second->txID_;
                return { TD_low_fee, txnResultLowFee };
            }
        }
    }

    auto requiredFeeLevel = loadFeeTrack_.scaleTxnFee(loadBase);
    journal_.trace << "Transaction " << txn->getTransactionID()
        << " from account " << account
        << " has fee level of " << fee_level
        << " needs at least " << requiredFeeLevel;

    // Can transaction go in open ledger?
    if (fee_level >= requiredFeeLevel)
    {
        // Transaction fee is sufficient to go in open ledger immediately

        // TODO EHENNIS Check for dependencies in the hold queue. Return
        //              TD_missing_prior if any.

        journal_.trace << "Applying transaction "
            << txn->getTransactionID()
            << " to open ledger.";
        ripple::TER txnResult;
        bool didApply;
        std::tie(txnResult, didApply) = engine.applyTransaction(*txn,
            params);

        if (didApply)
        {
            journal_.trace << "Transaction "
                << txn->getTransactionID()
                << " applied successfully.";
            loadFeeTrack_.onTx(txn->getTransactionFee().mantissa());

            return { TD_open_ledger, txnResult };
        }
        // failure
        if (journal_.trace)
        {
            journal_.trace << "Transaction "
                << txn->getTransactionID()
                << " failed with " << transToken(txnResult);
        }
        if (isTemMalformed(txnResult))
        {
            return { TD_malformed, txnResult };
        }

        return { TD_failed, txnResult };
    }

    if (! canBeHeld (txn))
    {
        // Bail, transaction cannot be held
        journal_.trace << "Transaction "
            << txn->getTransactionID()
            << " can not be held";
        return { TD_low_fee, txnResultLowFee };
    }

    // Are preconditions met?

    // Is the queue full?
    // It's pretty unlikely that the queue will be "overfilled",
    // but should it happen, take the opportunity to fix it now.
    while (isFull())
    {
        auto lastRIter = byFee_.rbegin();
        if (fee_level > lastRIter->feeLevel_)
        {
            // The queue is full, and this transaction is more
            // valuable, so kick out the cheapest transaction.
            journal_.trace
                << "Removing end item from queue with fee of"
                << lastRIter->feeLevel_ << " in favor of "
                << txn->getTransactionID() << " with fee of "
                << fee_level;
            auto lastFIter = byFee_.iterator_to(*lastRIter);
            erase(lastFIter);
        }
        else
        {
            journal_.trace << "Queue is full, and transaction "
                << txn->getTransactionID()
                << " fee is lower than end item";
            return { TD_low_fee, txnResultLowFee };
        }
    }

    {
        // see if the transaction can get into the ledger/engine,
        // but don't return success, because there's no point if
        // it has no hope of success.
        ripple::TER txnResult;
        bool didApply;
        std::tie(txnResult, didApply) = engine.applyTransaction(*txn,
            params | tapIGNORE_FEE);

        if (!didApply && !isTerRetry(txnResult))
        {
            if (journal_.trace)
            {
                journal_.trace << "Not adding transaction "
                    << txn->getTransactionID()
                    << " to queue. Fails with " << transToken(txnResult);
            }
            return { TD_malformed, txnResult };
        }
    }

    // Hold the transaction
    if (accountIter == byAccount_.end())
    {
        // Create a new TxQAccount object and add the byAccount lookup.
        byAccount_.emplace(
            std::make_pair(account, TxQAccount(txn)));
        auto& candidate = byAccount_.at(account).addCandidate(
            { txn, engine, loadBase, params });
        // Then index it into the byFee lookup.
        byFee_.insert(candidate);
        journal_.debug << "Added transaction " << candidate.txID_
            << " from new account " << candidate.account_
            << " to queue.";
    }
    else
    {
        auto& candidate = accountIter->second.addCandidate(
            { txn, engine, loadBase, params });
        byFee_.insert(candidate);
        journal_.debug << "Added transaction " << candidate.txID_
            << " from existing account " << candidate.account_
            << " to queue.";
    }

    return { TD_held, txnResultHeld };
}

void TxQImpl::processValidatedLedger (Ledger::ref validatedLedger)
{
    auto ledgerSeq = validatedLedger->getLedgerSeq();
    auto ledgerSize = countLedgerNodes(validatedLedger);

    std::lock_guard<std::mutex> lock(mutex_);

    // Empty ledgers aren't interesting.
    if (ledgerSize > 0)
    {
        journal_.debug << "Ledger number " << ledgerSeq
            << " has " << ledgerSize << " transactions";
        ledgerTransactionCounts_.emplace(std::make_pair(ledgerSeq, ledgerSize));

        if (ledgerTransactionCounts_.size() %
            setup_.minLedgersToComputeSizeLimit_ == 0)
        {
            auto total = std::accumulate(
                ledgerTransactionCounts_.begin(),
                ledgerTransactionCounts_.end(),
                (size_t)0,
                [] (const size_t&a, const std::pair<uint32_t, size_t>&b)
                {
                    return a + b.second;
                });
            maxSize_ = total * setup_.ledgersInQueue_ /
                ledgerTransactionCounts_.size();
            journal_.debug << "Changed queue maxsize to "
                << *maxSize_;

            while (ledgerTransactionCounts_.size() > 
                setup_.maxLedgerCountsToStore_)
            {
                ledgerTransactionCounts_.erase(ledgerTransactionCounts_.begin());
            }
        }
    }

    // Remove any queued candidates whos LastLedgerSequence has gone by.
    // Stop if we leave maxSize_ candidates.
    size_t keptCandidates = 0;
    auto candidateIter = byFee_.begin();
    while (candidateIter != byFee_.end()
        && (!maxSize_ || keptCandidates < maxSize_.value()))
    {
        if (candidateIter->lastValid_.is_initialized()
            && candidateIter->lastValid_.value() >= ledgerSeq)
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
    {
        candidateIter = erase(candidateIter);
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

void TxQImpl::fillOpenLedger(TransactionEngine& engine)
{
    /* Move transactions from the queue from largest fee to smallest.
       As we add more transactions, the required fee will increase.
       Stop when the transaction fee gets lower than the required fee.
    */

#ifndef NDEBUG
    auto lastFeeLevel = boost::optional<uint32>();
#endif

    auto loadBase = loadFeeTrack_.getLoadBase();

    std::lock_guard<std::mutex> lock(mutex_);

    for (auto candidateIter = byFee_.begin(); candidateIter != byFee_.end();)
    {
        auto requiredFee = loadFeeTrack_.scaleTxnFee(loadBase);
        auto txnFeeLevel = candidateIter->feeLevel_;
#ifndef NDEBUG
        assert(!lastFeeLevel || *lastFeeLevel >= txnFeeLevel);
        lastFeeLevel = txnFeeLevel;
#endif
        journal_.trace << "Queued transaction " << candidateIter->txID_
            << " from account " << candidateIter->account_
            << " has fee level of " << txnFeeLevel
            << " needs at least " << requiredFee;
        if (txnFeeLevel >= requiredFee)
        {
            auto firstTxn = candidateIter->getTransaction();

            journal_.trace << "Applying queued transaction "
                << candidateIter->txID_
                << " to open ledger.";
            // first approximation based on LedgerMasterImp::doTransaction
            TER txnResult;
            bool didApply;
            std::tie(txnResult, didApply) = engine.applyTransaction(*firstTxn,
                candidateIter->transactionParams_);

            if (didApply)
            {
                // Remove the candidate from the queue
                journal_.trace << "Queued transaction "
                    << candidateIter->txID_
                    << " applied successfully. Remove from queue.";
                loadFeeTrack_.onTx(firstTxn->getTransactionFee().mantissa());

                candidateIter = erase(candidateIter);
            }
            else if (isTefFailure(txnResult) || isTemMalformed(txnResult) ||
                isTelLocal(txnResult))
            {
                if (journal_.trace)
                {
                    journal_.trace << "Queued transaction "
                        << candidateIter->txID_
                        << " failed with " << transToken(txnResult)
                        << ". Remove from queue.";
                }
                candidateIter = erase(candidateIter);
            }
            else
            {
                if (journal_.trace)
                {
                    journal_.trace << "Transaction "
                        << candidateIter->txID_
                        << " failed with " << transToken(txnResult)
                        << ". Leave in queue.";
                }
                candidateIter++;
            }

        }
        else
        {
            break;
        }
    }
}

TxQ::TxFeeMetrics TxQImpl::getFeeMetrics ()
{
    TxFeeMetrics result;

    auto base = loadFeeTrack_.getLoadBase();

    std::lock_guard<std::mutex> lock(mutex_);

    result.txCount = byFee_.size();
    result.txPerLedger = loadFeeTrack_.getExpectedLedgerSize();
    result.referenceFeeLevel = base;
    if (isFull())
    {
        auto lastRIter = byFee_.rbegin();
        result.minFeeLevel = lastRIter->feeLevel_ + 1;
    }
    else
    {
        result.minFeeLevel = base;
    }
    result.medFeeLevel = loadFeeTrack_.getMedianFee();
    result.expFeeLevel = loadFeeTrack_.scaleTxnFee(base);

    return std::move(result);
}

Json::Value TxQImpl::doRPC(Json::Value const& query)
{
    using std::to_string;

    Json::Value ret(Json::objectValue);

    auto& levels = ret[jss::levels] = Json::objectValue;

    auto metrics = getFeeMetrics();

    levels[jss::expected_ledger_size] = to_string(metrics.txPerLedger);
    levels[jss::reference_level] = to_string(metrics.referenceFeeLevel);
    levels[jss::minimum_level] = to_string(metrics.minFeeLevel);
    levels[jss::median_level] = to_string(metrics.medFeeLevel);
    levels[jss::open_ledger_level] = to_string(metrics.expFeeLevel);

    return ret;
}

} // ripple
