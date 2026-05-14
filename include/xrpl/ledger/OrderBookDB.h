#pragma once

#include <xrpl/ledger/AcceptedLedgerTx.h>
#include <xrpl/ledger/BookListeners.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/MultiApiJson.h>
#include <xrpl/protocol/UintTypes.h>

#include <memory>
#include <optional>
#include <vector>

namespace xrpl {

/** Pure abstract index of all active order books across the ledger.
 *
 *  An order book is a directed trading pair — a set of open `ltOFFER` entries
 *  sharing the same "taker pays" (`in`) and "taker gets" (`out`) assets.
 *  Because pathfinding and client subscriptions both need fast lookups of
 *  which markets exist, this index is maintained separately from ledger state.
 *
 *  The interface lives in the public ledger layer; the concrete implementation
 *  (`OrderBookDBImpl`) is instantiated via `makeOrderBookDb()` and injected
 *  through the service registry, keeping heavy implementation details out of
 *  consumer headers.
 *
 *  @note All internal maps are guarded by a `std::recursive_mutex`. The
 *      expensive full-ledger scan in `setup()` builds new maps outside the
 *      lock and swaps them in a brief critical section, so reader calls are
 *      only briefly blocked rather than held for the duration of a full ledger
 *      traversal.
 */
class OrderBookDB
{
public:
    virtual ~OrderBookDB() = default;

    /** Notify the database that a new ledger has been accepted.
     *
     *  Triggers a throttled full-ledger scan when needed. The scan is skipped
     *  if the new ledger is within 25,600 sequences ahead of the last scanned
     *  ledger (incremental updates from `processTxn` keep the index current)
     *  or within 16 sequences behind it (small reorg). Outside these windows
     *  a full scan is scheduled — synchronously in standalone mode, or as a
     *  background job queue task otherwise. The scan rebuilds the book maps in
     *  local variables then swaps them under a lock to minimise reader
     *  contention.
     *
     *  @param ledger The accepted ledger to evaluate; the scan reads every
     *      `ltDIR_NODE` with an `sfExchangeRate` field and every `ltAMM`
     *      object to rebuild the in-memory book maps.
     */
    virtual void
    setup(std::shared_ptr<ReadView const> const& ledger) = 0;

    /** Register a single order book without triggering a full ledger scan.
     *
     *  Used to record a newly discovered book incrementally — for example,
     *  when a new offer type is seen in `processTxn` before the next scheduled
     *  full `setup()` scan.
     *
     *  @param book The directed trading pair to register.
     */
    virtual void
    addOrderBook(Book const& book) = 0;

    /** Return all order books whose "taker pays" side is @p asset.
     *
     *  The primary pathfinding query: given an asset a sender currently holds,
     *  enumerate every market where that asset can be spent. The pathfinding
     *  engine calls this at each hop to discover possible next steps toward
     *  the destination currency.
     *
     *  @param asset The asset the taker pays (the "in" side of the book).
     *  @param domain If provided, restricts results to books scoped to that
     *      permissioned domain; if absent, returns only global books.
     *  @return All `Book` objects with @p asset as their `in` side.
     */
    virtual std::vector<Book>
    getBooksByTakerPays(Asset const& asset, std::optional<Domain> const& domain = std::nullopt) = 0;

    /** Return the number of distinct "taker gets" assets available for @p asset.
     *
     *  Used as a breadth-limiting heuristic by the pathfinding engine: a large
     *  count signals a liquid hub currency; a small count may not warrant
     *  deeper exploration.
     *
     *  @param asset The asset the taker pays.
     *  @param domain If provided, counts only books in that permissioned domain;
     *      if absent, counts only global books.
     *  @return The number of order books whose "in" side matches @p asset.
     */
    virtual int
    getBookSize(Asset const& asset, std::optional<Domain> const& domain = std::nullopt) = 0;

    /** Return whether any order book exists that sells @p asset for XRP.
     *
     *  The implementation maintains a dedicated O(1) set (`xrpBooks_` /
     *  `xrpDomainBooks_`) so this check does not scan `allBooks_`. Pathfinding
     *  uses it to identify assets that can be liquidated directly to XRP
     *  without an intermediate hop.
     *
     *  @param asset The asset the taker pays.
     *  @param domain If provided, checks the permissioned-domain book set;
     *      if absent, checks the global book set.
     *  @return `true` if a book with @p asset as "in" and XRP as "out" exists.
     */
    virtual bool
    isBookToXRP(Asset const& asset, std::optional<Domain> const& domain = std::nullopt) = 0;

    /** Fan out a closed-ledger transaction to all relevant book subscribers.
     *
     *  Walks the transaction's metadata nodes looking for `ltOFFER` entries
     *  that were created, modified, or deleted and extracts their `TakerGets`
     *  and `TakerPays` fields. For each affected offer, the reversed book
     *  (`TakerGets` → `TakerPays`) is looked up in the listeners map and, if
     *  subscribers exist, `BookListeners::publish()` is called.
     *
     *  Deduplication is handled via a `hash_set<uint64_t> havePublished` local
     *  to each call: a subscriber whose sequence number is already in the set
     *  will not receive a second copy of the same transaction, even if multiple
     *  of its subscribed books were touched.
     *
     *  @note Only called for transactions with result `tesSUCCESS`.
     *
     *  @param ledger The closed ledger the transaction was applied to.
     *  @param alTx   The fully materialised transaction-in-ledger projection,
     *      including metadata.
     *  @param jvObj  Version-indexed JSON representation of the transaction,
     *      built once upstream and dispatched to subscribers by API version.
     */
    virtual void
    processTxn(
        std::shared_ptr<ReadView const> const& ledger,
        AcceptedLedgerTx const& alTx,
        MultiApiJson const& jvObj) = 0;

    /** Return the listener set for @p book, or `nullptr` if none exists.
     *
     *  Used when unsubscribing: a `nullptr` result means no entry needs to be
     *  updated. Avoids creating empty `BookListeners` objects for every book
     *  that passes through the system.
     *
     *  @param book The directed trading pair to look up.
     *  @return Shared pointer to the existing `BookListeners` for @p book, or
     *      `nullptr` if no subscribers are registered.
     */
    virtual BookListeners::pointer
    getBookListeners(Book const&) = 0;

    /** Return the listener set for @p book, creating it on demand.
     *
     *  Used when subscribing: if no `BookListeners` entry exists for the book,
     *  one is created and inserted into the map before returning.
     *
     *  @note Internally calls `getBookListeners()` under the same lock,
     *      which is why the implementation uses a `std::recursive_mutex`.
     *
     *  @param book The directed trading pair to look up or create.
     *  @return Shared pointer to the (possibly newly created) `BookListeners`
     *      for @p book; never `nullptr`.
     */
    virtual BookListeners::pointer
    makeBookListeners(Book const&) = 0;
};

}  // namespace xrpl
