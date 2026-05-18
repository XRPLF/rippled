# `AccountNFTs.cpp` — RPC Handler for `account_nfts`

## Role and Purpose

This file implements `doAccountNFTs`, the single entry point for the `account_nfts` JSON-RPC command. Its job is to enumerate all NFTs held in an account's NFToken page directory, delivering them in a consistent order with full support for cursor-based pagination. It sits alongside the other per-account RPC handlers (`AccountLines`, `AccountObjects`, `AccountOffers`, etc.) and follows the same request/response contract used across that family.

## The NFToken Storage Model

Understanding the code requires understanding how NFTs are stored on ledger. Rather than a flat list, an account's NFTs are distributed across a chain of `NFTokenPage` ledger objects. Each page is identified by a composite 256-bit key: the owner's 160-bit `AccountID` concatenated with a 96-bit suffix derived from the tokens it can hold. The pages form a singly-linked list via the `sfNextPageMin` field — each page holds a reference to the minimum key of the next page.

Within each page, tokens are sorted by their low 96 bits (the `nft::pageMask`: `0x00...00ffffffffffffffffffffffff`), which covers the issuer, taxon, and serial number fields packed into a `uint256` token ID. The high 160 bits (flags and transfer fee) are _not_ part of the sort key. This is a crucial detail that directly shapes the pagination logic.

The `NFTokenID` itself is a packed big-endian 256-bit field:
- **[255:240]** — 16-bit flags (`getFlags`)
- **[239:224]** — 16-bit transfer fee in tenths of a basis point (`getTransferFee`)
- **[223:64]** — 160-bit issuer `AccountID` (`getIssuer`)
- **[63:32]** — 32-bit ciphered taxon (`getTaxon`, XOR-deciphered on read)
- **[31:0]** — 32-bit mint serial number (`getSerial`)

## Request Validation

The handler opens with a standard validation sequence common to this handler family. `jss::account` is required, must be a string, and must decode successfully as a Base58 `AccountID`. The ledger is resolved via `RPC::lookupLedger`, which handles `ledger_hash`/`ledger_index` negotiation and returns an error result if the ledger cannot be found. Account existence is then confirmed with `ledger->exists(keylet::account(accountID))`.

The optional `limit` field is validated by `readLimitField` against `RPC::Tuning::accountNFTokens`, which sets a minimum of 20, default of 100, and maximum of 400. The optional `marker` must be a hex-encoded `uint256` — it is parsed via `marker.parseHex()` and fails early if malformed.

## Page Traversal and the Marker Protocol

Rather than scanning from page zero every time, the handler uses `ledger->succ(first.key, last.key.next())` to skip directly to the first page at or after the marker position. `keylet::nftpage(keylet::nftpage_min(accountID), marker)` produces the synthetic lower-bound key for this search. The `succ` call returns the actual next existing page key within the range `[first.key, last.key)`, so the initial read already skips empty ledger space efficiently.

The inner loop walks each page's `sfNFTokens` array and then follows `sfNextPageMin` to advance to the next page. The iteration terminates when `sfNextPageMin` is absent (end of the chain) or the count limit is reached.

### The Two-Level Marker Comparison

The trickiest logic in the file handles the position-finding within the first page. Because page sort order uses only the low 96 bits of the token ID, multiple tokens on the same page can share the same masked value (same issuer/taxon/serial, differing only in flags or fee). The handler addresses this with two flags and a two-level comparison:

1. **`maskedNftokenID < maskedMarker`** — Skip tokens that sort before the marker using page-ordering semantics.
2. **`maskedNftokenID == maskedMarker && nftokenID < marker`** — Within the same page-sort position, skip tokens whose full ID still precedes the marker.
3. **`nftokenID == marker`** — The exact marker token itself is skipped (it was already returned in the previous page of results); `markerFound` is set to `true`.

Once `pastMarker` is `true`, the loop emits tokens normally. If a marker was supplied but never found (`markerSet && !markerFound`) — checked both mid-loop after the first valid token and at loop exit — the handler returns `RPC::invalid_field_error(jss::marker)`. This guards against stale or corrupted markers that reference tokens no longer present in the ledger.

## Response Enrichment

The raw `STObject` for each NFT is serialized with `getJson(JsonOptions::none)`, but the token ID fields are decoded and injected as top-level response fields: `Flags`, `Issuer`, `NFTokenTaxon`, and `nft_serial`. This decomposition spares the caller from having to parse the packed 256-bit ID themselves. The `TransferFee` field is conditionally included — it is omitted entirely when zero, since a zero transfer fee is semantically equivalent to "not set."

## Pagination Output

When the count limit is hit mid-page, the handler writes `result[jss::limit]` and `result[jss::marker]` (set to the hex string of the last-emitted token's full `sfNFTokenID`) and returns immediately. On a complete scan with no truncation, `result[jss::account]` is set to the Base58 account string and `context.loadType` is set to `Resource::feeMediumBurdenRPC`, signaling to the fee framework that this is a moderately expensive query. The asymmetry — account is only echoed back in the non-truncated case — means clients should use the marker's presence, not the account field, to detect pagination.

## Relationship to Sibling Handlers

`AccountNFTs.cpp` is the NFT-specific counterpart to `AccountObjects.cpp`. `AccountObjects` enumerates all ledger object types in an owner directory; `AccountNFTs` dives specifically into the `NFTokenPage` chain with awareness of the NFT ID layout. The two share the same surrounding infrastructure (`lookupLedger`, `readLimitField`, `Tuning`, `JsonContext`) but differ in their iteration strategy: `AccountObjects` uses a general owner-directory walk, while this handler exploits the page-chain structure unique to NFTs for efficient prefix-skipping at the page level.