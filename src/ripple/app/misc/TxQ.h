//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-19 Ripple Labs Inc.

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
#include <ripple/ledger/ApplyView.h>
#include <ripple/ledger/OpenView.h>
#include <ripple/protocol/STTx.h>
#include <ripple/protocol/SeqProxy.h>
#include <ripple/protocol/TER.h>
#include <boost/circular_buffer.hpp>
#include <boost/intrusive/set.hpp>
#include <optional>

namespace ripple {

class Application;
class Config;

/**
    Transaction Queue. Used to manage transactions in conjunction with
    fee escalation.

    Once enough transactions are added to the open ledger, the required
    fee will jump dramatically. If additional transactions are added,
    the fee will grow exponentially from there.

    Transactions that don't have a high enough fee to be applied to
    the ledger are added to the queue in order from highest fee level to
    lowest. Whenever a new ledger is accepted as validated, transactions
    are first applied from the queue to the open ledger in fee level order
    until either all transactions are applied or the fee again jumps
    too high for the remaining transactions.

    For further information and a high-level overview of how transactions
    are processed with the `TxQ`, see FeeEscalation.md
*/
class TxQ
{
public:
    /// Fee level for single-signed reference transaction.
    static constexpr FeeLevel64 baseLevel{256};

    /**
        Structure used to customize @ref TxQ behavior.
    */
    struct Setup
    {
        /// Default constructor
        explicit Setup() = default;

        /** Number of ledgers' worth of transactions to allow
            in the queue. For example, if the last ledger had
            150 transactions, then up to 3000 transactions can
            be queued.

            Can be overridden by @ref queueSizeMin
        */
        std::size_t ledgersInQueue = 20;
        /** The smallest limit the queue is allowed.

            Will allow more than `ledgersInQueue` in the queue
            if ledgers are small.
        */
        std::size_t queueSizeMin = 2000;
        /** Extra percentage required on the fee level of a queued
            transaction to replace that transaction with another
            with the same SeqProxy.

            If queued transaction for account "Alice" with seq 45
            has a fee level of 512, a replacement transaction for
            "Alice" with seq 45 must have a fee level of at least
            512 * (1 + 0.25) = 640 to be considered.
        */
        std::uint32_t retrySequencePercent = 25;
        /// Minimum value of the escalation multiplier, regardless
        /// of the prior ledger's median fee level.
        FeeLevel64 minimumEscalationMultiplier = baseLevel * 500;
        /// Minimum number of transactions to allow into the ledger
        /// before escalation, regardless of the prior ledger's size.
        std::uint32_t minimumTxnInLedger = 32;
        /// Like @ref minimumTxnInLedger for standalone mode.
        /// Primarily so that tests don't need to worry about queuing.
        std::uint32_t minimumTxnInLedgerSA = 1000;
        /// Number of transactions per ledger that fee escalation "works
        /// towards".
        std::uint32_t targetTxnInLedger = 256;
        /** Optional maximum allowed value of transactions per ledger before
            fee escalation kicks in. By default, the maximum is an emergent
            property of network, validator, and consensus performance. This
            setting can override that behavior to prevent fee escalation from
            allowing more than `maximumTxnInLedger` "cheap" transactions into
            the open ledger.

            @todo eahennis. This setting seems to go against our goals and
                values. Can it be removed?
        */
        std::optional<std::uint32_t> maximumTxnInLedger;
        /** When the ledger has more transactions than "expected", and
            performance is humming along nicely, the expected ledger size
            is updated to the previous ledger size plus this percentage.

            Calculations are subject to configured limits, and the recent
            transactions counts buffer.

            Example: If the "expectation" is for 500 transactions, and a
            ledger is validated normally with 501 transactions, then the
            expected ledger size will be updated to 601.
        */
        std::uint32_t normalConsensusIncreasePercent = 20;
        /** When consensus takes longer than appropriate, the expected
            ledger size is updated to the lesser of the previous ledger
            size and the current expected ledger size minus this
            percentage.

            Calculations are subject to configured limits.

            Example: If the ledger has 15000 transactions, and it is
            validated slowly, then the expected ledger size will be
            updated to 7500. If there are only 6 transactions, the
            expected ledger size will be updated to 5, assuming the
            default minimum.
        */
        std::uint32_t slowConsensusDecreasePercent = 50;
        /// Maximum number of transactions that can be queued by one account.
        std::uint32_t maximumTxnPerAccount = 10;
        /** Minimum difference between the current ledger sequence and a
            transaction's `LastLedgerSequence` for the transaction to be
            queueable. Decreases the chance a transaction will get queued
            and broadcast only to expire before it gets a chance to be
            processed.
        */
        std::uint32_t minimumLastLedgerBuffer = 2;
        /// Use standalone mode behavior.
        bool standAlone = false;
    };

