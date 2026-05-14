/** @file
 *  Defines `ApplyView`, the writable ledger view used during transaction
 *  application, and the `ApplyFlags` bitmask that configures each apply pass.
 *
 *  All state mutations produced by a transaction — trust-line updates, offer
 *  creation, account creation, fee destruction — flow through `ApplyView`.
 *  Changes are journaled and may be committed to the parent view or discarded
 *  atomically, enabling transactional rollback at every layer of the view
 *  hierarchy.
 */

#pragma once

#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/RawView.h>
#include <xrpl/ledger/ReadView.h>

namespace xrpl {

/** Bitmask of flags that configure how a transaction apply pass behaves.
 *
 *  Carried through every transaction-application call site.  Multiple flags
 *  may be combined with `operator|`.  All bitwise operators use `safeCast`
 *  to prevent silent conversion to the underlying integer type.
 *
 *  @note Correctness and commutativity of `operator|` and `operator&` are
 *      verified by `static_assert` at compile time, guarding against future
 *      value collisions.
 */
// Bitwise flag enum with existing operator overloads
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum ApplyFlags : std::uint32_t {
    /** No flags set; default processing. */
    TapNone = 0x00,

    /** Transaction originated locally with `fail_hard` set.
     *
     *  The engine must not retry; a hard failure that claims fees is
     *  produced instead of a soft retry.
     */
    TapFailHard = 0x10,

    /** This is not the transaction's final pass.
     *
     *  Soft failures (insufficient balance, wrong sequence) are allowed
     *  because the transaction may succeed in a later pass.
     */
    TapRetry = 0x20,

    /** Transaction arrived from a trusted, privileged source.
     *
     *  Certain per-transaction limits are relaxed (e.g., path count).
     */
    TapUnlimited = 0x400,

    /** Transaction is being processed as part of a batch transaction. */
    TapBatch = 0x800,

    /** Dry-run simulation: apply the transaction without committing state.
     *
     *  Signature checks are skipped.  A full `TxMeta` is still produced so
     *  callers can inspect the outcome.  Used by the `simulate` RPC handler.
     */
    TapDryRun = 0x1000
};

/** Combine two `ApplyFlags` values. */
constexpr ApplyFlags
operator|(ApplyFlags const& lhs, ApplyFlags const& rhs)
{
    return safeCast<ApplyFlags>(
        safeCast<std::underlying_type_t<ApplyFlags>>(lhs) |
        safeCast<std::underlying_type_t<ApplyFlags>>(rhs));
}

static_assert((TapFailHard | TapRetry) == safeCast<ApplyFlags>(0x30u), "ApplyFlags operator |");
static_assert((TapRetry | TapFailHard) == safeCast<ApplyFlags>(0x30u), "ApplyFlags operator |");

/** Mask `ApplyFlags` values, retaining only the bits present in both operands. */
constexpr ApplyFlags
operator&(ApplyFlags const& lhs, ApplyFlags const& rhs)
{
    return safeCast<ApplyFlags>(
        safeCast<std::underlying_type_t<ApplyFlags>>(lhs) &
        safeCast<std::underlying_type_t<ApplyFlags>>(rhs));
}

static_assert((TapFailHard & TapRetry) == TapNone, "ApplyFlags operator &");
static_assert((TapRetry & TapFailHard) == TapNone, "ApplyFlags operator &");

/** Invert all bits of an `ApplyFlags` value. */
constexpr ApplyFlags
operator~(ApplyFlags const& flags)
{
    return safeCast<ApplyFlags>(~safeCast<std::underlying_type_t<ApplyFlags>>(flags));
}

static_assert(~TapRetry == safeCast<ApplyFlags>(0xFFFFFFDFu), "ApplyFlags operator ~");

/** Set-assign `ApplyFlags` bits from `rhs` into `lhs`. */
inline ApplyFlags
operator|=(ApplyFlags& lhs, ApplyFlags const& rhs)
{
    lhs = lhs | rhs;
    return lhs;
}

/** Clear `ApplyFlags` bits in `lhs` that are absent from `rhs`. */
inline ApplyFlags
operator&=(ApplyFlags& lhs, ApplyFlags const& rhs)
{
    lhs = lhs & rhs;
    return lhs;
}

//------------------------------------------------------------------------------

/** Writable view of a ledger used during transaction application.
 *
 *  Extends `ReadView` with a checkout-modify-commit protocol: callers
 *  `peek()` an SLE to obtain a mutable handle, mutate it in place, then
 *  call `update()` (or `erase()`) to journal the change.  `insert()` adds
 *  entries that were never checked out.  All deltas are buffered; calling
 *  `apply()` on the concrete subclass flushes them to the parent view.
 *  Discarding the view without calling `apply()` abandons all changes.
 *
 *  Also exposes directory management (`dirAppend`, `dirInsert`, `dirRemove`,
 *  `dirDelete`) and virtual payment hooks (`creditHookIOU`, `creditHookMPT`,
 *  `issuerSelfDebitHookMPT`, `adjustOwnerCountHook`) that `PaymentSandbox`
 *  overrides to prevent double-spend within a multi-hop payment path.
 *
 *  @invariant `update()` and `erase()` must be called with an SLE obtained
 *      from `peek()` on **the same view instance**.  Passing an SLE across
 *      view boundaries is undefined behavior, because each view journals its
 *      own deltas independently.
 */
class ApplyView : public ReadView
{
private:
    /** Insert a key into the directory, routing to append-tail or
     *  sorted-insert logic based on `preserveOrder`.
     *
     *  @param preserveOrder if `true`, append to tail (offer-book order);
     *      if `false`, insert in sorted position within each page.
     *  @param directory keylet of the directory root page.
     *  @param key the `uint256` key to insert.
     *  @param describe callback invoked on each newly allocated page SLE to
     *      brand it with type-specific fields (e.g., `sfOwner`).
     *  @return the 0-based page index where the key was stored, or
     *      `std::nullopt` if the page counter overflowed.
     */
    std::optional<std::uint64_t>
    dirAdd(
        bool preserveOrder,
        Keylet const& directory,
        uint256 const& key,
        std::function<void(std::shared_ptr<SLE> const&)> const& describe);

public:
    ApplyView() = default;

