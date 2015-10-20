//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-14 Ripple Labs Inc.

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

#ifndef RIPPLE_TXQ_H_INCLUDED
#define RIPPLE_TXQ_H_INCLUDED

#include <ripple/app/tx/applySteps.h>
#include <ripple/ledger/OpenView.h>
#include <ripple/ledger/ApplyView.h>
#include <ripple/core/Config.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/protocol/TER.h>
#include <ripple/protocol/STTx.h>
#include <boost/intrusive/set.hpp>

namespace ripple {

class Application;

namespace detail {

class FeeMetrics
{
private:
    // Fee escalation

    // Limit of the txnsExpected value after a
    // time leap.
    std::size_t const targetTxnCount_;
    // Minimum value of txnsExpected.
    std::size_t minimumTxnCount_;
    // Number of transactions expected per ledger.
    // One more than this value will be accepted
    // before escalation kicks in.
    std::size_t txnsExpected_;
    // Minimum value of escalationMultiplier.
    std::uint32_t const minimumMultiplier_;
    // Based on the median fee of the LCL. Used
    // when fee escalation kicks in.
    std::uint32_t escalationMultiplier_;
    beast::Journal j_;

    std::mutex mutable lock_;

public:
    static const std::uint64_t baseLevel = 256;

public:
    FeeMetrics(bool standAlone, beast::Journal j)
        : targetTxnCount_(50)
        , minimumTxnCount_(standAlone ? 1000 : 5)
        , txnsExpected_(minimumTxnCount_)
        , minimumMultiplier_(500)
        , escalationMultiplier_(minimumMultiplier_)
        , j_(j)
    {
    }

    /**
    Updates fee metrics based on the transactions in the ReadView
    for use in fee escalation calculations.

    @param view View of the LCL that was just closed or received.
    @param timeLeap Indicates that rippled is under load so fees
    should grow faster.
    */
    std::size_t
    updateFeeMetrics(Application& app,
        ReadView const& view, bool timeLeap);

    /** Used by tests only.
    */
    std::size_t
    setMinimumTx(int m)
    {
        std::lock_guard <std::mutex> sl(lock_);

        auto const old = minimumTxnCount_;
        minimumTxnCount_ = m;
        txnsExpected_ = m;
        return old;
    }

    std::size_t
    getTxnsExpected() const
    {
        std::lock_guard <std::mutex> sl(lock_);

        return txnsExpected_;
    }

    std::uint32_t
    getEscalationMultiplier() const
    {
        std::lock_guard <std::mutex> sl(lock_);

        return escalationMultiplier_;
    }

    std::uint64_t
    scaleFeeLevel(OpenView const& view) const;
};

}

/**
    Transaction Queue. Used to manage transactions in conjunction with
    fee escalation. See also: RIPD-598, and subissues
    RIPD-852, 853, and 854.

    Once enough transactions are added to the open ledger, the required
    fee will jump dramatically. If additional transactions are added,
    the fee will grow exponentially.

    Transactions that don't have a high enough fee to be applied to
    the ledger are added to the queue in order from highest fee to
    lowest. Whenever a new ledger is accepted as validated, transactions
    are first applied from the queue to the open ledger in fee order
    until either all transactions are applied or the fee again jumps
    too high for the remaining transactions.
*/
class TxQ
{
public:
    struct Setup
    {
        std::size_t ledgersInQueue = 20;
        std::uint32_t retrySequencePercent = 125;
        bool standAlone = false;
    };

    struct Metrics
    {
        std::size_t txCount;            // Transactions in the queue
        boost::optional<std::size_t> txQMaxSize;    // Max txns in queue
        std::size_t txInLedger;         // Amount currently in the ledger
        std::size_t txPerLedger;        // Amount expected per ledger
        std::uint64_t referenceFeeLevel;  // Reference transaction fee level
        std::uint64_t minFeeLevel;        // Minimum fee level to get in the queue
        std::uint64_t medFeeLevel;        // Median fee level of the last ledger
        std::uint64_t expFeeLevel;        // Estimated fee level to get in next ledger
    };

    TxQ(Setup const& setup,
        beast::Journal j);

    virtual ~TxQ();

    /**
        Add a new transaction to the open ledger, hold it in the queue,
        or reject it.

        How the decision is made:
        1. Is there already a transaction for the same account with the
           same sequence number in the queue?
            Yes: Is `txn`'s fee higher than the queued transaction's fee?
                Yes: Remove the queued transaction. Continue to step 2.
                No: Reject `txn` with a low fee TER code. Stop.
            No: Continue to step 2.
        2. Is the `txn`s fee level >= the required fee level?
            Yes: `txn` can be applied to the ledger. Pass it
                 to the engine and return that result.
            No: Can it be held in the queue? (See TxQImpl::canBeHeld).
                No: Reject `txn` with a low fee TER code.
                Yes: Is the queue full?
                    No: Put `txn` in the queue.
                    Yes: Is the `txn`'s fee higher than the end item's
                         fee?
                        Yes: Remove the end item, and add `txn`.
                        No: Reject `txn` with a low fee TER code.

        If the transaction is queued, addTransaction will return
        { TD_held, terQUEUED }


        @param txn The transaction to be attempted.
        @param params Flags to control engine behaviors.
        @param engine Transaction Engine.
    */
    std::pair<TER, bool>
    apply(Application& app, OpenView& view,
        std::shared_ptr<STTx const> const& tx,
            ApplyFlags flags, beast::Journal j);