    /**
        Structure returned by @ref TxQ::getMetrics, expressed in
        reference fee level units.
    */
    struct Metrics
    {
        /// Default constructor
        explicit Metrics() = default;

        /// Number of transactions in the queue
        std::size_t txCount;
        /// Max transactions currently allowed in queue
        std::optional<std::size_t> txQMaxSize;
        /// Number of transactions currently in the open ledger
        std::size_t txInLedger;
        /// Number of transactions expected per ledger
        std::size_t txPerLedger;
        /// Reference transaction fee level
        FeeLevel64 referenceFeeLevel;
        /// Minimum fee level for a transaction to be considered for
        /// the open ledger or the queue
        FeeLevel64 minProcessingFeeLevel;
        /// Median fee level of the last ledger
        FeeLevel64 medFeeLevel;
        /// Minimum fee level to get into the current open ledger,
        /// bypassing the queue
        FeeLevel64 openLedgerFeeLevel;
    };

    /**
        Structure that describes a transaction in the queue
        waiting to be applied to the current open ledger.
        A collection of these is returned by @ref TxQ::getTxs.
    */
    struct TxDetails
    {
        /// Full initialization
        TxDetails(
            FeeLevel64 feeLevel_,
            std::optional<LedgerIndex> const& lastValid_,
            TxConsequences const& consequences_,
            AccountID const& account_,
            SeqProxy seqProxy_,
            std::shared_ptr<STTx const> const& txn_,
            int retriesRemaining_,
            TER preflightResult_,
            std::optional<TER> lastResult_)
            : feeLevel(feeLevel_)
            , lastValid(lastValid_)
            , consequences(consequences_)
            , account(account_)
            , seqProxy(seqProxy_)
            , txn(txn_)
            , retriesRemaining(retriesRemaining_)
            , preflightResult(preflightResult_)
            , lastResult(lastResult_)
        {
        }

        /// Fee level of the queued transaction
        FeeLevel64 feeLevel;
        /// LastValidLedger field of the queued transaction, if any
        std::optional<LedgerIndex> lastValid;
        /** Potential @ref TxConsequences of applying the queued transaction
            to the open ledger.
        */
        TxConsequences consequences;
        /// The account the transaction is queued for
        AccountID account;
        /// SeqProxy of the transaction
        SeqProxy seqProxy;
        /// The full transaction
        std::shared_ptr<STTx const> txn;
        /** Number of times the transactor can return a retry / `ter` result
            when attempting to apply this transaction to the open ledger
            from the queue. If the transactor returns `ter` and no retries are
            left, this transaction will be dropped.
        */
        int retriesRemaining;
        /** The *intermediate* result returned by @ref preflight before
            this transaction was queued, or after it is queued, but before
            a failed attempt to `apply` it to the open ledger. This will
            usually be `tesSUCCESS`, but there are some edge cases where
            it has another value. Those edge cases are interesting enough
            that this value is made available here. Specifically, if the
            `rules` change between attempts, `preflight` will be run again
            in `TxQ::MaybeTx::apply`.
        */
        TER preflightResult;
        /** If the transactor attempted to apply the transaction to the open
            ledger from the queue and *failed*, then this is the transactor
            result from the last attempt. Should never be a `tec`, `tef`,
            `tem`, or `tesSUCCESS`, because those results cause the
            transaction to be removed from the queue.
        */
        std::optional<TER> lastResult;
    };