    /** Return the flags that govern this transaction apply pass.
     *
     *  Flags shape engine behavior: `TapRetry` allows soft failures,
     *  `TapFailHard` demands a fee-claiming hard failure, `TapDryRun`
     *  suppresses state commits, and `TapUnlimited` relaxes per-tx limits.
     *
     *  @return the `ApplyFlags` bitmask for this view.
     */
    [[nodiscard]] virtual ApplyFlags
    flags() const = 0;

    /** Check out a ledger entry for in-place mutation.
     *
     *  Returns an owning `shared_ptr<SLE>` whose contents may be freely
     *  modified.  The caller must pass the same pointer back to `update()`
     *  or `erase()` on **this** view instance when done; passing it to any
     *  other `ApplyView` is undefined behavior.
     *
     *  @param k keylet identifying the entry.
     *  @return a mutable handle to the SLE, or `nullptr` if `k` is not
     *      present in this view.
     */
    virtual std::shared_ptr<SLE>
    peek(Keylet const& k) = 0;

    /** Remove an entry previously checked out with `peek()`.
     *
     *  Journals a delete delta so the entry is absent when this view's
     *  changes are later committed.
     *
     *  @param sle a pointer obtained from `peek()` on this view instance.
     *
     *  @note The key is taken from the SLE's own key field.
     */
    virtual void
    erase(std::shared_ptr<SLE> const& sle) = 0;

    /** Insert a brand-new ledger entry that has no prior existence in this view.
     *
     *  The SLE must not have been obtained from `peek()`.  Its key must not
     *  already exist in this view.  The view takes ownership of the
     *  `shared_ptr`.
     *
     *  @param sle the new entry to insert.
     *
     *  @note The key is taken from the SLE's own key field.
     */
    virtual void
    insert(std::shared_ptr<SLE> const& sle) = 0;

    /** Journal modifications to a checked-out ledger entry.
     *
     *  Signals to the underlying delta table that the entry has changed and
     *  its new state must be written when this view's changes are committed.
     *  The entry's key must already exist.
     *
     *  @param sle a pointer obtained from `peek()` on this view instance.
     *
     *  @note The key is taken from the SLE's own key field.
     */
    virtual void
    update(std::shared_ptr<SLE> const& sle) = 0;

    //--------------------------------------------------------------------------

    /** Notification hook invoked whenever an IOU credit is made to an account.
     *
     *  The default implementation is a no-op; `PaymentSandbox` overrides it to
     *  record the credit in its `DeferredCredits` table so that subsequent
     *  `balanceHookIOU` calls subtract in-path credits from reported balances,
     *  preventing circular paths from manufacturing liquidity.
     *
     *  @param from the debited account (sender side of the trust line).
     *  @param to the credited account (receiver side of the trust line).
     *  @param amount the IOU amount being credited; must hold an `Issue`.
     *  @param preCreditBalance the sender's trust-line balance before the credit.
     *
     *  @note The `XRPL_ASSERT` in the default body verifies that `amount` holds
     *      an `Issue`; it fires in debug builds if the wrong asset type is passed.
     */
    virtual void
    creditHookIOU(
        AccountID const& from,
        AccountID const& to,
        STAmount const& amount,
        STAmount const& preCreditBalance)
    {
        XRPL_ASSERT(amount.holds<Issue>(), "creditHookIOU: amount is for Issue");
    }

