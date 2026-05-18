# `NFTInvariant.h` — NFToken Post-Transaction Integrity Guards

This header declares two invariant checker classes that the XRPL transaction engine runs unconditionally after every transaction applies to the ledger. They are part of the broader invariant-checking framework defined in `InvariantCheck.h`, which assembles all checkers into a `std::tuple<..., ValidNFTokenPage, NFTokenCountTracking, ...>` and drives them through a uniform `visitEntry` / `finalize` protocol.

The two-phase design is deliberate: because a transaction can touch an arbitrary number of ledger entries, invariant checkers accumulate observations during `visitEntry` calls (one call per modified `SLE`) and only render a verdict in `finalize` once the full picture is available. Both classes store only small, flat state — a handful of `bool` flags or `uint32_t` counters — making them cheap to run on every transaction regardless of outcome.

## `ValidNFTokenPage`

NFToken pages (`ltNFTOKEN_PAGE`) use a composite 256-bit key: the owning account occupies the high 160 bits, and the low 96 bits represent the page's *high limit* — the exclusive upper bound on which NFToken IDs (by their own low 96 bits) belong on that page. The `nft::pageMask` constant (`0x...ffffffffffffffffffffffff`) cleanly separates these two parts.

`visitEntry` ignores entries that are not `ltNFTOKEN_PAGE` and applies a `check` lambda to both the `before` and `after` SLE states when they exist. This dual inspection matters: it catches not just a corrupted write but also confirms that entries which already existed on disk satisfy the structural invariants.

**Linking checks** (`badLink_`): `sfPreviousPageMin` and `sfNextPageMin` links are verified in both directions. For each neighbour link, the checker masks out the account bits and confirms they match the current page's account — a cross-account link is impossible by design and would indicate serious corruption. It also enforces page ordering: `prev.hiLimit < current.hiLimit < next.hiLimit`, because the NFToken directory is a doubly-linked sorted list of pages.

**Size checks** (`invalidSize_`): A page must contain at least one and at most `dirMaxTokensPerPage` (32) NFTokens. The only exception is during deletion (`isDelete == true`), where a page being removed may legitimately be empty.

**Placement checks** (`badEntry_`): Each NFToken's ID, masked to its low 96 bits, must fall within `[loLimit, hiLimit)` of the page. `loLimit` is derived from `sfPreviousPageMin`'s low 96 bits, or zero if the page has no predecessor. An NFToken found outside this range would mean the page-splitting/merging logic placed a token on the wrong page.

**Sort checks** (`badSort_`): Tokens within a page must be in ascending order under `nft::compareTokens()`. The check maintains a running lower bound `loCmp`, starting at `loLimit`, and flags any token that is not strictly greater.

**URI checks** (`badURI_`): If a token carries an `sfURI` field, that field must be non-empty. An empty URI is explicitly prohibited by the NFToken specification.

**Amendment-gated checks**: Two additional invariants are enabled only when the `fixNFTokenPageLinks` amendment is active, reflecting that these checks address a historical bug rather than a founding invariant:

- `deletedFinalPage_`: The "final" page of an account's NFToken directory is identified by its low 96 bits all being `1` (equal to `nft::pageMask`). This page must not be deleted while `sfPreviousPageMin` still exists — doing so would orphan all earlier pages in the directory.

- `deletedLink_`: If a non-final page transitions from having `sfNextPageMin` to not having it (without being deleted), a forward link has been silently lost, which would break forward traversal of the directory.

Gating these behind the amendment avoids penalising historical ledger states where the bug may already have occurred while still protecting future transactions once the fix is deployed.

## `NFTokenCountTracking`

Every `ltACCOUNT_ROOT` entry carries two optional counters: `sfMintedNFTokens` and `sfBurnedNFTokens`. `NFTokenCountTracking` sums these fields across all account roots touched by the transaction — both the `before` state (summed into `beforeMintedTotal`/`beforeBurnedTotal`) and the `after` state (summed into `afterMintedTotal`/`afterBurnedTotal`). Absent fields are treated as zero via `.value_or(0)`.

The `finalize` logic branches on whether the transaction holds the `changeNFTCounts` privilege (declared in `InvariantCheckPrivilege.h` as bit `0x0020`). Transactions without this privilege — the vast majority — must leave both totals completely unchanged. This is a strong guard against any code path that accidentally modifies mint/burn counters as a side effect.

For `ttNFTOKEN_MINT`, three rules apply:
- A successful mint (`isTesSuccess(result)`) must strictly increase `afterMintedTotal` beyond `beforeMintedTotal`.
- A failed mint must not change `afterMintedTotal` at all.
- Neither success nor failure may change `afterBurnedTotal`.

For `ttNFTOKEN_BURN`, a symmetric set of rules applies with the burned and minted roles swapped.

The asymmetric treatment of success vs. failure is important: the invariant system intentionally checks failed transactions. A bug or exploit that mutates counters during a failed transaction would still be caught here, before the corrupted state is committed to the ledger.

## Relationship to the Broader Invariant System

Both classes conform to the `InvariantChecker_PROTOTYPE` interface documented in `InvariantCheck.h`. The engine instantiates all checkers as value types in a tuple, calls `visitEntry` for every modified SLE via tuple iteration, then calls `finalize` on each. A `false` return from any checker causes the transaction to be rolled back with a `tecINVARIANT_FAILED` error code, making these checks a last-resort safety net that fires after all normal transaction logic has completed.