    /// Constructor
    TxQ(Setup const& setup, beast::Journal j);

    /// Destructor
    virtual ~TxQ();

    /**
        Add a new transaction to the open ledger, hold it in the queue,
        or reject it.

        @return A pair with the `TER` and a `bool` indicating
                whether or not the transaction was applied to
                the open ledger. If the transaction is queued,
                will return `{ terQUEUED, false }`.
    */
    std::pair<TER, bool>
    apply(
        Application& app,
        OpenView& view,
        std::shared_ptr<STTx const> const& tx,
        ApplyFlags flags,
        beast::Journal j);

    /**
        Fill the new open ledger with transactions from the queue.

        @note As more transactions are applied to the ledger, the
        required fee may increase. The required fee may rise above
        the fee level of the queued items before the queue is emptied,
        which will end the process, leaving those in the queue for
        the next open ledger.

        @return Whether any transactions were added to the `view`.
    */
    bool
    accept(Application& app, OpenView& view);

    /**
        Update fee metrics and clean up the queue in preparation for
        the next ledger.

        @note Fee metrics are updated based on the fee levels of the
        txs in the validated ledger and whether consensus is slow.
        Maximum queue size is adjusted to be enough to hold
        `ledgersInQueue` ledgers or `queueSizeMin` transactions.
        Any transactions for which the `LastLedgerSequence` has
        passed are removed from the queue, and any account objects
        that have no candidates under them are removed.
    */
    void
    processClosedLedger(Application& app, ReadView const& view, bool timeLeap);

    /** Return the next sequence that would go in the TxQ for an account. */
    SeqProxy
    nextQueuableSeq(std::shared_ptr<SLE const> const& sleAccount) const;

    /** Returns fee metrics in reference fee level units.
     */
    Metrics
    getMetrics(OpenView const& view) const;

    struct FeeAndSeq
    {
        XRPAmount fee;
        std::uint32_t accountSeq;
        std::uint32_t availableSeq;
    };

    /**
     * @brief Returns minimum required fee for tx and two sequences:
     *        first vaild sequence for this account in current ledger
     *        and first available sequence for transaction
     * @param view current open ledger
     * @param tx the transaction
     * @return minimum required fee, first sequence in the ledger
     *        and first available sequence
     */
    FeeAndSeq
    getTxRequiredFeeAndSeq(
        OpenView const& view,
        std::shared_ptr<STTx const> const& tx) const;

    /** Returns information about the transactions currently
        in the queue for the account.

        @returns Empty `vector` if the account has no transactions
        in the queue.
    */
    std::vector<TxDetails>
    getAccountTxs(AccountID const& account, ReadView const& view) const;

    /** Returns information about all transactions currently
        in the queue.

        @returns Empty `vector` if there are no transactions
        in the queue.
    */
    std::vector<TxDetails>
    getTxs(ReadView const& view) const;

    /** Summarize current fee metrics for the `fee` RPC command.

        @returns a `Json objectvalue`
    */
    Json::Value
    doRPC(Application& app) const;

private:
    // Implementation for nextQueuableSeq().  The passed lock must be held.
    SeqProxy
    nextQueuableSeqImpl(
        std::shared_ptr<SLE const> const& sleAccount,
        std::lock_guard<std::mutex> const&) const;

