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
#include <ripple/protocol/TER.h>
#include <ripple/protocol/STTx.h>
#include <boost/intrusive/set.hpp>
#include <boost/circular_buffer.hpp>

namespace ripple {

class Application;
class Config;

/**
    Transaction Queue. Used to manage transactions in conjunction with
    fee escalation.

    Once enough transactions are added to the open ledger, the required
    fee will jump dramatically. If additional transactions are added,
    the fee will grow exponentially.

    Transactions that don't have a high enough fee to be applied to
    the ledger are added to the queue in order from highest fee level to
    lowest. Whenever a new ledger is accepted as validated, transactions
    are first applied from the queue to the open ledger in fee level order
    until either all transactions are applied or the fee again jumps
    too high for the remaining transactions.
*/
class TxQ
{
public:
    static constexpr std::uint64_t baseLevel = 256;

    struct Setup
    {
        explicit Setup() = default;

        std::size_t ledgersInQueue = 20;
        std::size_t queueSizeMin = 2000;
        std::uint32_t retrySequencePercent = 25;
        // TODO: eahennis. Can we remove the multi tx factor?
        std::int32_t multiTxnPercent = -90;
        std::uint32_t minimumEscalationMultiplier = baseLevel * 500;
        std::uint32_t minimumTxnInLedger = 5;
        std::uint32_t minimumTxnInLedgerSA = 1000;
        std::uint32_t targetTxnInLedger = 50;
        boost::optional<std::uint32_t> maximumTxnInLedger;
        std::uint32_t maximumTxnPerAccount = 10;
        std::uint32_t minimumLastLedgerBuffer = 2;
        /* So we don't deal with infinite fee levels, treat
            any transaction with a 0 base fee (ie. SetRegularKey
            password recovery) as having this fee level.
            Should the network behavior change in the future such
            that these transactions are unable to be processed,
            we can make this more complicated. But avoid
            bikeshedding for now.
        */
        std::uint64_t zeroBaseFeeTransactionFeeLevel = 256000;
        bool standAlone = false;
    };

    struct Metrics
    {
        explicit Metrics() = default;

        std::size_t txCount;            // Transactions in the queue
        boost::optional<std::size_t> txQMaxSize;    // Max txns in queue
        std::size_t txInLedger;         // Amount currently in the ledger
        std::size_t txPerLedger;        // Amount expected per ledger
        std::uint64_t referenceFeeLevel;  // Reference transaction fee level
        std::uint64_t minFeeLevel;        // Minimum fee level to get in the queue
        std::uint64_t medFeeLevel;        // Median fee level of the last ledger
        std::uint64_t expFeeLevel;        // Estimated fee level to get in next ledger
    };

    struct AccountTxDetails
    {
        explicit AccountTxDetails() = default;

        uint64_t feeLevel;
        boost::optional<LedgerIndex const> lastValid;
        boost::optional<TxConsequences const> consequences;
    };

    struct TxDetails : AccountTxDetails
    {
        explicit TxDetails() = default;

        AccountID account;
        std::shared_ptr<STTx const> txn;
        int retriesRemaining;
        TER preflightResult;
        boost::optional<TER> lastResult;
    };

    TxQ(Setup const& setup,
        beast::Journal j);

    virtual ~TxQ();

    /**
        Add a new transaction to the open ledger, hold it in the queue,
        or reject it.

        @return A pair with the TER and a bool indicating
                whether or not the transaction was applied.
                If the transaction is queued, will return
                { terQUEUED, false }.
    */
    std::pair<TER, bool>
    apply(Application& app, OpenView& view,
        std::shared_ptr<STTx const> const& tx,
            ApplyFlags flags, beast::Journal j);

    /**
        Fill the new open ledger with transactions from the queue.
        As we apply more transactions to the ledger, the required
        fee will increase.

        @return Whether any txs were added to the view.
    */
    bool
    accept(Application& app, OpenView& view);

    /**
        Update stats and clean up the queue in preparation for
        the next ledger.
    */
    void
    processClosedLedger(Application& app,
        ReadView const& view, bool timeLeap);

