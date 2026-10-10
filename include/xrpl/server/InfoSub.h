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
#include <optional>
#include <string>

namespace xrpl {

// Operations that clients may wish to perform against the network
// Master operational handler, server sequencer, network tracker

/**
 * Maximum number of subscriptions a single client connection may hold at once.
 *
 * Applies to the account, real-time account, account-history, and MPT issuance
 * subscriptions tracked on one InfoSub (the sets counted by
 * totalSubscriptionCount), bounding the disconnect-time cleanup of those sets.
 * Book subscriptions are tracked separately (OrderBookDB) and are not counted
 * here. Generous enough for legitimate power users such as block explorers.
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

/**
 * Returns @p jvObj reshaped as a JSON-RPC 2.0 notification, or nullopt when the
 * message already has the shape @p apiVersion expects.
 *
 * From version 3 a message a subscription pushes is shaped as the
 * specification's notification: an object naming the protocol and the method,
 * the message's content under `params`, and no `id`. The `type` naming the
 * event becomes the `method`.
 *
 * A path finding update is one of these, though directed at the one subscriber
 * that asked: the specification allows one response per request and
 * `path_find create` consumed it. The `id` the client correlates by travels
 * inside `params`, where the update carries it.
 *
 * A message naming no `type`, or one whose `type` is not a string, names no
 * method, so it is passed through unshaped. Shaping reports that `type` as the
 * `method` and leaves none behind, so shaping a message a second time changes
 * nothing.
 *
 * @param jvObj The message to shape.
 * @param apiVersion The subscriber's API version.
 * @return The notification, or nullopt if no reshaping is needed.
 */
[[nodiscard]] std::optional<json::Value>
shapeAsNotification(json::Value const& jvObj, unsigned int apiVersion);

/**
 * Returns a copy of the stream error @p error naming @p stream as its `type`,
 * or nullopt when the error is sent as it stands.
 *
 * An error object names no `type`, so shapeAsNotification would pass it
 * through and a version 3 client could not classify it. The name is added only
 * where a notification is built from it: for a version below 3, or a subscriber
 * that cannot receive a notification, the object goes out unshaped and the
 * member would reach the wire undefined. Nothing is copied in that case.
 *
 * @param error The error a stream reports, as rpcError builds it.
 * @param apiVersion The subscriber's API version.
 * @param wantsNotifications Whether the subscriber can receive a notification.
 *        See InfoSub::wantsNotifications.
 * @param stream The stream the error concerns, which becomes the `method`.
 * @return The stamped copy, or nullopt if the error needs no name.
 */
[[nodiscard]] std::optional<json::Value>
stampStreamError(
    json::Value const& error,
    unsigned int apiVersion,
    bool wantsNotifications,
    json::StaticString const& stream);

class InfoSub;

/**
 * Sends @p jvObj to @p subscriber in the shape @p apiVersion expects.
 *
 * The version belongs to the subscription the message answers, so the caller
 * passes it from the entry it took the subscriber from. A `path_find` update
 * is served at the version its own request named, which can differ from the
 * version the connection's subscriptions are served at, and one webhook may
 * be shared by several callers.
 *
 * A shape is not transport-independent either, so a subscriber whose transport
 * cannot carry a notification is sent the message as the publisher built it.
 * See InfoSub::wantsNotifications.
 *
 * Every caller shapes for the one subscriber in front of it, so a stream event
 * reaching many subscribers is shaped once for each of them.
 *
 * @param subscriber Where the message is going.
 * @param apiVersion The version the subscription was registered at.
 * @param jvObj The message.
 * @param broadcast Whether the message was published to a stream rather than
 *        directed at this subscriber. Only a diagnostic reads it.
 */
void
sendShaped(
    InfoSub& subscriber,
    unsigned int apiVersion,
    json::Value const& jvObj,
    bool broadcast = true);

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
    using Wptr = std::weak_ptr<InfoSub>;

    using Ref = std::shared_ptr<InfoSub> const&;

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
        subAccount(
            Ref ispListener,
            HashSet<AccountID> const& vnaAccountIDs,
            bool realTime,
            unsigned int apiVersion) = 0;

        // for normal use, removes from InfoSub and server
        virtual void
        unsubAccount(Ref isplistener, HashSet<AccountID> const& vnaAccountIDs, bool realTime) = 0;

        // for use during InfoSub destruction
        // Removes only from the server
        virtual void
        unsubAccountInternal(
            std::uint64_t uListener,
            HashSet<AccountID> const& vnaAccountIDs,
            bool realTime) = 0;

        /**
         * subscribe an account's new transactions and retrieve the account's
         * historical transactions
         * @param apiVersion The version the subscription is registered at,
         *        which is the version its messages are shaped for.
         * @return rpcSUCCESS if successful, otherwise an error code
         */
        virtual ErrorCodeI
        subAccountHistory(Ref ispListener, AccountID const& account, unsigned int apiVersion) = 0;

