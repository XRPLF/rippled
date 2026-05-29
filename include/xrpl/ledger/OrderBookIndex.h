#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/ledger/detail/PersistentOrderTree.h>
#include <xrpl/protocol/Book.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace xrpl {

class ReadView;

/** Deterministic, ordered, **persistent** in-memory index of every active order
    book.

    `BookTip::step()` finds the next offer to cross by calling `ReadView::succ()`
    — an O(log N) SHAMap successor walk from the book root, re-done once per
    consumed offer. Profiling shows that walk is ~32% of crossing-apply cost.
    This index materializes the same quality-ordered offer sequence so iteration
    becomes an in-memory cursor advance instead of a trie re-walk.

    It generalizes `TopOfBookCache` from "the best directory page" to "the full
    ordered book". Like `FlatStateMap`, it is **auxiliary**: the SHAMap remains
    the authoritative state and the source of the consensus root. The index is
    rebuildable from the SHAMap at any time (`rebuildBook`) and differentially
    validated against it (`validateMatchesShaMap`); a divergence is a bug in the
    maintenance hooks, never a fallback.

    **Persistence.** Each book's offers live in an immutable, structurally-shared
    weight-balanced tree ([[detail/PersistentOrderTree.h]]). `clone()` copies only
    the per-book `shared_ptr` roots (O(#books)), not the offers — so the
    open-ledger copy-on-write (`OpenView` copy per `modify()`) preserves the index
    cheaply and it stays warm across transactions, instead of cold-starting and
    rebuilding per tx. Immutable nodes also make the COW rollback of a discarded
    sandbox free: it simply drops its own root pointers.

    Ordering invariant (the load-bearing property for bit-exact crossing):

      - Books are keyed by `Book` (which already carries the permissioned-DEX
        `domain`), so each book — open or domain — is indexed independently.
      - Within a book, the tree is keyed by `(dirRoot, insertSeq)`. `dirRoot` is
        the quality-directory root key; ascending == best-quality-first ==
        `succ()` order. `insertSeq` is a per-book monotonic counter capturing
        directory append order; since `dirRemove` preserves relative order and
        offer keys are never reused, in-order traversal reproduces the SHAMap
        directory walk byte-for-byte.

    Maintenance drives `insertOffer`/`deleteOffer` from the offer-mutation
    notifications (`notifyOfferInserted`/`notifyOfferDeleted`), which fire with
    the quality-directory root key and the offer key.
*/
class OrderBookIndex
{
public:
    OrderBookIndex() = default;

    /** Move-construct by locking the source and stealing its book map.
        Counters are not transferred (a fresh view starts its own accounting). */
    OrderBookIndex(OrderBookIndex&& other);

    OrderBookIndex(OrderBookIndex const&) = delete;
    OrderBookIndex&
    operator=(OrderBookIndex const&) = delete;
    OrderBookIndex&
    operator=(OrderBookIndex&&) = delete;

    /** Cheap structural copy: clones the per-book tree roots (O(#books)
        shared_ptr copies), sharing all offer nodes. Used by the `OpenView` copy
        ctor so the index stays warm across the open-ledger COW. Counters reset. */
    [[nodiscard]] OrderBookIndex
    clone() const;

    // --- maintenance (apply-path hooks) ---

    /** Record that `offerKey` was inserted into `book` at quality-directory root
        `dirRoot`. Appended (next insertSeq) so it sorts after same-level offers,
        preserving directory order. */
    void
    insertOffer(Book const& book, uint256 const& dirRoot, uint256 const& offerKey);

    /** Record that `offerKey` was removed from `book` at quality-directory root
        `dirRoot`. The book is dropped when it empties. Removing an absent key is
        a no-op. */
    void
    deleteOffer(Book const& book, uint256 const& dirRoot, uint256 const& offerKey);

    // --- ordered read access (BookTip seam) ---

    /** All offer keys of `book`, best-quality-first, directory order within a
        level. Empty if the book is absent. */
    [[nodiscard]] std::vector<uint256>
    flatten(Book const& book) const;

    /** The best (first) offer key of `book`, or nullopt if absent. */
    [[nodiscard]] std::optional<uint256>
    firstOffer(Book const& book) const;

    // --- rebuild / validation (composition with the authoritative SHAMap) ---

    /** Repopulate `book` from `view` by the canonical quality-ordered walk
        (`succ()` over directory roots + directory iteration within each). */
    void
    rebuildBook(ReadView const& view, Book const& book);

    /** True iff the maintained sequence for `book` equals a fresh walk of
        `view`. The differential invariant. */
    [[nodiscard]] bool
    validateMatchesShaMap(ReadView const& view, Book const& book) const;

    // --- bookkeeping ---

    /** True if `book` has an entry (at least one offer). O(1). Present implies
        non-empty (empty books are dropped). */
    [[nodiscard]] bool
    contains(Book const& book) const;

    void
    eraseBook(Book const& book);

    void
    clear();

    [[nodiscard]] std::size_t
    bookCount() const;

    [[nodiscard]] std::size_t
    offerCount(Book const& book) const;

    [[nodiscard]] std::uint64_t
    inserts() const noexcept
    {
        return inserts_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t
    deletes() const noexcept
    {
        return deletes_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t
    rebuilds() const noexcept
    {
        return rebuilds_.load(std::memory_order_relaxed);
    }

    // --- operator-facing kill switch (mirrors TopOfBookCache) ---

    [[nodiscard]] static bool
    enabled() noexcept;

    static void
    setEnabled(bool on) noexcept;

private:
    struct BookState
    {
        detail::OrderTreePtr root;  // persistent (dirRoot, insertSeq) -> offerKey
        std::uint64_t nextSeq{0};   // per-book monotonic append counter
    };

    // Canonical quality-ordered walk of `book` in `view`: (dirRoot, offerKey)
    // for each offer, best-quality-first, directory order within a level.
    [[nodiscard]] static std::vector<std::pair<uint256, uint256>>
    walkBook(ReadView const& view, Book const& book);

    mutable std::shared_mutex mutex_;
    std::unordered_map<Book, BookState> books_;
    std::atomic<std::uint64_t> inserts_{0};
    std::atomic<std::uint64_t> deletes_{0};
    std::atomic<std::uint64_t> rebuilds_{0};
};

}  // namespace xrpl
