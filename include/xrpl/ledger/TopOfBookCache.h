#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Protocol.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace xrpl {

/** One entry in the top-of-book cache.

    Records the keylet of the best-quality (lowest-keyed) directory page
    for a single order book at the time the entry was recorded.
*/
struct TopOfBookEntry
{
    /// Keylet of the best directory page for the book.
    uint256 firstPageKey;
    /// Quality bits encoded in firstPageKey (decoded for fast comparison).
    std::uint64_t bestQuality{0};
    /// Ledger sequence at which this entry was populated.
    LedgerIndex asOfLedger{0};
};

/** Cache of "best directory page" keylet per active order book.

    Reads of the top of an order book usually return the same directory page
    over and over, but `BookTip::step()` re-walks the SHAMap on every call.
    This cache memoizes that result. Lookups become a single hash-map probe;
    the SHAMap successor walk happens only on cold or invalidated entries.

    The cache is auxiliary — invalidating an entry is always safe, since the
    next read repopulates lazily via `ReadView::succ()`. That property is what
    lets the cache ship without an amendment.

    Maintenance rules, applied at the apply path:

    - **Offer inserted**: if the new offer's directory keylet is at-or-better
      than the cached top, update the entry. Otherwise no-op.
    - **Offer deleted**: if the deleted offer was on the cached top page,
      invalidate. Otherwise no-op.

    A best-page key is `keylet::quality(keylet::kBook(book), rate).key`. All
    pages of a single book share the same prefix, so lower uint256 key =
    better quality. Comparisons in this file rely on that ordering.
*/
class TopOfBookCache
{
public:
    TopOfBookCache() = default;

    /** Copy-construct (used when snapshotting open->closed ledger).

        Hit/miss/invalidation counters are not copied; only the data is.
    */
    TopOfBookCache(TopOfBookCache const& other);

    /** Move-construct by locking the source and stealing its map.

        Needed because views that own a cache (OpenView) are moveable;
        std::mutex is not, so the move is implemented via lock-and-move.
        Counters are not transferred.
    */
    TopOfBookCache(TopOfBookCache&& other);

    TopOfBookCache&
    operator=(TopOfBookCache const&) = delete;
    TopOfBookCache&
    operator=(TopOfBookCache&&) = delete;

    /** Look up the cached top of `book`.

        Returns std::nullopt on miss. Hit/miss counters are updated.
    */
    [[nodiscard]] std::optional<TopOfBookEntry>
    get(Book const& book) const;

    /** Record (or overwrite) a top-of-book entry for `book`.

        Called from the cold path after `succ()` discovers the first page.
    */
    void
    record(Book const& book, uint256 const& firstPageKey, LedgerIndex seq);

    /** Notify the cache that an offer was inserted into `book` at directory
        keylet `dirKey`.

        If the new keylet is better than (less than) the cached top, the entry
        is updated. If it is equal, no change. If worse, no change.

        If no entry exists for `book`, this is a no-op: the next read will
        populate from `succ()`.
    */
    void
    onOfferInsert(Book const& book, uint256 const& dirKey, LedgerIndex seq);

    /** Notify the cache that an offer was deleted from `book` at directory
        keylet `dirKey`.

        If the delete was on the cached top page, invalidate (the page may
        now be empty, or the offer count is irrelevant — next read repopulates).
        Otherwise no-op.
    */
    void
    onOfferDelete(Book const& book, uint256 const& dirKey);

    /** Drop the entry for `book` unconditionally.

        Used as a safety hatch and by tests.
    */
    void
    invalidate(Book const& book);

    /** Drop every entry. */
    void
    clear();

    [[nodiscard]] std::size_t
    size() const;

    [[nodiscard]] std::uint64_t
    hits() const noexcept
    {
        return hits_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t
    misses() const noexcept
    {
        return misses_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t
    invalidations() const noexcept
    {
        return invalidations_.load(std::memory_order_relaxed);
    }

    /** Operator-facing kill switch.

        When false, `BookTip` skips cache consults and writes entirely,
        falling back to plain `succ()`. Default is true.
    */
    [[nodiscard]] static bool
    enabled() noexcept;

    static void
    setEnabled(bool on) noexcept;

private:
    mutable std::mutex mutex_;
    std::unordered_map<Book, TopOfBookEntry> map_;
    mutable std::atomic<std::uint64_t> hits_{0};
    mutable std::atomic<std::uint64_t> misses_{0};
    std::atomic<std::uint64_t> invalidations_{0};
};

}  // namespace xrpl
