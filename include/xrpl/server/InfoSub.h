#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/server/Manifest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace xrpl {

// Operations that clients may wish to perform against the network
// Master operational handler, server sequencer, network tracker

/**
 * Maximum number of subscriptions a single client connection may hold at once.
 *
 * Applies to the account, real-time account, and account-history subscriptions
 * tracked on one InfoSub (the sets counted by totalSubscriptionCount), bounding
 * the disconnect-time cleanup of those sets. Book subscriptions are tracked
 * separately (OrderBookDB) and are not counted here. Generous enough for
 * legitimate power users such as block explorers.
 */
constexpr std::size_t kMaxSubscriptionsPerConnection = 100'000;

/**
 * Whether adding @p additional subscriptions to a connection already holding
 * @p current would exceed the cap.
 *
 * Pure arithmetic split out so it can be unit-tested without a live
 * connection. The first term avoids underflow in the subtraction.
 *
 * @param current    Subscriptions already tracked on the connection.
 * @param additional Subscriptions a request would add.
 * @param cap        The effective per-connection cap. Defaults to the
 * built-in limit; callers may pass a configured override.
 * @return true if the request must be rejected to stay within the cap.
 */
[[nodiscard]] constexpr bool
exceedsSubscriptionCap(
    std::size_t current,
    std::size_t additional,
    std::size_t cap = kMaxSubscriptionsPerConnection)
{
    return additional > cap || current > cap - additional;
}

class InfoSubRequest : public CountedObject<InfoSubRequest>
{
public:
    using pointer = std::shared_ptr<InfoSubRequest>;

    virtual ~InfoSubRequest() = default;

    virtual json::Value
    doClose() = 0;
    virtual json::Value
    doStatus(json::Value const&) = 0;
};

/**
 * Manages a client's subscription to data feeds.
 *
 * An InfoSub holds a non-owning reference to its `Source` (typically the
 * process-wide `NetworkOPsImp`). The destructor reaches back into the
 * `Source` to remove this subscriber from every server-side subscription
 * map.
 *
 * @note Lifetime contract: every `InfoSub` instance MUST be destroyed
 * before the backing `Source`. NetworkOPsImp shutdown drops all
 * subscriber strong refs before its own teardown to satisfy this.
 * @note Thread-safety: per-instance state is guarded by `lock_`. The
 * destructor reads tracking sets without taking `lock_` because
 * the strong-pointer ref-count is zero at destruction time, so
 * no other thread can be calling the public mutators.
 */
class InfoSub : public CountedObject<InfoSub>
{
public:
    using pointer = std::shared_ptr<InfoSub>;

    // VFALCO TODO Standardize on the names of weak / strong pointer type
    // aliases.
    using wptr = std::weak_ptr<InfoSub>;

    using ref = std::shared_ptr<InfoSub> const&;

    using Consumer = resource::Consumer;

public:
    /**
     * Abstracts the source of subscription data.
     */
    class Source
    {
    public:
        virtual ~Source() = default;

        // For some reason, these were originally called "rt"
        // for "real time". They actually refer to whether
        // you get transactions as they occur or once their
        // results are confirmed
        virtual void
        subAccount(ref ispListener, hash_set<AccountID> const& vnaAccountIDs, bool realTime) = 0;

        // for normal use, removes from InfoSub and server
        virtual void
        unsubAccount(ref isplistener, hash_set<AccountID> const& vnaAccountIDs, bool realTime) = 0;

        // for use during InfoSub destruction
        // Removes only from the server
        virtual void
        unsubAccountInternal(
            std::uint64_t uListener,
            hash_set<AccountID> const& vnaAccountIDs,
            bool realTime) = 0;

        /**
         * subscribe an account's new transactions and retrieve the account's
         * historical transactions
         * @return rpcSUCCESS if successful, otherwise an error code
         */
        virtual ErrorCodeI
        subAccountHistory(ref ispListener, AccountID const& account) = 0;

        /**
         * unsubscribe an account's transactions
         * @param historyOnly if true, only stop historical transactions
         * @note once a client receives enough historical transactions,
         * it should unsubscribe with historyOnly == true to stop receiving
         * more historical transactions. It will continue to receive new
         * transactions.
         */
        virtual void
        unsubAccountHistory(ref ispListener, AccountID const& account, bool historyOnly) = 0;

        virtual void
        unsubAccountHistoryInternal(
            std::uint64_t uListener,
            AccountID const& account,
            bool historyOnly) = 0;

        virtual void
        unsubMPTInternal(std::uint64_t uListener, hash_set<MPTID> const& mptIDs) = 0;

