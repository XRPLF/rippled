# `src/libxrpl/ledger/Dir.cpp`

## Role in the System

XRPL's ledger organizes collections of related objects — such as all buy or sell offers for a given NFT — into paged linked-list structures called *directory nodes*. Each `DirectoryNode` (`ltDIR_NODE`) SLE holds a `STVector256` field (`sfIndexes`) containing a list of `uint256` keys pointing to the actual ledger objects on that page. Pages are chained together via a `sfIndexNext` field (a `uint64`; zero means last page). `Dir.cpp` wraps this storage model in a standard C++ forward-iterable range, hiding all the page-chasing and SLE loading behind a familiar `begin()`/`end()` interface suitable for range-based `for` loops.

As of mid-2024, `Dir` is used specifically for NFTokenOffer directories and unit tests. The parallel class `BookDirs` serves order-book directories, which require the additional complexity of traversing across quality-keyed subdirectories. `Dir` handles the simpler case where a single root keylet anchors a flat, linearly-paged directory.

## `Dir`: The Range Adaptor

`Dir` is intentionally thin. Its constructor takes a `ReadView const&` and a root `Keylet`, immediately reads the root SLE via `view_->read(root_)`, and stores a pointer to the `sfIndexes` vector within that SLE. Construction is cheap — no per-entry loading occurs. If `sle_` is `nullptr` (the directory root doesn't exist in the ledger), `indexes_` stays null, and `begin()` returns an iterator in the end-sentinel state, making an absent directory safely iterable as an empty range.

`end()` is always `const_iterator(*view_, root_, root_)` — a freshly-constructed iterator with `page_` set to `root_` and `index_` at its zero-initialized state. This choice matters for how equality comparison works.

## `const_iterator`: State Machine Across Pages

The iterator carries enough state to walk the multi-page structure:

- `sle_` — shared ownership of the current page's SLE, keeping it alive.
- `indexes_` — a raw pointer directly into `sle_`'s `sfIndexes` field data. This is safe because `sle_` keeps the SLE alive for the iterator's lifetime.
- `it_` — a `std::vector<uint256>::const_iterator` positioned within `*indexes_`.
- `index_` — the `uint256` key of the current entry (a copy, used as the canonical position marker).
- `page_` — the `Keylet` of the page currently being iterated.
- `cache_` — a `mutable std::optional<value_type>` for the SLE that `index_` points to.

### End Sentinel Encoding

Rather than a separate boolean flag, the end state is encoded structurally: `page_.key == root_.key && index_ == beast::zero`. Both `begin()` (when the directory is empty or missing) and `end()` produce iterators in this state, so `operator==` compares `page_.key` and `index_` to determine equality. Crucially, both iterators must share the same `view_` pointer and `root_.key`, which is enforced via `XRPL_ASSERT` in `operator==`. Comparing iterators from different directories is a programming error caught at assertion level, not a silent logic bug.

### Lazy Dereference with `cache_`

`operator*()` does not read the target SLE eagerly. It only calls `view_->read(keylet::child(index_))` on first access, storing the result in `cache_`. Advancing the iterator clears `cache_` by resetting it to `std::nullopt`. This is a meaningful optimization: callers that iterate directories to filter by key, without always needing the full SLE, avoid the cost of loading entries they don't use.

### Page Transitions via `next_page()`

When `operator++()` exhausts the current page (`++it_` reaches `std::end(*indexes_)`), it delegates to `next_page()`. This method reads `sfIndexNext` from the current SLE. A value of zero means the directory is fully traversed: `page_` is reset to `root_` and `index_` to `beast::zero`, converging the iterator to the end-sentinel. A non-zero value constructs `keylet::page(root_, next)`, reads the corresponding SLE, updates `indexes_` and `it_`, and loads the first entry key. An `XRPL_ASSERT` guards against a missing page SLE, since a non-zero `sfIndexNext` pointing to a non-existent page is a ledger integrity violation.

`next_page()` is also exposed as a public method on the iterator (declared in `Dir.h`), enabling callers to skip the remainder of the current page and jump directly to the start of the next one — an accelerated traversal pattern for code that processes one page at a time rather than one entry at a time.

## Relationship to `BookDirs`

`BookDirs.cpp` implements the analogous class for order books, using the lower-level `cdirFirst`/`cdirNext` helpers from `DirectoryHelpers.h`. `Dir` bypasses those helpers and accesses the directory SLE structure directly. The design tradeoff is that `Dir` is simpler and more transparent but assumes a single-root, quality-flat directory — assumptions that hold for NFT offer directories but not for order books spanning multiple quality levels.

## Invariants and Guards

- `index_ != beast::zero` is asserted before any dereference or increment, preventing use of a consumed or never-initialized iterator.
- `sle_ != nullptr` is checked softly (conditional) at construction time and asserted hard (`XRPL_ASSERT`) during page transitions, reflecting the difference between "directory doesn't exist yet" (legitimate) and "linked page is missing" (invariant violation).
- `indexes_->empty()` is checked before assigning `it_`, since an empty page at root level is valid (the root can be empty while subsequent pages are not), but advancing an iterator into an empty vector would be undefined behavior.