    /**
        Track and use the fee escalation metrics of the
        current open ledger. Does the work of scaling fees
        as the open ledger grows.
    */
    class FeeMetrics
    {
    private:
        /// Minimum value of txnsExpected.
        std::size_t const minimumTxnCount_;
        /// Number of transactions per ledger that fee escalation "works
        /// towards".
        std::size_t const targetTxnCount_;
        /// Maximum value of txnsExpected
        std::optional<std::size_t> const maximumTxnCount_;
        /// Number of transactions expected per ledger.
        /// One more than this value will be accepted
        /// before escalation kicks in.
        std::size_t txnsExpected_;
        /// Recent history of transaction counts that
        /// exceed the targetTxnCount_
        boost::circular_buffer<std::size_t> recentTxnCounts_;
        /// Based on the median fee of the LCL. Used
        /// when fee escalation kicks in.
        FeeLevel64 escalationMultiplier_;
        /// Journal
        beast::Journal const j_;

    public:
        /// Constructor
        FeeMetrics(Setup const& setup, beast::Journal j)
            : minimumTxnCount_(
                  setup.standAlone ? setup.minimumTxnInLedgerSA
                                   : setup.minimumTxnInLedger)
            , targetTxnCount_(
                  setup.targetTxnInLedger < minimumTxnCount_
                      ? minimumTxnCount_
                      : setup.targetTxnInLedger)
            , maximumTxnCount_(
                  setup.maximumTxnInLedger
                      ? *setup.maximumTxnInLedger < targetTxnCount_
                          ? targetTxnCount_
                          : *setup.maximumTxnInLedger
                      : std::optional<std::size_t>(std::nullopt))
            , txnsExpected_(minimumTxnCount_)
            , recentTxnCounts_(setup.ledgersInQueue)
            , escalationMultiplier_(setup.minimumEscalationMultiplier)
            , j_(j)
        {
        }

        /**
            Updates fee metrics based on the transactions in the ReadView
            for use in fee escalation calculations.

            @param app Rippled Application object.
            @param view View of the LCL that was just closed or received.
            @param timeLeap Indicates that rippled is under load so fees
            should grow faster.
            @param setup Customization params.
        */
        std::size_t
        update(
            Application& app,
            ReadView const& view,
            bool timeLeap,
            TxQ::Setup const& setup);

        /// Snapshot of the externally relevant FeeMetrics
        /// fields at any given time.
        struct Snapshot
        {
            // Number of transactions expected per ledger.
            // One more than this value will be accepted
            // before escalation kicks in.
            std::size_t const txnsExpected;
            // Based on the median fee of the LCL. Used
            // when fee escalation kicks in.
            FeeLevel64 const escalationMultiplier;
        };

        /// Get the current @ref Snapshot
        Snapshot
        getSnapshot() const
        {
            return {txnsExpected_, escalationMultiplier_};
        }

        /** Use the number of transactions in the current open ledger
            to compute the fee level a transaction must pay to bypass the
            queue.

            @param view Current open ledger.

            @return A fee level value.
        */
        static FeeLevel64
        scaleFeeLevel(Snapshot const& snapshot, OpenView const& view);

        /**
            Computes the total fee level for all transactions in a series.
            Assumes that there are already more than @ref txnsExpected_ txns
            between the view and `extraCount`. If there aren't, the result
            will be sensible (e.g. there won't be any underflows or
            overflows), but the level will be higher than actually required.

            @note A "series" is a set of transactions for the same account.
                In the context of this function, the series is already in
                the queue, and the series starts with the account's current
                sequence number. This function is called by
                @ref tryClearAccountQueueUpThruTx to figure out if a newly
                submitted transaction is paying enough to get all of the queued
                transactions plus itself out of the queue and into the open
                ledger while accounting for the escalating fee as each one
                is processed. The idea is that if a series of transactions
                are taking too long to get out of the queue, a user can
                "rescue" them without having to resubmit each one with an
                individually higher fee.

            @param view Current open / working ledger. (May be a sandbox.)
            @param extraCount Number of additional transactions to count as
                in the ledger. (If `view` is a sandbox, should be the number of
                transactions in the parent ledger.)
            @param seriesSize Total number of transactions in the series to be
                processed.

            @return A `std::pair` as returned from @ref `mulDiv` indicating
                whether the calculation result overflows.
        */
        static std::pair<bool, FeeLevel64>
        escalatedSeriesFeeLevel(
            Snapshot const& snapshot,
            OpenView const& view,
            std::size_t extraCount,
            std::size_t seriesSize);
    };