    /**
        Fill the new open ledger with transactions from the queue.
        As we apply more transactions to the ledger, the required
        fee will increase.

        Iterate over the transactions from highest fee to lowest.
        For each transaction, compute the required fee.
        Is the transaction fee is less than the required fee?
            Yes: Stop. We're done.
            No: Try to apply the transaction. Did it apply?
                Yes: Take it out of the queue.
                No: Leave it in the queue, and continue iterating.

        @return Whether any txs were added to the view.
    */
    bool
    accept(Application& app, OpenView& view,
        ApplyFlags flags = tapNONE);

    /**
        We have a new last validated ledger, update and clean up the
        queue.

        1) Keep track of the average non-empty ledger size. Once there
            are enough data points, the maximum queue size will be
            enough to hold 20 ledgers. (Parameters for this are
            experimentally configurable, but should be left alone.)
            1a) If the new limit makes the queue full, trim excess
                transactions from the end of the queue.
        2) Remove any transactions from the queue whos the
            `LastLedgerSequence` has passed.

    */
    void
    processValidatedLedger(Application& app,
        OpenView const& view, bool timeLeap,
            ApplyFlags flags = tapNONE);

    /** Used by tests only.
    */
    std::size_t
    setMinimumTx(int m);

    /** Returns fee metrics in reference fee (level) units.
    */
    struct Metrics
    getMetrics(OpenView const& view) const;

    /** Packages up fee metrics for the `fee` RPC command.
    */
    Json::Value
    doRPC(Application& app) const;

    /** Return the instantaneous fee to get into the current
        open ledger for a reference transaction.
    */
    XRPAmount
    openLedgerFee(OpenView const& view) const;

private:
    class CandidateTxn
    {
    public:
        // Used by the TxQ::FeeHook and TxQ::FeeMultiSet below
        // to put each candidate object into more than one
        // set without copies, pointers, etc.
        boost::intrusive::set_member_hook<> byFeeListHook;

        std::shared_ptr<STTx const> txn;

        uint64_t const feeLevel;
        TxID const txID;
        boost::optional<TxID> priorTxID;
        AccountID const account;
        boost::optional<LedgerIndex> lastValid;
        TxSeq const sequence;
        ApplyFlags const flags;
        // pfresult_ is never allowed to be empty. The
        // boost::optional is leveraged to allow `emplace`d
        // construction and replacement without a copy
        // assignment operation.
        boost::optional<PreflightResult const> pfresult;

    public:
        CandidateTxn(std::shared_ptr<STTx const> const&,
            TxID const& txID, std::uint64_t feeLevel,
                ApplyFlags const flags,
                    PreflightResult const& pfresult);
    };

    class GreaterFee
    {
    public:
        bool operator()(const CandidateTxn& lhs, const CandidateTxn& rhs) const
        {
            return lhs.feeLevel > rhs.feeLevel;
        }
    };

    class TxQAccount
    {
    public:

        AccountID const account;
        uint64_t totalFees;
        // Sequence number will be used as the key.
        std::map <TxSeq, CandidateTxn> transactions;

    public:
        explicit TxQAccount(std::shared_ptr<STTx const> const& txn);
        explicit TxQAccount(const AccountID& account);

        std::size_t
        getTxnCount() const
        {
            return transactions.size();
        }

        bool
        empty() const
        {
            return !getTxnCount();
        }

        CandidateTxn&
        addCandidate(CandidateTxn&&);

        bool
        removeCandidate(TxSeq const& sequence);

        CandidateTxn const*
        findCandidateAt(TxSeq const& sequence) const;
    };


    using FeeHook = boost::intrusive::member_hook
        <CandidateTxn, boost::intrusive::set_member_hook<>,
        &CandidateTxn::byFeeListHook>;

    using FeeMultiSet = boost::intrusive::multiset
        < CandidateTxn, FeeHook,
        boost::intrusive::compare <GreaterFee> >;

    Setup const setup_;
    beast::Journal j_;

    detail::FeeMetrics feeMetrics_;
    FeeMultiSet byFee_;
    std::map <AccountID, TxQAccount> byAccount_;
    boost::optional<size_t> maxSize_;

    // Most queue operations are done under the master lock,
    // but use this mutex for the RPC "fee" command, which isn't.
    std::mutex mutable mutex_;

private:
    bool isFull() const
    {
        return maxSize_ && byFee_.size() >= *maxSize_;
    }

    bool canBeHeld(std::shared_ptr<STTx const> const&);

    FeeMultiSet::iterator_type erase(FeeMultiSet::const_iterator_type);

};

TxQ::Setup
setup_TxQ(Config const&);

std::unique_ptr<TxQ>
make_TxQ(TxQ::Setup const&, beast::Journal);

} // ripple

#endif