        /**
         * unsubscribe an account's transactions
         * @param historyOnly if true, only stop historical transactions
         * @note once a client receives enough historical transactions,
         * it should unsubscribe with historyOnly == true to stop receiving
         * more historical transactions. It will continue to receive new
         * transactions.
         */
        virtual void
        unsubAccountHistory(Ref ispListener, AccountID const& account, bool historyOnly) = 0;

        virtual void
        unsubAccountHistoryInternal(
            std::uint64_t uListener,
            AccountID const& account,
            bool historyOnly) = 0;

        /**
         * Remove an MPT issuance subscription during InfoSub teardown.
         *
         * Removes only the server-side entry from subMPT_. Does NOT touch
         * InfoSub::mptSubscriptions_ because the InfoSub is being destroyed.
         * Called by ~InfoSub() for each issuance in mptSubscriptions_.
         *
         * @param uListener The sequence number of the subscriber being torn down.
         * @param mptID     The MPT issuance to remove.
         *
         * @note Thread-safety: acquires mptLock_ internally.
         */
        virtual void
        unsubMPTInternal(std::uint64_t uListener, MPTID const& mptID) = 0;

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
            HashSet<AccountID> rtAccounts,
            HashSet<AccountID> normalAccounts,
            HashSet<AccountID> historyAccounts) = 0;

        // VFALCO TODO Document the bool return value
        //
        // Every sub* below takes the version the subscription is registered at, which is the
        // version its messages are shaped for. Registering the same subscription again replaces
        // the entry, so the newest registration decides that subscription's version.
        virtual bool
        subLedger(Ref ispListener, json::Value& jvResult, unsigned int apiVersion) = 0;
        virtual bool
        unsubLedger(std::uint64_t uListener) = 0;

        virtual bool
        subBookChanges(Ref ispListener, unsigned int apiVersion) = 0;
        virtual bool
        unsubBookChanges(std::uint64_t uListener) = 0;

        virtual bool
        subManifests(Ref ispListener, unsigned int apiVersion) = 0;
        virtual bool
        unsubManifests(std::uint64_t uListener) = 0;
        virtual void
        pubManifest(Manifest const&) = 0;

        virtual bool
        subServer(Ref ispListener, json::Value& jvResult, bool admin, unsigned int apiVersion) = 0;
        virtual bool
        unsubServer(std::uint64_t uListener) = 0;

        virtual bool
        subBook(Ref ispListener, Book const&, unsigned int apiVersion) = 0;

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
        unsubBook(Ref ispListener, Book const&) = 0;

        /**
         * Remove a book subscription during InfoSub teardown.
         *
         * Removes only the server-side entry from subBook_. Does NOT touch
         * InfoSub::bookSubscriptions_ because the InfoSub is being destroyed.
         * Called by ~InfoSub() for each book in bookSubscriptions_.
         *
         * @param uListener The sequence number of the subscriber being torn
         *        down.
         * @param book      The order book entry to remove.
         * @return true if the entry was present and removed, false otherwise
         * (e.g., already removed by a concurrent RPC unsubscribe).
         *
         * @note Thread-safety: acquires bookLock_ internally.
         */
        virtual bool
        unsubBookInternal(std::uint64_t uListener, Book const&) = 0;

        virtual bool
        subTransactions(Ref ispListener, unsigned int apiVersion) = 0;
        virtual bool
        unsubTransactions(std::uint64_t uListener) = 0;

        virtual bool
        subRTTransactions(Ref ispListener, unsigned int apiVersion) = 0;
        virtual bool
        unsubRTTransactions(std::uint64_t uListener) = 0;

        virtual bool
        subValidations(Ref ispListener, unsigned int apiVersion) = 0;
        virtual bool
        unsubValidations(std::uint64_t uListener) = 0;

        virtual bool
        subPeerStatus(Ref ispListener, unsigned int apiVersion) = 0;

        virtual bool
        unsubPeerStatus(std::uint64_t uListener) = 0;
        virtual void
        pubPeerStatus(std::function<json::Value()> const&) = 0;

        virtual bool
        subConsensus(Ref ispListener, unsigned int apiVersion) = 0;
        virtual bool
        unsubConsensus(std::uint64_t uListener) = 0;

        virtual void
        subMPT(InfoSub::Ref ispListener, HashSet<MPTID> const& mptIDs, unsigned int apiVersion) = 0;
        virtual void
        unsubMPT(InfoSub::Ref ispListener, HashSet<MPTID> const& mptIDs) = 0;

        // VFALCO TODO Remove
        //             This was added for one particular partner, it
        //             "pushes" subscription data to a particular URL.
        //
        virtual pointer
        findRpcSub(std::string const& strUrl) = 0;
        virtual pointer
        addRpcSub(std::string const& strUrl, Ref rspEntry) = 0;
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

    /**
     * Deliver a message to this subscriber.
     *
     * @param jvObj The message.
     * @param broadcast True for a stream event published to every subscriber of
     *        that stream, false for one directed at this subscriber alone
     *        because it asked (a path finding update). Both are shaped the same
     *        way, since neither is a response. This reports only how far the
     *        message traveled, which is what a diagnostic reads.
     */
    virtual void
    send(json::Value const& jvObj, bool broadcast) = 0;

    /**
     * Whether a message sent to this subscriber may be shaped as a JSON-RPC
     * 2.0 notification.
     *
     * True for a subscriber the server writes a message to as it is, which
     * every WebSocket session is. False for one whose transport puts what it
     * is given inside another request, which a subscriber named by a url does:
     * a notification would reach it nested in a request and beside a member
     * the specification does not define, so it would not be a notification at
     * all. Such a subscriber receives the message as the publisher built it, at
     * every API version.
     *
     * A publisher that shapes a message asks this first.
     *
     * @return true if a notification is a shape this subscriber can receive.
     */
    [[nodiscard]] virtual bool
    wantsNotifications() const noexcept
    {
        return true;
    }

    [[nodiscard]] std::uint64_t
    getSeq() const;

    /**
     * Return the number of subscriptions currently tracked on this
     * connection.
     *
     * The combined size of the per-connection account, real-time account,
     * account-history, and MPT issuance subscription sets. `doSubscribe` reads
     * this to enforce the per-connection subscription cap before admitting more.
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
        HashSet<AccountID> const& proposedAccounts,
        HashSet<AccountID> const& normalAccounts,
        std::size_t cap);

    /**
     * Enforce the cap and reserve a request's net-new MPT issuances, atomically.
     *
     * The MPT analogue of tryReserveAccountSubscriptions: under one hold of
     * `lock_`, count the net-new issuances, check the total against @p cap, and
     * insert them only if it fits. All-or-nothing, so a rejected request records
     * nothing. Doing check and insert together stops two concurrent requests
     * sharing an InfoSub (the admin subscribe-by-url path) from both passing the
     * check before either records its issuances. The server-side map is
     * populated afterwards by subMPT, whose re-insert is a no-op.
     *
     * @param mptIDs The MPT issuance ids to reserve.
     * @param cap    The effective per-connection cap.
     * @return true if reserved; false if the request must be rejected.
     * @note Thread-safe: takes `lock_`.
     */
    [[nodiscard]] bool
    tryReserveMPTSubscriptions(HashSet<MPTID> const& mptIDs, std::size_t cap);

    /**
     * Whether this connection already tracks an account-history for @p account.
     *
     * `doSubscribe` reads this to charge the cap for an
     * account_history_tx_stream only when it is net-new, matching the account
     * branches.
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

    /**
     * Establishes @p apiVersion as the version every subscription on this
     * connection is served at.
     *
     * A connection serves one version: one message has one shape, and several
     * of its subscriptions may match it. The first `subscribe` decides it and
     * it is never cleared. A `subscribe` refused for another reason still
     * establishes it, the registrations it makes being spread through the
     * handler.
     *
     * For the subscribe path alone. Nothing reads this value back: a publisher
     * reads the version recorded on the subscription it took the sink from.
     *
     * @param apiVersion The version the `subscribe` named.
     * @return nullopt when @p apiVersion is this connection's version;
     *         otherwise the version already established.
     * @note Thread-safe: takes `lock_`.
     */
    [[nodiscard]] std::optional<unsigned int>
    establishSubscriptionVersion(unsigned int apiVersion);

    void
    insertSubMPTInfo(MPTID const& mptID);

    void
    deleteSubMPTInfo(MPTID const& mptID);