    /**
        Represents a transaction in the queue which may be applied
        later to the open ledger.
    */
    class MaybeTx
    {
    public:
        /// Used by the TxQ::FeeHook and TxQ::FeeMultiSet below
        /// to put each MaybeTx object into more than one
        /// set without copies, pointers, etc.
        boost::intrusive::set_member_hook<> byFeeListHook;

        /// The complete transaction.
        std::shared_ptr<STTx const> txn;

        /// Computed fee level that the transaction will pay.
        FeeLevel64 const feeLevel;
        /// Transaction ID.
        TxID const txID;
        /// Account submitting the transaction.
        AccountID const account;
        /// Expiration ledger for the transaction
        /// (`sfLastLedgerSequence` field).
        std::optional<LedgerIndex> const lastValid;
        /// Transaction SeqProxy number
        /// (`sfSequence` or `sfTicketSequence` field).
        SeqProxy const seqProxy;
        /**
            A transaction at the front of the queue will be given
            several attempts to succeed before being dropped from
            the queue. If dropped, one of the account's penalty
            flags will be set, and other transactions may have
            their `retriesRemaining` forced down as part of the
            penalty.
        */
        int retriesRemaining;
        /// Flags provided to `apply`. If the transaction is later
        /// attempted with different flags, it will need to be
        /// `preflight`ed again.
        ApplyFlags const flags;
        /** If the transactor attempted to apply the transaction to the open
            ledger from the queue and *failed*, then this is the transactor
            result from the last attempt. Should never be a `tec`, `tef`,
            `tem`, or `tesSUCCESS`, because those results cause the
            transaction to be removed from the queue.
        */
        std::optional<TER> lastResult;
        /** Cached result of the `preflight` operation. Because
            `preflight` is expensive, minimize the number of times
            it needs to be done.
            @invariant `pfresult` is never allowed to be empty. The
                `std::optional` is leveraged to allow `emplace`d
                construction and replacement without a copy
                assignment operation.
        */
        std::optional<PreflightResult const> pfresult;

        /** Starting retry count for newly queued transactions.

            In TxQ::accept, the required fee level may be low
            enough that this transaction gets a chance to apply
            to the ledger, but it may get a retry ter result for
            another reason (eg. insufficient balance). When that
            happens, the transaction is left in the queue to try
            again later, but it shouldn't be allowed to fail
            indefinitely. The number of failures allowed is
            essentially arbitrary. It should be large enough to
            allow temporary failures to clear up, but small enough
            that the queue doesn't fill up with stale transactions
            which prevent lower fee level transactions from queuing.
        */
        static constexpr int retriesAllowed = 10;

    public:
        /// Constructor
        MaybeTx(
            std::shared_ptr<STTx const> const&,
            TxID const& txID,
            FeeLevel64 feeLevel,
            ApplyFlags const flags,
            PreflightResult const& pfresult);

        /// Attempt to apply the queued transaction to the open ledger.
        std::pair<TER, bool>
        apply(Application& app, OpenView& view, beast::Journal j);

        /// Potential @ref TxConsequences of applying this transaction
        /// to the open ledger.
        TxConsequences const&
        consequences() const
        {
            return pfresult->consequences;
        }

        /// Return a TxDetails based on contained information.
        TxDetails
        getTxDetails() const
        {
            return {
                feeLevel,
                lastValid,
                consequences(),
                account,
                seqProxy,
                txn,
                retriesRemaining,
                pfresult->ter,
                lastResult};
        }
    };

