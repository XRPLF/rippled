# `include/xrpl/ledger/Dir.h` — Ledger Directory Iterator

## Role in the System

Ledger directories in XRPL are linked lists of `DirectoryNode` SLEs (`ltDIR_NODE`), each storing a `STVector256` (`sfIndexes`) — a page of 256-bit keys pointing to other ledger objects. These paged structures are used to associate collections of objects with a root key, such as all NFT buy or sell offers for a given token. `Dir` wraps that storage model in a clean C++ forward-iterable range, hiding the page-chasing and SLE loading behind a familiar `begin()`/`end()` interface.

As of mid-2024 the class is used in two production contexts: `keylet::nft_buys()` and `keylet::nft_sells()` directories in `NFTokenHelpers.cpp`, and `keylet::ownerDir()` in several unit test files (Escrow, PayChan). The header comment notes this explicitly — the class was designed with generality in mind but its actual deployment is deliberately narrow.

## Data Model

A directory's root `Keylet` identifies the first `DirectoryNode` page. Each page SLE carries:

- `sfIndexes` — a `STVector256` of object keys stored on this page.
- `sfIndexNext` — a `uint64` giving the page number of the next page (`0` = last page).

Subsequent pages are fetched via `keylet::page(root, sfIndexNext)`. Dereferencing any entry calls `view_->read(keylet::child(index_))`, which produces the `SLE` the directory entry points to.

## `Dir` Class

`Dir` itself is a thin range adaptor. Its constructor takes a `ReadView const&` and a root `Keylet`, immediately reads the root SLE, and caches `sfIndexes`. Construction is cheap — no per-entry loading happens yet.

`begin()` initialises a `const_iterator` that points at the first entry of the root page: it copies the cached root SLE and sets `it_` to `std::begin(*indexes_)`. If the root page is missing or empty the iterator is left in the same state as `end()`. `end()` returns an iterator with `page_.key == root_.key` and `index_ == beast::zero` (a zero-valued `uint256`), serving as a sentinel.

## `const_iterator` Design

The iterator carries two levels of state simultaneously:

- An inner `std::vector<uint256>::const_iterator it_` walking through the current page's `sfIndexes`.
- A `Keylet page_` identifying which `DirectoryNode` SLE is currently loaded.

`operator++()` simply advances `it_`. When it reaches `std::end(*indexes_)`, it delegates to `next_page()`, which reads `sfIndexNext` from the current SLE. A value of zero means the directory is exhausted: `page_` is reset to `root_` and `index_` to `beast::zero`, converging the iterator to the end-sentinel state. A non-zero value constructs a new `keylet::page(root_, next)`, reads the corresponding SLE, and sets `it_` to the beginning of its `sfIndexes`.

`operator*()` lazily loads the referenced object on first dereference using `view_->read(keylet::child(index_))`, storing the result in `mutable cache_`. The cache is cleared to `std::nullopt` on every advance (including page transitions), keeping invalidation tight without any more-complex bookkeeping.

`operator==()` compares `page_.key` and `index_`. An iterator is equal to `end()` when both conditions match: the page key has been reset to root and `index_` has been zeroed. Note that `operator==()` returns `false` if either view pointer is null, and an `XRPL_ASSERT` confirms that non-null comparisons only occur between iterators from the same view and root.

## `next_page()` as a Performance Accelerator

`next_page()` is intentionally exposed as a public member rather than being internal to `operator++()`. This allows callers to skip an entire page's individual entries and jump directly to the next `DirectoryNode`. The canonical use is in `nft::notTooManyOffers()`:

```cpp
Dir const buys(view, keylet::nft_buys(nftokenID));
for (auto iter = buys.begin(); iter != buys.end(); iter.next_page())
    totalOffers += iter.page_size();
```

Here, `page_size()` returns `indexes_->size()` — the count of entries on the current page — without loading any of the entries themselves. Calling `next_page()` as the loop increment skips the entire page instead of stepping through it entry by entry. This turns an O(n) traversal with n `ReadView::read()` calls into an O(p) traversal with only p page reads, where p is the number of pages. For the NFToken offer-count check this matters: burning an NFToken requires confirming the offer count is small enough to delete in a single transaction, a check that must be fast.

## Const-Safety and Ownership

Both `Dir` and `const_iterator` hold `ReadView const*`, ensuring directory traversal is strictly read-only. Write operations such as `dirInsert` and `dirRemove` are performed through `ApplyView` and are entirely separate concerns. `value_type` is `std::shared_ptr<SLE const>`, matching `ReadView::read()`'s return type and giving callers shared ownership of each SLE beyond the iterator's lifetime.