protected:
    // Mutable so the read-only totalSubscriptionCount() accessor can lock it
    // from a const method; locking semantics are otherwise unchanged.
    mutable std::mutex lock_;

private:
    // The lock type guarding this instance's subscription sets.
    using ScopedLock = std::scoped_lock<decltype(lock_)>;

    /**
     * The combined tally the per-connection cap is enforced against.
     *
     * @param lock Proof that `lock_` is held; unused otherwise.
     */
    [[nodiscard]] std::size_t
    subscriptionCount(ScopedLock const& lock) const;

    Consumer consumer_;
    Source& source_;
    HashSet<AccountID> realTimeSubscriptions_;
    HashSet<AccountID> normalSubscriptions_;
    std::shared_ptr<InfoSubRequest> request_;
    std::uint64_t seq_;
    HashSet<AccountID> accountHistorySubscriptions_;
    HashSet<Book> bookSubscriptions_;
    HashSet<MPTID> mptSubscriptions_;
    // The API version every subscription on this connection was registered at, set by the first
    // `subscribe` and never cleared. Guarded by lock_. Read only by establishSubscriptionVersion,
    // so no publisher can reach it.
    std::optional<unsigned int> subscriptionApiVersion_;

    /**
     * The next sequence number, which is what identifies a connection.
     *
     * 64 bits wide, as the counter and `seq_` are, so no two live connections
     * share an identity.
     *
     * @return A sequence number no live connection holds.
     */
    static std::uint64_t
    assignId()
    {
        static std::atomic<std::uint64_t> kID(0);
        return ++kID;
    }
};

}  // namespace xrpl