        /**
         * Schedule the server-side teardown of a disconnecting connection's
         * account subscriptions off the destructor thread.
         *
         * The implementation posts a low-priority JobQueue task that erases the
         * entries in bounded chunks, so `~InfoSub` returns immediately instead
         * of running the erase loop inline. The sets are taken by value so the
         * job owns its copies and never references the destroyed `InfoSub`.
         * Cleanup is keyed on `seq` (unique per connection), so deferring it
         * cannot disturb a reconnected client reusing the same accounts.
         *
         * @param seq The disconnecting connection's unique subscription id.
         * @param rtAccounts Real-time account subscriptions to remove.
         * @param normalAccounts Normal account subscriptions to remove.
         * @param historyAccounts Account-history subscriptions to remove.
         *
         * @note The implementing `Source` must outlive any job it posts. If the
         * JobQueue is already stopping (process shutdown), the job is not
         * enqueued; the cleanup is skipped because the server-side maps
         * are about to be destroyed and no publishing can run.
         */
        virtual void
        scheduleAccountCleanup(
            std::uint64_t seq,
            hash_set<AccountID> rtAccounts,
            hash_set<AccountID> normalAccounts,
            hash_set<AccountID> historyAccounts) = 0;

        // VFALCO TODO Document the bool return value
        virtual bool
        subLedger(ref ispListener, json::Value& jvResult) = 0;
        virtual bool
        unsubLedger(std::uint64_t uListener) = 0;

        virtual bool
        subBookChanges(ref ispListener) = 0;
        virtual bool
        unsubBookChanges(std::uint64_t uListener) = 0;

        virtual bool
        subManifests(ref ispListener) = 0;
        virtual bool
        unsubManifests(std::uint64_t uListener) = 0;
        virtual void
        pubManifest(Manifest const&) = 0;

        virtual bool
        subServer(ref ispListener, json::Value& jvResult, bool admin) = 0;
        virtual bool
        unsubServer(std::uint64_t uListener) = 0;

        virtual bool
        subBook(ref ispListener, Book const&) = 0;

        /**
         * Remove a book subscription for a live subscriber.
         *
         * Clears the book from the subscriber's own tracking set
         * (InfoSub::bookSubscriptions_) and then removes the server-side
         * entry from subBook_. Call this from RPC unsubscribe handlers.
         *
         * @param ispListener The subscriber requesting removal.
         * @param book        The order book to unsubscribe from.
         * @return true if the entry was present and removed, false if the
         * subscriber was not subscribed to @p book.
         *
         * @note Thread-safety: acquires bookLock_ internally.
         * @note Do NOT call from ~InfoSub(). Use unsubBookInternal instead
         * to avoid a redundant write-back to bookSubscriptions_ on a
         * partially-destroyed object.
         */
        virtual bool
        unsubBook(ref ispListener, Book const&) = 0;

        /**
         * Remove a book subscription during InfoSub teardown.
         *
         * Removes only the server-side entry from subBook_. Does NOT touch
         * InfoSub::bookSubscriptions_ because the InfoSub is being destroyed.
         * Called by ~InfoSub() for each book in bookSubscriptions_.
         *
         * @param uListener The sequence number of the subscriber being torn down.
         * @param book      The order book entry to remove.
         * @return true if the entry was present and removed, false otherwise
         * (e.g., already removed by a concurrent RPC unsubscribe).
         *
         * @note Thread-safety: acquires bookLock_ internally.
         */
        virtual bool
        unsubBookInternal(std::uint64_t uListener, Book const&) = 0;

        virtual bool
        subTransactions(ref ispListener) = 0;
        virtual bool
        unsubTransactions(std::uint64_t uListener) = 0;

        virtual bool
        subRTTransactions(ref ispListener) = 0;
        virtual bool
        unsubRTTransactions(std::uint64_t uListener) = 0;

        virtual bool
        subValidations(ref ispListener) = 0;
        virtual bool
        unsubValidations(std::uint64_t uListener) = 0;

        virtual bool
        subPeerStatus(ref ispListener) = 0;

        virtual bool
        unsubPeerStatus(std::uint64_t uListener) = 0;
        virtual void
        pubPeerStatus(std::function<json::Value(void)> const&) = 0;

        virtual bool
        subConsensus(ref ispListener) = 0;
        virtual bool
        unsubConsensus(std::uint64_t uListener) = 0;

        virtual void
        subMPT(InfoSub::ref ispListener, hash_set<MPTID> const& mptIDs) = 0;
        virtual void
        unsubMPT(InfoSub::ref ispListener, hash_set<MPTID> const& mptIDs) = 0;

        // VFALCO TODO Remove
        //             This was added for one particular partner, it
        //             "pushes" subscription data to a particular URL.
        //
        virtual pointer
        findRpcSub(std::string const& strUrl) = 0;
        virtual pointer
        addRpcSub(std::string const& strUrl, ref rspEntry) = 0;
        virtual bool
        tryRemoveRpcSub(std::string const& strUrl) = 0;