    /** Notification hook invoked whenever an MPT credit is made to an account.
     *
     *  The default implementation is a no-op; `PaymentSandbox` overrides it to
     *  record the credit in its `DeferredCredits` table, enabling the same
     *  double-spend prevention as `creditHookIOU` but for MPT trust lines.
     *
     *  @param from the debited account.
     *  @param to the credited account.
     *  @param amount the MPT amount being credited; must hold an `MPTIssue`.
     *  @param preCreditBalanceHolder the holder's MPT balance before the credit.
     *  @param preCreditBalanceIssuer the issuer's `OutstandingAmount` before the
     *      credit (signed to accommodate transient overflow).
     *
     *  @note The `XRPL_ASSERT` in the default body verifies that `amount` holds
     *      an `MPTIssue`; it fires in debug builds if the wrong asset type is
     *      passed.
     */
    virtual void
    creditHookMPT(
        AccountID const& from,
        AccountID const& to,
        STAmount const& amount,
        std::uint64_t preCreditBalanceHolder,
        std::int64_t preCreditBalanceIssuer)
    {
        XRPL_ASSERT(amount.holds<MPTIssue>(), "creditHookMPT: amount is for MPTIssue");
    }

    /** Notification hook for MPT issuer self-debit via an owned sell offer.
     *
     *  Unlike IOU trust lines, MPT has no bi-directional issuer↔holder
     *  relationship that caps issuance.  When the payment engine processes a
     *  sell offer owned by the MPT issuer (in reverse order), it tentatively
     *  credits the holder first, which can transiently push `OutstandingAmount`
     *  beyond `MaximumAmount`.  A subsequent step then redeems MPT from the
     *  issuer, restoring `OutstandingAmount`.  The hook lets `PaymentSandbox`
     *  track the issuer's cumulative self-debit so that `balanceHookSelfIssueMPT`
     *  can cap available-to-issue at `origBalance − selfDebit` across the entire
     *  payment rather than trusting the transient ledger state.
     *
     *  The default implementation is a no-op.
     *
     *  @param issue the MPT issuance being self-debited.
     *  @param amount the quantity the issuer is selling (debiting to self).
     *  @param origBalance the issuer's `OutstandingAmount` at the start of the
     *      payment, before any path steps executed.
     */
    virtual void
    issuerSelfDebitHookMPT(MPTIssue const& issue, std::uint64_t amount, std::int64_t origBalance)
    {
    }

    /** Notification hook invoked when an account's owner count changes.
     *
     *  The default implementation is a no-op; `PaymentSandbox` overrides it to
     *  record the high-water owner count for each account touched during the
     *  payment, so that reserve checks reflect the peak count rather than the
     *  instantaneous count at any single path step.
     *
     *  @param account the account whose owner count is changing.
     *  @param cur the owner count before the change.
     *  @param next the owner count after the change.
     */
    virtual void
    adjustOwnerCountHook(AccountID const& account, std::uint32_t cur, std::uint32_t next)
    {
    }

    /** Append an entry to a directory, preserving insertion order.
     *
     *  New entries are always placed at the tail of the last page, maintaining
     *  chronological ordering within an offer-book directory.  This ordering
     *  is relied upon during offer matching: earlier offers at the same quality
     *  have priority.
     *
     *  @param directory keylet of the directory root (page 0).
     *  @param key keylet of the entry to append; must be of type `ltOFFER`.
     *  @param describe callback invoked on each newly allocated page SLE to
     *      brand it with type-specific fields.
     *  @return the 0-based page index where the entry was stored, or
     *      `std::nullopt` if the page counter overflowed the protocol maximum.
     *
     *  @note Only `ltOFFER` entries may be appended; passing any other keylet
     *      type triggers `UNREACHABLE` and returns `std::nullopt`.  Use
     *      `dirInsert` for non-offer entries.
     *  @note A root page is created automatically if the directory does not yet
     *      exist.  New pages are linked into the chain as needed.
     */
    /** @{ */
    std::optional<std::uint64_t>
    dirAppend(
        Keylet const& directory,
        Keylet const& key,
        std::function<void(std::shared_ptr<SLE> const&)> const& describe)
    {
        if (key.type != ltOFFER)
        {
            // LCOV_EXCL_START
            UNREACHABLE(
                "xrpl::ApplyView::dirAppend : only Offers are appended to "
                "book directories");
            // Only Offers are appended to book directories. Call dirInsert()
            // instead
            return std::nullopt;
            // LCOV_EXCL_STOP
        }
        return dirAdd(true, directory, key.key, describe);
    }
    /** @} */