    /// Used for sorting @ref MaybeTx
    class OrderCandidates
    {
    public:
        /// Default constructor
        explicit OrderCandidates() = default;

        /** Sort @ref MaybeTx by `feeLevel` descending, then by
         * transaction ID ascending
         *
         * The transaction queue is ordered such that transactions
         * paying a higher fee are in front of transactions paying
         * a lower fee, giving them an opportunity to be processed into
         * the open ledger first. Within transactions paying the same
         * fee, order by the arbitrary but consistent transaction ID.
         * This allows validators to build similar queues in the same
         * order, and thus have more similar initial proposals.
         *
         */
        bool
        operator()(const MaybeTx& lhs, const MaybeTx& rhs) const
        {
            if (lhs.feeLevel == rhs.feeLevel)
                return lhs.txID < rhs.txID;
            return lhs.feeLevel > rhs.feeLevel;
        }
    };

    /** Used to represent an account to the queue, and stores the
        transactions queued for that account by SeqProxy.
    */
    class TxQAccount
    {
    public:
        using TxMap = std::map<SeqProxy, MaybeTx>;

        /// The account
        AccountID const account;
        /// Sequence number will be used as the key.
        TxMap transactions;
        /* If this account has had any transaction retry more than
            `retriesAllowed` times so that it was dropped from the
            queue, then all other transactions for this account will
            be given at most 2 attempts before being removed. Helps
            prevent wasting resources on retries that are more likely
            to fail.
        */
        bool retryPenalty = false;
        /* If this account has had any transaction fail or expire,
            then when the queue is nearly full, transactions from
            this account will be discarded. Helps prevent the queue
            from getting filled and wedged.
        */
        bool dropPenalty = false;

    public:
        /// Construct from a transaction
        explicit TxQAccount(std::shared_ptr<STTx const> const& txn);
        /// Construct from an account
        explicit TxQAccount(const AccountID& account);

        /// Return the number of transactions currently queued for this account
        std::size_t
        getTxnCount() const
        {
            return transactions.size();
        }

        /// Checks if this account has no transactions queued
        bool
        empty() const
        {
            return !getTxnCount();
        }

        /// Find the entry in transactions that precedes seqProx, if one does.
        TxMap::const_iterator
        getPrevTx(SeqProxy seqProx) const;

        /// Add a transaction candidate to this account for queuing
        MaybeTx&
        add(MaybeTx&&);

        /** Remove the candidate with given SeqProxy value from this
            account.

            @return Whether a candidate was removed
        */
        bool
        remove(SeqProxy seqProx);
    };

    // Helper function returns requiredFeeLevel.
    FeeLevel64
    getRequiredFeeLevel(
        OpenView& view,
        ApplyFlags flags,
        FeeMetrics::Snapshot const& metricsSnapshot,
        std::lock_guard<std::mutex> const& lock) const;

    // Helper function for TxQ::apply.  If a transaction's fee is high enough,
    // attempt to directly apply that transaction to the ledger.
    std::optional<std::pair<TER, bool>>
    tryDirectApply(
        Application& app,
        OpenView& view,
        std::shared_ptr<STTx const> const& tx,
        ApplyFlags flags,
        beast::Journal j);

    // Helper function that removes a replaced entry in _byFee.
    std::optional<TxQAccount::TxMap::iterator>
    removeFromByFee(
        std::optional<TxQAccount::TxMap::iterator> const& replacedTxIter,
        std::shared_ptr<STTx const> const& tx);

    using FeeHook = boost::intrusive::member_hook<
        MaybeTx,
        boost::intrusive::set_member_hook<>,
        &MaybeTx::byFeeListHook>;

    using FeeMultiSet = boost::intrusive::
        multiset<MaybeTx, FeeHook, boost::intrusive::compare<OrderCandidates>>;

