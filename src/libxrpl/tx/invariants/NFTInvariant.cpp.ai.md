# NFTInvariant.cpp — NFT Ledger Invariant Checkers

`NFTInvariant.cpp` implements two post-transaction invariant checkers for the XRPL NFT subsystem: `ValidNFTokenPage` and `NFTokenCountTracking`. Both follow the ledger's two-phase invariant protocol — `visitEntry` is called for every ledger entry touched by a transaction (with `before` and `after` snapshots), then `finalize` is called once to render a verdict. If `finalize` returns `false`, the transaction is rejected with `tecINVARIANT_FAILED` and no state change is committed.

These classes are not called by application code directly; they are instantiated and driven by the invariant-check harness inside the transaction application pipeline, which maintains a heterogeneous list of invariant checker objects and dispatches to each one.

## NFToken Page Structure

Understanding the checks requires understanding how NFToken pages are addressed. Each `ltNFTOKEN_PAGE` ledger entry has a 256-bit key that simultaneously encodes two things: the high 160 bits (`accountBits = ~pageMask`) identify the owning account, and the low 96 bits (`pageBits = nft::pageMask`) represent the page's upper-bound discriminator — the highest NFT ID (in page-bits terms) that may reside on this page. The mask is defined in `nftPageMask.h` as 96 consecutive 1-bits in the low word:

```
pageMask = 0x0000000000000000000000000000000000000000ffffffffffffffffffffffff
```

This design means every page's address is self-describing: from the key alone you can derive both who owns it and where it sits in the sorted NFT ID space.

## ValidNFTokenPage

`ValidNFTokenPage` enforces the structural integrity of `ltNFTOKEN_PAGE` entries. Its `visitEntry` method ignores any SLE that isn't an NFT page (early-return type guard), then applies a `check` lambda to both the `before` and `after` states.

**Link integrity** is verified by inspecting `sfPreviousPageMin` and `sfNextPageMin` fields. For each link that is present, two conditions must hold: the high 160 bits of the linked key must match the current page's account bits (enforcing ownership), and the linked page's low 96 bits must be strictly less than (for previous) or strictly greater than (for next) the current page's discriminator. A violation sets `badLink_`.

**Size constraints** are checked against `dirMaxTokensPerPage` (32 tokens). A page being actively used must contain at least one and at most 32 tokens; an empty page is only permitted when it is in the process of being deleted (`isDelete == true`). A violation sets `invalidSize_`.

**Sorting** is enforced by iterating the `sfNFTokens` array and calling `nft::compareTokens` on each consecutive pair, starting from a lower bound derived from the previous page link. Each token's ID must be strictly greater than the last. A violation sets `badSort_`.

**Page membership** is verified by extracting the page-bits of each token ID and confirming it falls in `[loLimit, hiLimit)`. A token with page-bits below the lower limit belongs on an earlier page; one at or above the upper limit belongs on a later one. A violation sets `badEntry_`.

**URI validity**: if a token carries an `sfURI` field, it must be non-empty. Storing an empty URI is protocol-invalid — the field should simply be absent. A violation sets `badURI_`.

Two additional checks appear in `visitEntry` outside the `check` lambda:

- **Deleted final page**: if the page being deleted has all 96 page-bits set to `1` (i.e., it is the highest-addressed page in the directory) but still has a `sfPreviousPageMin` link, deleting it would orphan the rest of the directory. This sets `deletedFinalPage_`.

- **Lost forward link**: for a non-final page transitioning from `before` to `after`, if `sfNextPageMin` was present in `before` but absent in `after`, the chain has been broken. This sets `deletedLink_`.

Both of these last two checks are gated in `finalize` behind `view.rules().enabled(fixNFTokenPageLinks)`. The amendment gating is deliberate: it allows the checks to be added to the invariant framework while historical ledger replay (which predates the fix) remains unaffected. Before the amendment activates, link corruption is logged but not enforced; after it activates, `finalize` returns `false`.

## NFTokenCountTracking

`NFTokenCountTracking` guards the global NFT mint and burn tallies stored on `ltACCOUNT_ROOT` entries as `sfMintedNFTokens` and `sfBurnedNFTokens`. The checker accumulates pre- and post-transaction totals across all account roots touched, using `value_or(0)` to handle accounts that have never minted or burned any NFTs.

The `finalize` logic branches on whether the transaction holds the `changeNFTCounts` privilege (a bitmask flag declared in `InvariantCheckPrivilege.h` and populated from a macro-driven transaction table). For transactions that lack this privilege, neither count may change — this catches any bug where a non-NFT transaction accidentally writes to these fields.

For privileged transactions, the rules are asymmetric and strict:

- A successful `ttNFTOKEN_MINT` must strictly increase `sfMintedNFTokens` (not merely equal — it must go up). The burned total must be unchanged.
- A failed `ttNFTOKEN_MINT` must leave both counts identical to their pre-transaction values.
- A successful `ttNFTOKEN_BURN` must strictly increase `sfBurnedNFTokens`. The minted total must be unchanged.
- A failed `ttNFTOKEN_BURN` must leave both counts identical.

The strict inequality (`>=` rather than `!=`) for successful mint/burn is important: it catches the case where the count wraps around or the field is incorrectly written back, not just the case where nothing changed. The choice to track global totals across all touched account roots rather than per-account makes the check simpler and still sound, because an NFT operation legitimately affects exactly one account's counters.

## Interaction with the Invariant Framework

Both classes accumulate state in boolean flags or integer accumulators that are set to failure-indicating values during `visitEntry`. All fatal log messages are emitted from `finalize`, which sees the final `ReadView` and the transaction's `TER` result code. This two-phase structure means every page touched by the transaction is checked before any failure is reported, giving the logs a complete picture even when multiple invariants are violated in a single transaction.