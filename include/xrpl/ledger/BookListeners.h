#pragma once

#include <xrpl/protocol/MultiApiJson.h>
#include <xrpl/server/InfoSub.h>

#include <memory>
#include <mutex>

namespace xrpl {

/** Per-book fan-out layer for WebSocket order-book subscriptions.
 *
 *  One instance exists for each `Book` (currency pair) that has at least one
 *  active subscriber. `OrderBookDB` owns and looks up instances via
 *  `getBookListeners()` / `makeBookListeners()`; callers hold references
 *  through the `pointer` alias.
 *
 *  Subscribers are stored as `InfoSub::wptr` (weak pointers) so that
 *  `BookListeners` does not extend the lifetime of the connection object.
 *  Dead entries are pruned lazily inside `publish()` when the weak pointer
 *  can no longer be locked.
 *
 *  All three public methods take `lock_` for their full duration, including
 *  across the `p->send()` calls in `publish()`. This favours correctness over
 *  throughput on high-subscriber-count books.
 */
class BookListeners
{
public:
    /** Shared-ownership handle used by `OrderBookDB` and callers. */
    using pointer = std::shared_ptr<BookListeners>;

    BookListeners() = default;

    /** Register a subscriber for this book.
     *
     *  Stores a weak pointer to @p sub, keyed by its sequence number, so that
     *  subsequent `publish()` calls deliver notifications to it.
     *
     *  @param sub The subscriber to add; must not be null.
     */
    void
    addSubscriber(InfoSub::ref sub);

    /** Unregister a subscriber by sequence number.
     *
     *  Removes the entry whose key equals @p sub. If no such entry exists
     *  (e.g. the subscriber was already pruned by a `publish()` call after
     *  disconnect), this is a no-op.
     *
     *  @param sub Sequence number returned by `InfoSub::getSeq()` for the
     *      subscriber to remove.
     */
    void
    removeSubscriber(std::uint64_t sub);

    /** Deliver a transaction notification to all live subscribers.
     *
     *  Iterates over the internal listener map and, for each live subscriber,
     *  attempts to insert its sequence number into @p havePublished. If the
     *  insertion succeeds (i.e. this subscriber has not yet received this
     *  transaction from another book), the version-appropriate JSON is
     *  dispatched via `InfoSub::send()`.
     *
     *  Dead weak pointers (subscribers that have disconnected) are erased
     *  in-place during the scan, providing lazy GC without a separate sweep.
     *
     *  @param jvObj Version-indexed JSON built once upstream; each subscriber
     *      receives the slice matching its negotiated API version.
     *  @param havePublished Per-transaction set of subscriber sequence numbers
     *      that have already been notified. Shared across every
     *      `BookListeners::publish()` call for the same transaction so that a
     *      client subscribed to multiple affected books receives the message
     *      only once. Passed by reference and mutated in-place.
     */
    void
    publish(MultiApiJson const& jvObj, hash_set<std::uint64_t>& havePublished);

private:
    std::recursive_mutex lock_;

    hash_map<std::uint64_t, InfoSub::wptr> listeners_;
};

}  // namespace xrpl
