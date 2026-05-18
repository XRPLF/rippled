# BookDirs.cpp

## Role in the System

`BookDirs.cpp` implements the `BookDirs` class, which provides a standard forward-iterator range over every offer sitting in a specific XRPL order-book. An order book in XRPL is identified by a `Book` — a pair of currency/issuer specifications — and its offers are not stored in a flat list but in a two-level, quality-bucketed directory structure in the ledger's state map. `BookDirs` exists to hide that structure behind a clean range interface, making it possible to write `for (auto const& offer : BookDirs(view, book))` with no knowledge of the underlying directory layout.

## The Directory Structure It Traverses

XRPL encodes exchange rate (quality) directly into the ledger key of each offer's directory page. `getBookBase(book)` computes the starting 256-bit key that represents quality zero for the given book, and `getQualityNext(root_)` produces the key immediately after the highest possible quality value — effectively the exclusive upper bound of the quality key range for that book. Any ledger key in the half-open interval `[root_, next_quality_)` is, by construction, a directory page for one specific exchange rate in this book.

Within a single quality directory there may be multiple linked pages, each holding a `sfIndexes` vector of offer keys. The `cdirFirst`/`cdirNext` helpers from `DirectoryHelpers` walk that page chain, exposing one entry at a time.

## Constructor: Eager State Seeding

The constructor does more than initialize fields — it performs the first real ledger lookup. `view_->succ(root_, next_quality_)` scans the ledger's SHAMap for the smallest key strictly greater than `root_` that is also less than `next_quality_`. This is the first quality directory that actually contains offers. If the book is entirely empty, `succ` returns nothing and `key_` is set to `beast::zero`, which propagates throughout the object as the "empty book" sentinel.

When a directory is found, the constructor immediately calls `cdirFirst` to load the first page into `sle_` and position `entry_` and `index_` at the first offer. This pre-flight work is intentional: `begin()` copies this already-computed state directly into the returned iterator, so the first dereference costs only a single `view_->read` call to load the offer SLE.

## begin() / end() Symmetry

Both `begin()` and `end()` construct a `const_iterator` via the same private constructor that sets `cur_key_` equal to `key_`. The difference is that `begin()` additionally propagates `next_quality_`, `sle_`, `entry_`, and `index_` from the pre-seeded `BookDirs` state. An iterator at the end position is one where `index_` is `beast::zero` and `cur_key_` equals `key_` (the starting key) — a state that `operator++` deliberately reinstates when iteration is exhausted. This symmetric representation means the iterator range is well-defined and comparisons between begin and end naturally resolve to "equal when exhausted."

## Increment: Cross-Directory Navigation

`operator++` is the most complex part. It first attempts `cdirNext` to advance to the next offer within the current page chain of the current quality directory. If the page chain is exhausted (`cdirNext` returns false and sets `index_` to zero), the iterator must move to the next quality level. It does this by calling `view_->succ(++cur_key_, next_quality_)`, incrementing `cur_key_` first so the scan starts strictly after the current quality directory's key. If `succ` finds nothing, the loop is over and the iterator resets to the end-sentinel state. If a new quality directory is found, `cdirFirst` positions at its first entry.

The guard `if (index_ == 0)` after `cdirNext` fails is necessary because `cdirNext` can return false in two situations: the current page has no `sfIndexNext`, and the page itself contained no entries. In the latter case `index_` remains non-zero and the fallback to `succ` is skipped — though in practice a well-formed ledger never has an empty directory, which is why the `cdirFirst` failure path is marked `UNREACHABLE` and excluded from coverage.

## Lazy Offer Loading with Cache Invalidation

`operator*` resolves `index_` (the offer key) to a full `SLE` via `view_->read(keylet::offer(index_))`, caching the result in `mutable cache_`. Every call to `operator++` clears `cache_` unconditionally. This lazy-load pattern avoids loading offer SLEs for entries that are iterated past with `operator++` without being dereferenced — important when scanning a large book looking for the first matching offer.

## Equality and Validity Invariants

`operator==` begins with a null-pointer check on both `view_` pointers, returning false rather than asserting if either iterator is default-constructed. This guards the sentinel-comparison in range-for loops where the end iterator may never have been assigned a view. After the null guard, an `XRPL_ASSERT` verifies that the two iterators share the same `view_` and `root_` — comparing iterators from different books or different ledger views is a programming error, not a recoverable condition. Equality itself is then determined by `entry_`, `cur_key_`, and `index_` together, which uniquely identifies a position in the two-level directory.

The static `j_` member on `const_iterator` is a null `beast::Journal` used by deprecated helper functions that require a journal but whose log output is not needed here. It is defined at file scope to satisfy the one-definition rule without introducing a per-instance overhead.