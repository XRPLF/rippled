# BookTip.cpp — Raw Order Book Cursor

`BookTip` is a low-level, destructive cursor over a single order book in the XRPL ledger. Its sole job is to present offers one at a time, in decreasing quality order (best exchange rate first), while physically removing each offer from the ledger as iteration advances. It lives at the innermost layer of the offer-crossing pipeline and is consumed directly by `TOfferStreamBase` / `FlowOfferStream`, which sit above it and handle validity filtering, expiry, and funding checks.

## The Ledger's Order Book Layout

XRPL stores order book offers in directory nodes whose 256-bit keys encode the book identity in the lower bits and the exchange rate (quality) in the upper 64 bits. `getBookBase(book)` produces the minimum-quality key for a given currency pair — numerically the lowest key in the book. `getQualityNext(m_book)` produces the first key that lies outside this book's quality range, acting as a sentinel for the upper bound.

Quality on XRPL is expressed as TakerPays/TakerGets. A *lower* numeric quality value means the taker pays less per unit — which is *better* for the taker. As a result, the book's directory keys increase numerically as quality decreases. `BookTip` traverses these keys from `getBookBase` upward toward `getQualityNext`, meaning it walks from best quality to worst quality, as documented in the header.

## Constructor

```cpp
BookTip::BookTip(ApplyView& view, Book const& book)
    : view_(view), m_book(getBookBase(book)), m_end(getQualityNext(m_book))
```

The constructor establishes two cursor sentinels: `m_book` (current scan position, initialized to the book's base) and `m_end` (exclusive upper bound). No ledger access happens here; the cursor starts in an invalid state (`m_valid = false`) until the first call to `step()`.

## The `step()` Protocol: Delete-then-Advance

`step()` is a "delete the previous, fetch the next" operation, which is the central design decision in this file. When called:

1. If the cursor holds a valid, non-null `m_entry` from the previous call, it calls `offerDelete(view_, m_entry, j)` to permanently remove that offer from the ledger, its owner directory, and the book directory. The `m_valid` guard on this block ensures no deletion happens on the very first call when there is no "previous" offer.

2. The function then enters an infinite loop probing `view_.succ(m_book, m_end)` to find the next directory page key strictly greater than `m_book` and less than `m_end`. If no such page exists, the book is exhausted and the method returns `false`.

3. On finding a candidate page, `dirFirst()` reads the page's first entry (index `[0]` in the `sfIndexes` array). If that succeeds, the cursor captures the directory key, the offer index, the offer SLE via `view_.peek`, and the quality from `getQuality(*first_page)`.

4. The positional update is the subtle part: `m_book` is first set to `*first_page`, then decremented by one (`--m_book`). On the *next* call to `step()`, `view_.succ(m_book, m_end)` with `m_book = *first_page - 1` will return `*first_page` again — the same directory — as long as it still has entries. After `offerDelete` removes the just-served offer from the directory, `dirFirst` will find the new head. This re-visitation continues until the directory is drained, at which point `dirFirst` fails, the code sets `m_book = *first_page` and loops around, advancing `succ` past the now-empty page to the next quality tier.

This design cleanly handles all offers at the same quality level without needing an inner loop or a separate "next-within-tier" cursor. The single position variable `m_book` drives both re-visitation and advancement.

## Defensive Handling of Empty Directories

The comment inside the loop says "there should never be an empty directory but just in case." If `dirFirst` returns false for a page returned by `succ`, the code sets `m_book = *first_page` and continues the loop without touching `m_book - 1`. This causes `succ` to seek past the empty page, cleanly handling what should be an impossible state without crashing or corrupting the cursor.

## What `BookTip` Does Not Do

The header is explicit: `BookTip` returns all offers, including those with missing ledger entries, unfunded balances, or invalid fields. It performs no validation beyond confirming that the ledger objects exist via `view_.peek`. The surrounding `TOfferStreamBase::step()` is responsible for filtering out stale and invalid offers, and it calls `BookTip::step()` in a loop until a usable offer is found or the book is exhausted.

Because `m_entry` is a `std::shared_ptr<SLE>`, the SLE object remains alive while the caller inspects it, even though it has been removed from the ledger in a prior `step()` call. The `entry()`, `index()`, `dir()`, and `quality()` accessors on the public interface expose a snapshot of the most recently advanced-to offer, valid only between consecutive calls to `step()`.

## Relationship to `OfferStream`

`TOfferStreamBase` holds a `BookTip tip_` member directly. Its own `step()` calls `tip_.step()`, which advances the raw cursor and deletes the previous raw offer. The stream layer then validates the newly exposed raw offer and, if invalid, calls `tip_.step()` again via the outer stream loop. Because `BookTip::step()` always deletes `m_entry` on entry, any offer the stream layer decides to skip is cleaned up automatically on the next advancement — the stream layer does not need to call a separate delete.