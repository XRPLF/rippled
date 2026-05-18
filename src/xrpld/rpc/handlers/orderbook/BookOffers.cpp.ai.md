# `BookOffers.cpp` — `book_offers` RPC Handler

## Role in the System

`BookOffers.cpp` is the server-side handler for the `book_offers` JSON-RPC method, one of the most commonly called APIs on the XRP Ledger. It lets clients enumerate active offers in a specific trading pair — essentially reading the order book for the XRPL's native decentralized exchange (DEX). The file contains three validation helpers and the entry-point function `doBookOffers`, which is dispatched by the RPC framework when a `book_offers` request arrives.

## Asset Model and the Two-Sided Book

The handler must resolve two sides of a trading pair from JSON input: `taker_pays` (what the taker offers) and `taker_gets` (what the taker receives). Each side is an `Asset`, which is a `std::variant<Issue, MPTIssue>`. An `Issue` covers both XRP (a currency with a sentinel XRP account) and IOUs (currency + issuer account); an `MPTIssue` represents a Multi-Purpose Token identified by a 192-bit `MPTID`. The `Book` struct wraps `Asset in`, `Asset out`, and an optional `domain` field for domain-scoped books, then gets forwarded verbatim to the ledger traversal layer.

## Three-Phase Validation Design

The input for each side of the book is validated in three distinct passes, deliberately separated into reusable functions:

**`validateTakerJSON`** enforces structural constraints on the raw JSON before any parsing. It rejects a taker object that has neither `currency` nor `mpt_issuance_id`, that mixes `mpt_issuance_id` with `currency` or `issuer` (MPT and IOU/XRP specifications are mutually exclusive), or that provides these fields as non-strings.

**`parseTakerAssetJSON`** translates the validated string representations into typed values. For IOU/XRP paths it calls `to_currency()` to populate an `Issue`; for MPT paths it calls `MPTID::parseHex()`. This function uses a small lambda to select the correct error code contextually — `rpcSRC_CUR_MALFORMED` for `taker_pays`, `rpcDST_AMT_MALFORMED` for `taker_gets` — without duplicating the body.

**`parseTakerIssuerJSON`** then fills in the `Issue::account` (issuer). For XRP it defaults to `xrpAccount()` and rejects any explicit issuer. For IOU it requires an explicit issuer, validates it with `to_issuer()`, rejects the sentinel `noAccount()`, and rejects a non-XRP currency paired with the XRP account. MPT assets skip this function entirely since there is no issuer concept.

This three-function split avoids any duplication between the `taker_pays` and `taker_gets` sides, keeping the logic in `doBookOffers` itself to a clean sequence of six consecutive `if (auto const err = ...)` calls.

## Concurrency and Load Shedding

Before touching the ledger at all, `doBookOffers` checks the `jtCLIENT` job count:

```cpp
if (context.app.getJobQueue().getJobCountGE(jtCLIENT) > 200)
    return rpcError(rpcTOO_BUSY);
```

This is an early-exit load-shedder: if the server is already processing more than 200 concurrent client jobs, the request is rejected immediately with `rpcTOO_BUSY`, avoiding any further work. The comment in the source (`// VFALCO TODO Here is a terrible place for this kind of business logic`) acknowledges this is a legacy placement; conceptually it belongs in the dispatch layer, but it has lived here long enough to be part of the contract.

## Pagination, Domain, and the Taker Identity

Three optional fields feed into the final `getBookPage` call:

- **`limit`**: validated through `readLimitField` against `RPC::Tuning::bookOffers` (default 60, maximum 100). Values outside the range are clamped or rejected.
- **`marker`**: a `Json::Value` passed opaquely to `getBookPage`, enabling cursor-based pagination across multiple calls without server-side state.
- **`domain`**: a hex `uint256` parsed from the `domain` JSON field, stored in `std::optional<uint256>` and forwarded as part of the `Book` struct. This scopes the query to offers within a specific domain, a feature added to support partitioned order books.
- **`taker`**: an optional Base58-encoded `AccountID` representing the perspective from which to evaluate offers. When absent, `beast::zero` is passed as a neutral identity. The taker identity matters because quality calculations in `getBookPage` can account for an existing account's balances and trust lines.

## Delegation to `NetworkOPs::getBookPage`

`doBookOffers` performs no ledger traversal. After building the fully validated `Book` and resolving all optional fields, it delegates entirely to `context.netOps.getBookPage`, passing the `ReadView`, the book spec, taker, pagination metadata, and a `jvResult` output parameter by reference. Results are written into `jvResult` by `getBookPage`, and the handler then stamps the response with `feeMediumBurdenRPC` to reflect the non-trivial read cost of scanning offer directory pages.

The clean separation between the parsing/validation surface here and the iteration logic in `NetworkOPsImp::getBookPage` means that the same traversal engine is also reused by the `Subscribe.cpp` handler for streaming order book subscriptions — both call `getBookPage` with identical signatures.

## Error Taxonomy

The handler produces a precise set of error codes that map semantically to which side of the book was malformed: `rpcSRC_CUR_MALFORMED` and `rpcSRC_ISR_MALFORMED` for problems in `taker_pays`; `rpcDST_AMT_MALFORMED` and `rpcDST_ISR_MALFORMED` for problems in `taker_gets`. The symmetry is maintained through the lambda-based error selector in the two parse functions, keeping the discriminating logic local rather than requiring callers to pass separate error codes.