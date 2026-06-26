#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/server/Manifest.h>

namespace xrpl {

// Operations that clients may wish to perform against the network
// Master operational handler, server sequencer, network tracker

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

/** Manages a client's subscription to data feeds.
 *
 *  An InfoSub holds a non-owning reference to its `Source` (typically the
 *  process-wide `NetworkOPsImp`). The destructor reaches back into the
 *  `Source` to remove this subscriber from every server-side subscription
 *  map.
 *
 *  @note Lifetime contract: every `InfoSub` instance MUST be destroyed
 *        before the backing `Source`. NetworkOPsImp shutdown drops all
 *        subscriber strong refs before its own teardown to satisfy this.
 *  @note Thread-safety: per-instance state is guarded by `lock_`. The
 *        destructor reads tracking sets without taking `lock_` because
 *        the strong-pointer ref-count is zero at destruction time, so
 *        no other thread can be calling the public mutators.
 */
class InfoSub : public CountedObject<InfoSub>
{
public:
    using pointer = std::shared_ptr<InfoSub>;

    // VFALCO TODO Standardize on the names of weak / strong pointer type
    // aliases.
    using wptr = std::weak_ptr<InfoSub>;

    using ref = std::shared_ptr<InfoSub> const&;

    using Consumer = Resource::Consumer;

public:
    /** Abstracts the source of subscription data.
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
         *         subscriber was not subscribed to @p book.
         *
         * @note Thread-safety: acquires subLock_ internally.
         * @note Do NOT call from ~InfoSub(). Use unsubBookInternal instead
         *       to avoid a redundant write-back to bookSubscriptions_ on a
         *       partially-destroyed object.
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
         *         (e.g., already removed by a concurrent RPC unsubscribe).
         *
         * @note Thread-safety: acquires subLock_ internally.
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

        /** Journal used by InfoSub for diagnostics that occur after the
         *  owning subsystem (e.g. application-level Logs) is the only
         *  surviving sink — primarily destructor-time cleanup failures.
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

    void
    onSendEmpty();

    void
    insertSubAccountInfo(AccountID const& account, bool rt);

    void
    deleteSubAccountInfo(AccountID const& account, bool rt);

    /** Record that this subscriber is following @p book.
     *
     *  Called by NetworkOPsImp::subBook so that ~InfoSub() can issue a
     *  matching unsubBook for every book this subscriber is tracking,
     *  keeping per-subscriber state symmetric with the server-side map.
     *
     *  @param book The order book this subscriber has just subscribed to.
     *  @note Idempotent: re-inserting an already-tracked book is a no-op.
     *  @note Thread-safe: takes InfoSub::lock_.
     */
    void
    insertBookSubscription(Book const& book);

    /** Stop tracking @p book for this subscriber.
     *
     *  Called by the unsubscribe RPC handler so that the book is not
     *  re-unsubscribed by ~InfoSub(). Pairs with insertBookSubscription.
     *
     *  @param book The order book to forget.
     *  @note No-op if @p book was not previously inserted.
     *  @note Thread-safe: takes InfoSub::lock_.
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

protected:
    std::mutex lock_;

private:
    Consumer consumer_;
    Source& source_;
    hash_set<AccountID> realTimeSubscriptions_;
    hash_set<AccountID> normalSubscriptions_;
    std::shared_ptr<InfoSubRequest> request_;
    std::uint64_t seq_;
    hash_set<AccountID> accountHistorySubscriptions_;
    hash_set<Book> bookSubscriptions_;
    unsigned int apiVersion_ = 0;

    static int
    assignId()
    {
        static std::atomic<std::uint64_t> kID(0);
        return ++kID;
    }
};

}  // namespace xrpl
