# `BookTip.h` — Raw Order Book Tip Iterator

`BookTip` is the lowest-level iterator over a DEX order book in the XRPL ledger. It traverses quality-sorted offer directories from best (highest) to worst (lowest) quality, unconditionally exposing every offer it encounters — including missing, unfunded, and invalid ones. Filtering those away is deliberately the responsibility of its caller, `TOfferStreamBase` / `FlowOfferStream`.

## Ledger Structure and Key-Space Design

XRPL order books are stored as a contiguous range of `uint256` directory keys. Each "quality bucket" is a directory node whose position in the key space encodes the exchange rate for offers it contains. `getBookBase(book)` computes the base key of the book's range (best quality), and `getQualityNext(m_book)` computes the exclusive upper bound for a quality-prefix search. The constructor initialises `m_book` and `m_end` from these values, establishing the full key-space range the iterator will walk.

Inside `step()`, the call `view_.succ(m_book, m_end)` does the heavy lifting: it returns the smallest existing key that is ≥ `m_book` and < `m_end` — in other words, the next occupied quality directory. This avoids linear key scanning and is O(log n) in the number of ledger entries.

## The Consume-Then-Advance Contract

`step()` operates in a *delete-then-seek* pattern. On every call after the first, if there is a current offer (`m_entry` is non-null), it immediately calls `offerDelete(view_, m_entry, j)` to remove that offer from the ledger view before searching for the next one. The `m_valid` flag gates this deletion on the first invocation (there is nothing to delete before the first `step()`).

This is an intentional design: `BookTip` is a *consuming* iterator. It never presents the same offer twice and leaves no consumed offer behind in the view. `OfferStream` is explicitly aware of this contract — the comment in `OfferStream.cpp` reads: *"BookTip::step deletes the current offer from the view before…"* — and it coordinates accordingly.

After finding a directory at `*first_page`, the code sets `m_book = *first_page` then decrements it (`--m_book`). This means the next `succ` call searches from just below the current directory. If the directory still has remaining offers (e.g., because `OfferStream` decided not to consume the offer via `BookTip` after all), `succ` will find it again on the next `step()`. If it was emptied, the decrement ensures `succ` naturally advances to the next quality bucket.

## Quality Extraction and State Exposure

When a valid directory is found, `step()` reads the quality directly out of the directory's key via `getQuality(*first_page)` — the quality value is embedded in the `uint256` index itself, which is a property of the XRPL key encoding scheme. The result is wrapped in a `Quality` value object and stored in `m_quality`.

The four accessors — `dir()`, `index()`, `quality()`, and `entry()` — are all `noexcept` and return const references to the corresponding state fields. Together they give the caller everything needed to inspect and act on the current offer: the directory key (`m_dir`), the offer's own ledger index (`m_index`), its exchange rate (`m_quality`), and its full SLE (`m_entry` as a `shared_ptr<SLE>`).

## Defensive Handling of Empty Directories

The `for (;;)` loop inside `step()` exists to handle an edge case the code explicitly acknowledges should never occur: an empty directory. If `dirFirst` finds no entries in a discovered directory, `m_book` is advanced to `*first_page` and the loop retries `succ` from there. Rather than asserting or crashing, `BookTip` silently skips the phantom directory. This is appropriate for consensus-critical ledger code where an assertion failure in a live validator would fork the network.

## Relationship to `OfferStream`

`BookTip` is always held as a member of `TOfferStreamBase`, never used standalone (except in `BookStep.cpp` for offer crossing). `OfferStream` wraps it with validity filtering: it discards offers whose SLE is missing, whose expiry has passed, and whose owner has insufficient funds. The clean separation — `BookTip` handles directory traversal and physical deletion, `OfferStream` handles semantic validity — keeps both classes focused and independently testable.

The requirement for `ApplyView` (rather than read-only `ReadView`) is a direct consequence of this design: because `BookTip` calls `offerDelete`, it must participate in the transactional apply context that governs all ledger mutations during payment path execution.