    using AccountMap = std::map<AccountID, TxQAccount>;

    /// Setup parameters used to control the behavior of the queue
    Setup const setup_;
    /// Journal
    beast::Journal const j_;

    /** Tracks the current state of the queue.
        @note This member must always and only be accessed under
        locked mutex_
    */
    FeeMetrics feeMetrics_;
    /** The queue itself: the collection of transactions ordered
        by fee level.
        @note This member must always and only be accessed under
        locked mutex_
    */
    FeeMultiSet byFee_;
    /** All of the accounts which currently have any transactions
        in the queue. Entries are created and destroyed dynamically
        as transactions are added and removed.
        @note This member must always and only be accessed under
        locked mutex_
    */
    AccountMap byAccount_;
    /** Maximum number of transactions allowed in the queue based
        on the current metrics. If uninitialized, there is no limit,
        but that condition cannot last for long in practice.
        @note This member must always and only be accessed under
        locked mutex_
    */
    std::optional<size_t> maxSize_;

    /** Most queue operations are done under the master lock,
        but use this mutex for the RPC "fee" command, which isn't.
    */
    std::mutex mutable mutex_;

private:
    /// Is the queue at least `fillPercentage` full?
    template <size_t fillPercentage = 100>
    bool
    isFull() const;

    /** Checks if the indicated transaction fits the conditions
        for being stored in the queue.
    */
    TER
    canBeHeld(
        STTx const&,
        ApplyFlags const,
        OpenView const&,
        std::shared_ptr<SLE const> const& sleAccount,
        AccountMap::iterator const&,
        std::optional<TxQAccount::TxMap::iterator> const&,
        std::lock_guard<std::mutex> const& lock);

    /// Erase and return the next entry in byFee_ (lower fee level)
    FeeMultiSet::iterator_type erase(FeeMultiSet::const_iterator_type);
    /** Erase and return the next entry for the account (if fee level
        is higher), or next entry in byFee_ (lower fee level).
        Used to get the next "applyable" MaybeTx for accept().
    */
    FeeMultiSet::iterator_type eraseAndAdvance(
        FeeMultiSet::const_iterator_type);
    /// Erase a range of items, based on TxQAccount::TxMap iterators
    TxQAccount::TxMap::iterator
    erase(
        TxQAccount& txQAccount,
        TxQAccount::TxMap::const_iterator begin,
        TxQAccount::TxMap::const_iterator end);

    /**
        All-or-nothing attempt to try to apply the queued txs for
        `accountIter` up to and including `tx`.  Transactions following
        `tx` are not cleared.
    */
    std::pair<TER, bool>
    tryClearAccountQueueUpThruTx(
        Application& app,
        OpenView& view,
        STTx const& tx,
        AccountMap::iterator const& accountIter,
        TxQAccount::TxMap::iterator,
        FeeLevel64 feeLevelPaid,
        PreflightResult const& pfresult,
        std::size_t const txExtraCount,
        ApplyFlags flags,
        FeeMetrics::Snapshot const& metricsSnapshot,
        beast::Journal j);
};

/**
    Build a @ref TxQ::Setup object from application configuration.
*/
TxQ::Setup
setup_TxQ(Config const&);

template <class T>
XRPAmount
toDrops(FeeLevel<T> const& level, XRPAmount const& baseFee)
{
    if (auto const drops = mulDiv(level, baseFee, TxQ::baseLevel); drops.first)
        return drops.second;

    return XRPAmount(STAmount::cMaxNativeN);
}

inline FeeLevel64
toFeeLevel(XRPAmount const& drops, XRPAmount const& baseFee)
{
    if (auto const feeLevel = mulDiv(drops, TxQ::baseLevel, baseFee);
        feeLevel.first)
        return feeLevel.second;

    return FeeLevel64(std::numeric_limits<std::uint64_t>::max());
}

}  // namespace ripple

#endif
