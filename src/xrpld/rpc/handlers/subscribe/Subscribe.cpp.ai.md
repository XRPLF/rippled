# Subscribe.cpp — `doSubscribe` RPC Handler

## Role in the System

`Subscribe.cpp` implements `doSubscribe`, the server-side handler for the XRPL `subscribe` RPC/WebSocket command. Its job is to register a client connection as a listener on one or more named event streams, sets of accounts, or order books. Once registered, the event routing infrastructure in `NetworkOPs` will push JSON notifications to the client whenever matching events occur on the ledger without requiring the client to poll. The symmetric counterpart, `doUnsubscribe` in `Unsubscribe.cpp`, tears down these registrations using exactly parallel logic.

## Two Delivery Modes: WebSocket vs. HTTP Callback

The handler supports fundamentally different delivery mechanisms that share the same parameter surface. When a client has an active WebSocket connection, `context.infoSub` is already populated with the connection's `InfoSub` object and that pointer is used directly. When a `url` parameter is present instead, the handler creates (or retrieves) an `RPCSub` object that POSTs events to the specified HTTP endpoint rather than streaming them over a socket. The URL mode is exclusively available to admin-role clients, enforced by the `Role::ADMIN` check at line 32. The `VFALCO TODO Remove` comment in `InfoSub::Source` makes it clear this HTTP-push path was a one-off addition for a specific partner and is considered legacy.

For URL subscriptions, `findRpcSub` checks a server-wide registry for an existing subscription to that URL, avoiding duplicate `RPCSub` objects. If none exists, `make_RPCSub` is called to construct one, with any `std::runtime_error` thrown during construction (e.g., a malformed URL) converted to an `rpcError` parameter error. When reusing an existing subscription, only the deprecated `username`/`password` fields update the stored credentials — an in-code comment at line 79 acknowledges this asymmetric treatment of `url_username`/`url_password` as a known oddity.

## `InfoSub`: The Subscriber Abstraction

`InfoSub` is the common base for all subscriber types. It holds a `Consumer` for resource accounting, a monotonically increasing 64-bit `mSeq` identifier used as a key for unsubscription, per-subscriber sets of tracked accounts, and the API version of the connected client. After `ispSub` is resolved, `setApiVersion()` is called immediately so every subsequent subscription registration inherits the correct formatting version — ensuring events serialized for this subscriber match what the client's API version expects.

## Named Stream Subscriptions

The `streams` array drives registration for seven public streams (`server`, `ledger`, `book_changes`, `manifests`, `transactions`, `transactions_proposed`, `validations`, `consensus`) and one admin-only stream (`peer_status`). Each name maps to a single `netOps.sub*` call that adds `ispSub` to the appropriate fan-out set inside `NetworkOPs`. The stream name `rt_transactions` is accepted as a deprecated alias for `transactions_proposed`; the distinction between these two is significant — `transactions` fires only for ledger-validated transactions while `transactions_proposed` fires for unconfirmed candidates entering the queue.

## Account Subscriptions

Account subscriptions come in two flavors, controlled by a `bool realTime` flag threaded through `netOps.subAccount`. The `accounts` field subscribes to finalized transaction outcomes, while `accounts_proposed` (and its deprecated alias `rt_accounts`) subscribes to proposed transactions before validation. Both paths call `RPC::parseAccountIds` to decode the base58-encoded address array, returning `rpcACT_MALFORMED` immediately if any parse fails, so the server never registers a partial or ambiguous set.

## Order Book Subscriptions and the Snapshot Pattern

Book subscriptions expose the most complex logic. Each entry in the `books` array must contain `taker_pays` and `taker_gets` currency/issuer objects, parsed via `RPC::parseSubUnsubJson`. The handler validates that both sides are not identical (a self-trade market makes no sense) and calls `isConsistent(book)` to enforce protocol-level coherence of the currency/issuer pair. The optional `domain` field allows filtering the book to a specific AMM domain (a uint256 hex-encoded identifier).

The `both`/`both_sides` flag (where `both_sides` is deprecated) subscribes to both the forward book and `reversed(book)`, allowing a client to monitor a trading pair regardless of which side is designated as bid or ask.

The most interesting design choice here is the **subscribe-then-snapshot** pattern triggered by the `snapshot`/`state_now` flags. The handler registers the live subscription first via `netOps.subBook`, then fetches current offers from the published ledger using `getBookPage`. The result populates the response under `offers` (single direction) or `bids`/`asks` (both directions). This ordering eliminates a race condition: any offer event that fires between the time the snapshot is read and the time the subscription is active would be silently dropped if the registration happened after the snapshot. Because registration precedes the read, the client may see duplicate events for offers that were already in the snapshot, but it cannot miss any. The snapshot path sets `context.loadType = Resource::feeMediumBurdenRPC` to signal elevated resource consumption to the rate-limiting layer.

## `account_history_tx_stream`

This experimental feature, gated behind both `useTxTables()` and an explicit warning in the response JSON, streams both current and historical transactions for a single account. The server replays past ledger history while simultaneously forwarding new transactions going forward. It requires transaction table storage to be enabled on the node — if not, `rpcNOT_ENABLED` is returned. Like the book snapshot path, it charges `feeMediumBurdenRPC` given the potential for significant historical replay work. `doUnsubscribe` adds a `stop_history_tx_only` option that lets a client halt the historical replay while continuing to receive live transactions, reflecting the natural lifecycle of this feature.

## Error Handling Discipline

Every validation step returns immediately on failure without partial side effects. Array inputs are type-checked before iteration; each stream name is checked for exact string match before any `netOps` call; book parameters are fully parsed and validated before `subBook` is invoked. The `make_RPCSub` factory is wrapped in a try/catch specifically for `std::runtime_error` so a bad URL string surfaces as a structured RPC error rather than an unhandled exception. The result is that either all requested subscriptions are registered (and any snapshot data is returned) or the handler returns an error and the client knows no state was changed.