        /**
         * Journal used by InfoSub for diagnostics that occur after the
         * owning subsystem (e.g. application-level Logs) is the only
         * surviving sink — primarily destructor-time cleanup failures.
         */
        [[nodiscard]] virtual beast::Journal const&
        journal() const = 0;
    };

public:
    InfoSub(Source& source);
    InfoSub(Source& source, Consumer consumer);

    virtual ~InfoSub();

    Consumer&
    getConsumer();

    virtual void
    send(json::Value const& jvObj, bool broadcast) = 0;

    [[nodiscard]] std::uint64_t
    getSeq() const;

    /**
     * Return the number of subscriptions currently tracked on this
     * connection.
     *
     * The combined size of the per-connection account, real-time account, and
     * account-history subscription sets. `doSubscribe` reads this to enforce
     * the per-connection subscription cap before admitting more.
     *
     * @return The total tracked subscription count for this connection.
     *
     * @note Thread-safe: takes `lock_` for the read; read-only.
     */
    [[nodiscard]] std::size_t
    totalSubscriptionCount() const;

    /**
     * Enforce the cap and reserve a request's net-new accounts, atomically.
     *
     * Under one hold of `lock_`: count the net-new entries in the two sets,
     * check the total against @p cap, and insert them only if it fits.
     * All-or-nothing. Doing check and insert together stops two concurrent
     * requests sharing an InfoSub (the admin subscribe-by-url path) from both
     * passing the check before either records its accounts. The server-side
     * maps are populated afterwards by subAccount, whose re-insert is a no-op.
     *
     * @param proposedAccounts Real-time (accounts_proposed) ids to reserve.
     * @param normalAccounts   Normal (accounts) ids to reserve.
     * @param cap              The effective per-connection cap.
     * @return true if reserved; false if the request must be rejected.
     * @note Thread-safe: takes `lock_`.
     */
    [[nodiscard]] bool
    tryReserveAccountSubscriptions(
        hash_set<AccountID> const& proposedAccounts,
        hash_set<AccountID> const& normalAccounts,
        std::size_t cap);

    /**
     * Whether this connection already tracks an account-history for @p account.
     *
     * `doSubscribe` reads this to charge the cap for an account_history_tx_stream
     * only when it is net-new, matching the account branches.
     *
     * @param account The account an account_history_tx_stream would add.
     * @return true if @p account is already in the account-history set.
     * @note Thread-safe: takes `lock_`; read-only.
     */
    [[nodiscard]] bool
    hasAccountHistorySubscription(AccountID const& account) const;

    void
    onSendEmpty();

    void
    insertSubAccountInfo(AccountID const& account, bool rt);

    void
    deleteSubAccountInfo(AccountID const& account, bool rt);

    /**
     * Record that this subscriber is following @p book.
     *
     * Called by NetworkOPsImp::subBook so that ~InfoSub() can issue a
     * matching unsubBook for every book this subscriber is tracking,
     * keeping per-subscriber state symmetric with the server-side map.
     *
     * @param book The order book this subscriber has just subscribed to.
     * @note Idempotent: re-inserting an already-tracked book is a no-op.
     * @note Thread-safe: takes InfoSub::lock_.
     */
    void
    insertBookSubscription(Book const& book);

    /**
     * Stop tracking @p book for this subscriber.
     *
     * Called by the unsubscribe RPC handler so that the book is not
     * re-unsubscribed by ~InfoSub(). Pairs with insertBookSubscription.
     *
     * @param book The order book to forget.
     * @note No-op if @p book was not previously inserted.
     * @note Thread-safe: takes InfoSub::lock_.
     */
    void
    deleteBookSubscription(Book const& book);

    // return false if already subscribed to this account
    bool
    insertSubAccountHistory(AccountID const& account);

    void
    deleteSubAccountHistory(AccountID const& account);

    void
    clearRequest();

    void
    setRequest(std::shared_ptr<InfoSubRequest> const& req);

    std::shared_ptr<InfoSubRequest> const&
    getRequest();

    void
    setApiVersion(unsigned int apiVersion);

    [[nodiscard]] unsigned int
    getApiVersion() const noexcept;

    void
    insertSubMPTInfo(MPTID const& mptID);

    void
    deleteSubMPTInfo(MPTID const& mptID);

protected:
    // Mutable so the read-only totalSubscriptionCount() accessor can lock it
    // from a const method; locking semantics are otherwise unchanged.
    mutable std::mutex lock_;

private:
    Consumer consumer_;
    Source& source_;
    hash_set<AccountID> realTimeSubscriptions_;
    hash_set<AccountID> normalSubscriptions_;
    std::shared_ptr<InfoSubRequest> request_;
    std::uint64_t seq_;
    hash_set<AccountID> accountHistorySubscriptions_;
    hash_set<Book> bookSubscriptions_;
    hash_set<MPTID> mptSubscriptions_;
    unsigned int apiVersion_ = 0;

    static int
    assignId()
    {
        static std::atomic<std::uint64_t> kID(0);
        return ++kID;
    }
};

}  // namespace xrpl