    /** Returns fee metrics in reference fee level units.

        @returns Uninitialized @ref optional if the FeeEscalation
        amendment is not enabled.
    */
    boost::optional<Metrics>
    getMetrics(OpenView const& view,
        std::uint32_t txCountPadding = 0) const;

    /** Returns information about the transactions currently
        in the queue for the account.

        @returns Uninitialized @ref optional if the FeeEscalation
        amendment is not enabled, OR if the account has no transactions
        in the queue.
    */
    std::map<TxSeq, AccountTxDetails const>
    getAccountTxs(AccountID const& account, ReadView const& view) const;

    /** Returns information about all transactions currently
        in the queue.

        @returns Uninitialized @ref optional if the FeeEscalation
        amendment is not enabled, OR if there are no transactions
        in the queue.
    */
    std::vector<TxDetails>
    getTxs(ReadView const& view) const;

    /** Packages up fee metrics for the `fee` RPC command.
    */
    Json::Value
    doRPC(Application& app) const;

private:
    class FeeMetrics
    {
    private:
        // Fee escalation

        // Minimum value of txnsExpected.
        std::size_t const minimumTxnCount_;
        // Limit of the txnsExpected value after a
        // time leap.
        std::size_t const targetTxnCount_;
        // Maximum value of txnsExpected
        boost::optional<std::size_t> const maximumTxnCount_;
        // Number of transactions expected per ledger.
        // One more than this value will be accepted
        // before escalation kicks in.
        std::size_t txnsExpected_;
        // Recent history of transaction counts that
        // exceed the targetTxnCount_
        boost::circular_buffer<std::size_t> recentTxnCounts_;
        // Minimum value of escalationMultiplier.
        std::uint64_t const minimumMultiplier_;
        // Based on the median fee of the LCL. Used
        // when fee escalation kicks in.
        std::uint64_t escalationMultiplier_;
        beast::Journal j_;

    public:
        FeeMetrics(Setup const& setup, beast::Journal j)
            : minimumTxnCount_(setup.standAlone ?
                setup.minimumTxnInLedgerSA :
                setup.minimumTxnInLedger)
            , targetTxnCount_(setup.targetTxnInLedger < minimumTxnCount_ ?
                minimumTxnCount_ : setup.targetTxnInLedger)
            , maximumTxnCount_(setup.maximumTxnInLedger ?
                *setup.maximumTxnInLedger < targetTxnCount_ ?
                    targetTxnCount_ : *setup.maximumTxnInLedger :
                        boost::optional<std::size_t>(boost::none))
            , txnsExpected_(minimumTxnCount_)
            , recentTxnCounts_(setup.ledgersInQueue)
            , minimumMultiplier_(setup.minimumEscalationMultiplier)
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
        update(Application& app,
            ReadView const& view, bool timeLeap,
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
            std::uint64_t const escalationMultiplier;
        };

        Snapshot
        getSnapshot() const
        {
            return {
                txnsExpected_,
                escalationMultiplier_
            };
        }

        static
        std::uint64_t
        scaleFeeLevel(Snapshot const& snapshot, OpenView const& view,
            std::uint32_t txCountPadding = 0);

        /**
            Returns the total fee level for all transactions in a series.
            Assumes that there are already more than txnsExpected_ txns in
            the view. If there aren't, the math still works, but the level
            will be high.

            Returns: A `std::pair` as returned from `mulDiv` indicating whether
                the calculation result is safe.
        */
        static
        std::pair<bool, std::uint64_t>
        escalatedSeriesFeeLevel(Snapshot const& snapshot, OpenView const& view,
            std::size_t extraCount, std::size_t seriesSize);
    };

    class MaybeTx
    {
    public:
        // Used by the TxQ::FeeHook and TxQ::FeeMultiSet below
        // to put each MaybeTx object into more than one
        // set without copies, pointers, etc.
        boost::intrusive::set_member_hook<> byFeeListHook;

