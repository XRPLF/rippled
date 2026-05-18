# `src/libxrpl/ledger/ApplyView.cpp` — Ledger Directory Management

## Role in the System

This file implements the XRPL ledger's **directory data structure** — the paged linked-list mechanism used to associate sets of ledger objects with an index key. Directories serve two distinct purposes: **owner directories** that track all objects (offers, trust lines, escrows, etc.) owned by a single account, and **book directories** that list all open offers at a specific price point in an order book. Both are stored as chains of `ltDIR_NODE` ledger entries. This file provides the concrete implementations of `ApplyView::dirAdd()`, `dirRemove()`, `emptyDirDelete()`, and `dirDelete()`, as well as the lower-level helpers in the anonymous `xrpl::directory` namespace.

## The Directory Data Structure

A directory is a circular doubly-linked list of pages, where page 0 is always the root. Each page is an `SLE` (`SLE` = State Ledger Entry) of type `ltDIR_NODE` holding up to `dirNodeMaxEntries` (32) `uint256` keys in its `sfIndexes` field. Pages carry `sfIndexNext` and `sfIndexPrevious` pointers; the root's `sfIndexPrevious` points to the last page in the chain, making it a circular list with O(1) access to both head and tail. The `sfRootIndex` field on every page records the directory's canonical key, enabling upward navigation.

## Helper Functions in `namespace directory`

These four functions are deliberately separated into a sub-namespace with a header-level warning ("Don't use them unless you really, really know what you're doing") because they expose the raw structural machinery that callers must sequence correctly.

**`createRoot()`** bootstraps a brand-new directory with a single entry. It allocates the root `SLE`, records the root index via `sfRootIndex`, invokes the caller-supplied `describe` callback to stamp type-specific metadata (e.g., `sfOwner` for owner directories, or `sfTakerPays`/`sfTakerGets` for book directories), pushes the first key, and inserts the SLE into the view. This callback pattern is the idiomatic way to keep the generic directory machinery decoupled from the specific ledger object types it manages.

**`findPreviousPage()`** navigates from the root to the last page via `sfIndexPrevious`. Because the root always points to the tail, this is O(1) regardless of chain length, which is important for write performance during heavy market-maker activity. If the previous page pointer is non-zero but the page cannot be found in the view, the function calls `LogicError` — a fatal termination indicating corrupted ledger state, not a recoverable error.

**`insertKey()`** handles the actual vector-level insertion into an existing page. The `preserveOrder` flag governs which of two strategies is used. When `true` (used by `dirAppend()` for offer-book directories), the key is appended at the tail to maintain insertion order. When `false` (used by `dirInsert()` for owner directories), the page is sorted first via `std::sort` — a legacy concession to the fact that pages may have been written by older code that did not maintain sort order — and `std::lower_bound` finds the correct insertion point. Both branches call `LogicError` on a duplicate key, as double-insertion is a programming error, not a protocol error.

**`insertPage()`** creates a new trailing page when the last page is full. It uses intentional unsigned arithmetic overflow of the `uint64_t` page counter to detect exhaustion: incrementing a max-value `uint64_t` wraps to zero, which is then treated as "out of pages." Two `static_assert` guards verify at compile time that the integer type is unsigned (making the overflow defined behavior per the C++ standard) and that the wrap-to-zero property actually holds. For post-`fixDirectoryLimit` ledgers, the older `dirNodeMaxPages` (262,144) ceiling is bypassed; the only limit is the `uint64_t` wrap. The new page is linked in at the tail by updating both the former-last-page's `sfIndexNext` and the root's `sfIndexPrevious`. The `describe` callback fires on each new page so type-specific fields propagate correctly beyond the first page.

## `ApplyView::dirAdd()` — the Insertion Dispatcher

The private `dirAdd()` method is the single entry point for all insertions. It first peeks at the root; if absent, it delegates to `createRoot()`. Otherwise it calls `findPreviousPage()` to retrieve the last page and its current entry count. If that page has room (`indexes.size() < dirNodeMaxEntries`), `insertKey()` is called directly. If the page is full, `insertPage()` is called with a hard-coded `nextPage = 0`, reflecting that new pages are always appended at the end of the chain — the reserved `sfIndexNext` field on a new non-root page is intentionally left unset (the commented-out code block documents this as reserved for a hypothetical future insertion-in-middle operation).

The public surface exposes `dirAppend()` (for offers, `preserveOrder = true`) and two overloads of `dirInsert()` (for owned objects, `preserveOrder = false`). `dirAppend()` additionally asserts that only `ltOFFER` types should use it, enforcing the separation between the two directory use cases.

## `dirRemove()` — Precise Removal with Page Cleanup

`dirRemove()` locates the target key by page number (callers store the page number alongside the object's ledger entry), removes it from the `sfIndexes` vector preserving relative order, and writes the update back. If the page remains non-empty after removal, the function returns immediately.

When a page becomes empty, the function enters a more complex cleanup path with distinct handling for the root page (page 0) versus interior/tail pages. The root is never deleted while `keepRoot` is true (callers like the owner-directory code retain the root when an account still exists). For non-root pages, the function unlinks the empty node from both its predecessor and successor, then erases it. A secondary check catches the edge case where the newly-adjacent `next` page is also empty and is the last page — a valid legacy state — in which case it too is reaped. Finally, if `keepRoot` is false and the chain has collapsed back to just an empty root, the root itself is erased, fully deleting the directory.

Structural consistency is verified at each step: missing neighbor pages, self-referential `sfIndexNext`/`sfIndexPrevious` on non-root nodes, and asymmetric forward/reverse links all trigger `LogicError` termination. These are marked `LCOV_EXCL_*` because reaching them in a well-functioning node indicates ledger database corruption, not an exercisable code path.

## `emptyDirDelete()` and `dirDelete()`

`emptyDirDelete()` is a narrower variant: it only deletes a root that is already empty, optionally cleaning up a single trailing empty page left by legacy code. It validates that the keylet refers to an `ltDIR_NODE` root before proceeding.

`dirDelete()` performs a bulk teardown by iterating all pages via `sfIndexNext` chaining, invoking a user-supplied callback for each `uint256` key encountered (allowing the caller to perform per-object cleanup), and erasing each page. This is used when an entire directory must be destroyed, such as when an account is permanently deleted from the ledger.

## Design Observations

The `describe` callback is a deliberate inversion of control: the directory machinery creates and links pages, but callers inject the type-specific fields, keeping `ltOFFER` and owner-directory logic out of generic code. The `keepRoot` flag on `dirRemove()` reflects the fact that an account's owner directory must persist as long as the account itself exists, even momentarily empty. The deliberate `uint64_t` overflow check in `insertPage()` — with both a conceptual comment and two compile-time assertions — is a rare, carefully documented instance of intentional unsigned wraparound used as a sentinel condition.