    /** Insert an entry into a directory, maintaining per-page sorted order.
     *
     *  Each individual page is kept in sorted key order, but entries may span
     *  multiple pages so the overall directory is only loosely ordered.
     *  Because legacy pages may not be sorted, each touched page is re-sorted
     *  before the new key is binary-inserted.  Used for account-owned object
     *  directories (offers owned by an account, escrows, etc.).
     *
     *  @param directory keylet of the directory root (page 0).
     *  @param key the `uint256` key to insert.
     *  @param describe callback invoked on each newly allocated page SLE to
     *      brand it with type-specific fields.
     *  @return the 0-based page index where the entry was stored, or
     *      `std::nullopt` if the page counter overflowed the protocol maximum.
     *
     *  @note A root page is created automatically if the directory does not yet
     *      exist.  New pages are allocated and linked as needed.
     */
    /** @{ */
    std::optional<std::uint64_t>
    dirInsert(
        Keylet const& directory,
        uint256 const& key,
        std::function<void(std::shared_ptr<SLE> const&)> const& describe)
    {
        return dirAdd(false, directory, key, describe);
    }

    /** @copydoc dirInsert(Keylet const&, uint256 const&, std::function<void(std::shared_ptr<SLE> const&)> const&)
     *
     *  Convenience overload that extracts the `uint256` key from `key.key`.
     */
    std::optional<std::uint64_t>
    dirInsert(
        Keylet const& directory,
        Keylet const& key,
        std::function<void(std::shared_ptr<SLE> const&)> const& describe)
    {
        return dirAdd(false, directory, key.key, describe);
    }
    /** @} */

    /** Remove a single entry from a directory and collapse any resulting
     *  empty non-root pages.
     *
     *  After the key is removed, if the containing page becomes empty:
     *  - Non-root pages are unlinked and erased from the ledger.
     *  - The root page (page 0) is never erased unless `keepRoot` is `false`
     *    and the entire directory is now empty.
     *  - Legacy empty trailing pages left by older code are cleaned up
     *    opportunistically when the root page is touched.
     *
     *  @param directory keylet of the directory root (page 0).
     *  @param page the 0-based page index that contains `key`; obtained from
     *      the page number stored in the owning ledger entry.
     *  @param key the `uint256` key to remove.
     *  @param keepRoot if `true`, preserve the root page even if it becomes
     *      empty after the removal (the directory anchor remains in the ledger).
     *  @return `true` if the entry was found and removed; `false` if the page
     *      or the key was not found.
     *
     *  @note Throws `std::logic_error` if the directory linked-list pointers
     *      are found to be inconsistent (broken chain); this indicates ledger
     *      corruption and should never occur under normal operation.
     */
    /** @{ */
    bool
    dirRemove(Keylet const& directory, std::uint64_t page, uint256 const& key, bool keepRoot);

    /** @copydoc dirRemove(Keylet const&, std::uint64_t, uint256 const&, bool)
     *
     *  Convenience overload that extracts the `uint256` key from `key.key`.
     */
    bool
    dirRemove(Keylet const& directory, std::uint64_t page, Keylet const& key, bool keepRoot)
    {
        return dirRemove(directory, page, key.key, keepRoot);
    }
    /** @} */

    /** Delete every page of a directory, invoking a callback for each key.
     *
     *  Traverses the entire linked-list chain starting from page 0, erases
     *  each page SLE, and calls `callback` once per key stored in the
     *  directory.  Callers are responsible for cleaning up the objects
     *  referenced by those keys before or after this call.
     *
     *  @param directory keylet of the directory root (page 0).
     *  @param callback function called with each `uint256` key found in the
     *      directory before the page is erased.
     *  @return `true` if the root page was found and the directory was deleted;
     *      `false` if the root page does not exist.
     */
    bool
    dirDelete(Keylet const& directory, std::function<void(uint256 const&)> const&);