        std::shared_ptr<STTx const> txn;

        boost::optional<TxConsequences const> consequences;

        uint64_t const feeLevel;
        TxID const txID;
        boost::optional<TxID> priorTxID;
        AccountID const account;
        boost::optional<LedgerIndex> lastValid;
        int retriesRemaining;
        TxSeq const sequence;
        ApplyFlags const flags;
        boost::optional<TER> lastResult;
        // Invariant: pfresult is never allowed to be empty. The
        // boost::optional is leveraged to allow `emplace`d
        // construction and replacement without a copy
        // assignment operation.
        boost::optional<PreflightResult const> pfresult;

        /* In TxQ::accept, the required fee level may be low
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
        MaybeTx(std::shared_ptr<STTx const> const&,
            TxID const& txID, std::uint64_t feeLevel,
                ApplyFlags const flags,
                    PreflightResult const& pfresult);

        std::pair<TER, bool>
        apply(Application& app, OpenView& view, beast::Journal j);
    };

    class GreaterFee
    {
    public:
        explicit GreaterFee() = default;

        bool operator()(const MaybeTx& lhs, const MaybeTx& rhs) const
        {
            return lhs.feeLevel > rhs.feeLevel;
        }
    };

    class TxQAccount
    {
    public:
        using TxMap = std::map <TxSeq, MaybeTx>;

        AccountID const account;
        // Sequence number will be used as the key.
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

        MaybeTx&
        add(MaybeTx&&);

        bool
        remove(TxSeq const& sequence);
    };

    using FeeHook = boost::intrusive::member_hook
        <MaybeTx, boost::intrusive::set_member_hook<>,
        &MaybeTx::byFeeListHook>;

    using FeeMultiSet = boost::intrusive::multiset
        < MaybeTx, FeeHook,
        boost::intrusive::compare <GreaterFee> >;

    using AccountMap = std::map <AccountID, TxQAccount>;

    Setup const setup_;
    beast::Journal j_;

    // These members must always and only be accessed under
    // locked mutex_
    FeeMetrics feeMetrics_;
    FeeMultiSet byFee_;
    AccountMap byAccount_;
    boost::optional<size_t> maxSize_;

    // Most queue operations are done under the master lock,
    // but use this mutex for the RPC "fee" command, which isn't.
    std::mutex mutable mutex_;

private:
    template<size_t fillPercentage = 100>
    bool
    isFull() const;

    bool canBeHeld(STTx const&, OpenView const&,
        AccountMap::iterator,
            boost::optional<FeeMultiSet::iterator>);

    // Erase and return the next entry in byFee_ (lower fee level)
    FeeMultiSet::iterator_type erase(FeeMultiSet::const_iterator_type);
    // Erase and return the next entry for the account (if fee level
    // is higher), or next entry in byFee_ (lower fee level).
    // Used to get the next "applyable" MaybeTx for accept().
    FeeMultiSet::iterator_type eraseAndAdvance(FeeMultiSet::const_iterator_type);
    // Erase a range of items, based on TxQAccount::TxMap iterators
    TxQAccount::TxMap::iterator
    erase(TxQAccount& txQAccount, TxQAccount::TxMap::const_iterator begin,
        TxQAccount::TxMap::const_iterator end);

    /*
        All-or-nothing attempt to try to apply all the queued txs for `accountIter`
        up to and including `tx`.
    */
    std::pair<TER, bool>
    tryClearAccountQueue(Application& app, OpenView& view,
        STTx const& tx, AccountMap::iterator const& accountIter,
            TxQAccount::TxMap::iterator, std::uint64_t feeLevelPaid,
                PreflightResult const& pfresult,
                    std::size_t const txExtraCount, ApplyFlags flags,
                        FeeMetrics::Snapshot const& metricsSnapshot,
                            beast::Journal j);

};

TxQ::Setup
setup_TxQ(Config const&);

std::unique_ptr<TxQ>
make_TxQ(TxQ::Setup const&, beast::Journal);

} // ripple

#endif
