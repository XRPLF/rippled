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

#include <ripple/ledger/OpenView.h>
#include <ripple/ledger/ApplyView.h>
#include <ripple/core/Config.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/protocol/TER.h>
#include <ripple/protocol/STTx.h>

namespace ripple {

class Application;

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

    Note EAHENNIS: I hate that this class is virtualized, but
    flattening it out introduces dependency problems.
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

    static constexpr TER txnResultHeld() { return terQUEUED; }
    static constexpr TER txnResultLowFee() { return telINSUF_FEE_P; }

    virtual ~TxQ() = default;

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
    virtual
    std::pair<TER, bool>
    apply(Application& app, OpenView& view,
        std::shared_ptr<STTx const> const& tx,
            ApplyFlags flags, beast::Journal j) = 0;

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
    virtual
    bool
    accept(Application& app, OpenView& view,
        ApplyFlags flags = tapNONE) = 0;

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
    virtual
    void
    processValidatedLedger(Application& app,
        OpenView const& view, bool timeLeap,
            ApplyFlags flags = tapNONE) = 0;

    /** Used by tests only.
    */
    virtual
    std::size_t
    setMinimumTx(int m) = 0;

    /** Returns fee metrics in reference fee (level) units.
    */
    virtual
    struct Metrics
    getMetrics(OpenView const& view) const = 0;

    /** Packages up fee metrics for the `fee` RPC command.
    */
    virtual
    Json::Value
    doRPC(Application& app) const = 0;

    /** Return the instantaneous fee to get into the current
        open ledger for a reference transaction.
    */
    virtual
    XRPAmount
    openLedgerFee(OpenView const& view) const = 0;
};

TxQ::Setup
setup_TxQ(Config const&);

std::unique_ptr<TxQ>
make_TxQ(TxQ::Setup const&, beast::Journal);

} // ripple

#endif