    /** Delete the root page of a directory if and only if it is empty.
     *
     *  Verifies that both `sfIndexes` is empty and the linked-list pointers
     *  indicate no other pages remain.  Legacy empty trailing pages (a known
     *  edge case from older code) are cleaned up as a side effect before the
     *  emptiness check.
     *
     *  @param directory keylet of the directory root page (`ltDIR_NODE`);
     *      must identify page 0 (the root).
     *  @return `true` if the directory was empty and was successfully erased;
     *      `false` if the directory was not found, contained entries, or had
     *      non-empty sub-pages.
     *
     *  @note Throws `std::logic_error` if the directory linked-list pointers
     *      are inconsistent; this indicates ledger corruption.
     */
    bool
    emptyDirDelete(Keylet const& directory);
};

/** Low-level primitives for building and modifying paged ledger directories.
 *
 *  These helpers implement the individual steps of the directory linked-list
 *  protocol: root creation, tail-page discovery, key insertion, and page
 *  allocation.  They are exposed so that specialised callers (tests, tooling)
 *  can exercise individual steps, but **transaction processors must always
 *  go through `ApplyView::dirAppend` / `dirInsert` / `dirRemove`** instead.
 *
 *  @warning Do not call these directly unless you fully understand the
 *      directory invariants and page-linking protocol.
 */
namespace directory {

/** Allocate and insert the root page (page 0) for a new directory.
 *
 *  Creates a fresh `ltDIR_NODE` SLE at `directory`, sets `sfRootIndex`,
 *  calls `describe` to brand it, stores `key` as the first `sfIndexes`
 *  entry, and inserts it into the view.
 *
 *  @param view the writable ledger view.
 *  @param directory keylet for the root page.
 *  @param key the first key to store in the new directory.
 *  @param describe callback to set type-specific fields on the root SLE.
 *  @return `0` — the root page index.
 */
std::uint64_t
createRoot(
    ApplyView& view,
    Keylet const& directory,
    uint256 const& key,
    std::function<void(std::shared_ptr<SLE> const&)> const& describe);

/** Locate the last used page in a directory by following `sfIndexPrevious`
 *  from the root.
 *
 *  The root's `sfIndexPrevious` field always points to the tail page (O(1)
 *  append guarantee).  If it is 0 the root itself is the tail.
 *
 *  @param view the writable ledger view.
 *  @param directory keylet of the directory root.
 *  @param start the root SLE (already peeked by the caller).
 *  @return a tuple of `(pageIndex, pageSLE, sfIndexes)` for the tail page.
 *  @throws std::logic_error if the back-pointer chain is broken.
 */
auto
findPreviousPage(ApplyView& view, Keylet const& directory, SLE::ref start);

/** Insert a key into the `sfIndexes` vector of an existing page SLE and
 *  commit the change via `view.update()`.
 *
 *  If `preserveOrder` is `true`, the key is appended at the end (offer-book
 *  order).  If `false`, the page is sorted first (to handle legacy unsorted
 *  pages), then the key is binary-inserted.  Double-insertion throws.
 *
 *  @param view the writable ledger view.
 *  @param node the page SLE to modify (must have been obtained via `peek()`).
 *  @param page the 0-based page index of `node`.
 *  @param preserveOrder `true` to append; `false` to sort-then-insert.
 *  @param indexes the current `sfIndexes` vector (mutated in place).
 *  @param key the key to insert.
 *  @return the page index (`page`) where the key was stored.
 *  @throws std::logic_error if `key` is already present in `indexes`.
 */
std::uint64_t
insertKey(
    ApplyView& view,
    SLE::ref node,
    std::uint64_t page,
    bool preserveOrder,
    STVector256& indexes,
    uint256 const& key);

/** Allocate a new trailing page, link it into the directory chain, and
 *  store the first key in it.
 *
 *  The new page number is computed as `page + 1`; unsigned wraparound to 0
 *  (verified by `static_assert`) signals overflow and causes `std::nullopt`
 *  to be returned.  The `fixDirectoryLimit` amendment lifts the legacy
 *  per-directory page cap.
 *
 *  @param view the writable ledger view.
 *  @param page the current last-page index (new page will be `page + 1`).
 *  @param node the current last-page SLE; its `sfIndexNext` is updated.
 *  @param nextPage reserved for future mid-chain insertion; must be `0`.
 *  @param next the root SLE; its `sfIndexPrevious` is updated to point to
 *      the new tail.
 *  @param key the first key to store on the new page.
 *  @param directory keylet of the directory root.
 *  @param describe callback to brand the new page SLE.
 *  @return the new page index, or `std::nullopt` on overflow or page-count
 *      limit violation.
 */
std::optional<std::uint64_t>
insertPage(
    ApplyView& view,
    std::uint64_t page,
    SLE::pointer node,
    std::uint64_t nextPage,
    SLE::ref next,
    uint256 const& key,
    Keylet const& directory,
    std::function<void(std::shared_ptr<SLE> const&)> const& describe);

}  // namespace directory
}  // namespace xrpl
