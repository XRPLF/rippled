# `RPCLedgerHelpers.cpp` — Ledger Resolution for RPC Handlers

This file implements the authoritative ledger-resolution layer for both the JSON-RPC and gRPC surfaces of `rippled`. Every RPC handler that needs to operate against a specific ledger — `account_info`, `account_lines`, `ledger_data`, `ledger_entry`, and dozens more — delegates the parsing, validation, and lookup of that ledger to the functions defined here. The file's primary job is translating the many ways a caller can express "give me ledger X" (a hash, a sequence number, a named shortcut, or nothing at all) into a concrete, validated `std::shared_ptr<ReadView const>` or `std::shared_ptr<Ledger const>`.

## Input Validation Architecture

The JSON entry point is the anonymous-namespace `ledgerFromRequest(T& ledger, JsonContext const& context)`. It enforces that at most one of three fields is present in the request: `ledger` (legacy, deprecated), `ledger_hash`, or `ledger_index`. If more than one is supplied, it returns `rpcINVALID_PARAMS` immediately. The error message deliberately omits mentioning the deprecated `ledger` field when both `ledger_hash` and `ledger_index` are present, nudging callers toward the non-deprecated API.

The legacy `ledger` field uses a heuristic to decide whether its string value is a hash or an index: a string of exactly 64 characters is treated as a hash. This is internally consistent but means a 64-digit decimal number would be misinterpreted — a reasonable trade-off for an explicitly deprecated code path.

`ledgerFromHash()` parses the value as a hex `uint256`, returning `rpcINVALID_PARAMS` on any parse failure. `ledgerFromIndex()` accepts the canonical shortcuts `"current"`, `"validated"`, `"closed"` (with empty string treated as `"current"`), or a decimal integer parsed via `beast::lexicalCastChecked`. Any other string is rejected.

## The `getLedger()` Overloads and Staleness Policy

The three `getLedger()` overloads are the actual points of contact with `LedgerMaster`. Their staleness behavior is deliberately asymmetric:

**By hash**: no staleness check at all. The caller specified an immutable object by its identity; there is no concept of "too old" for a specific historical ledger. It either exists in cache or it doesn't.

**By sequence index**: looks up the ledger, and if found, checks whether its sequence exceeds the last validated index *while* the node's validation is stale. This prevents an out-of-sync node from serving a ledger it thinks is current but that the network has since superseded. The function also tries the current open ledger as a fallback when `getLedgerBySeq()` returns null, handling the common case where the caller asks for the ledger that's still being built.

**By `LedgerShortcut`**: checks staleness *before* attempting lookup. For `Validated`, this means a stale node returns an error rather than returning a potentially wrong "validated" ledger. For `Current` and `Closed`, an additional sequence-gap check applies: if the shortcut ledger's sequence is more than 10 behind the last validated index, the node considers itself unsynced and returns an error. This `minSequenceGap` of 10 is a hard-coded heuristic protecting against returning data from a node that is clearly behind the network.

The staleness check itself is `isValidatedOld()`, which compares `LedgerMaster::getValidatedLedgerAge()` against `Tuning::maxValidatedLedgerAge` (2 minutes). Crucially, in standalone mode (development/test networks with no peers) this check always returns `false`, so local-only nodes are never blocked by network-dependent staleness logic.

The API version distinction (`context.apiVersion == 1`) maps old error codes to new ones: `rpcNO_NETWORK` / `"InsufficientNetworkMode"` for v1, `rpcNOT_SYNCED` / `"notSynced"` for v2 and later.

## Template Design and Explicit Instantiations

All the `getLedger()`, `ledgerFromRequest()`, and `ledgerFromSpecifier()` functions are function templates parameterized on the ledger type `T`. In practice they are only ever instantiated for `std::shared_ptr<ReadView const>`, but the template form avoids coupling the implementation to a specific pointer type. Because the implementations live in `.cpp`, the file closes with explicit instantiations for `std::shared_ptr<ReadView const>` (for the three `getLedger()` overloads) and for each of the three gRPC request types that need `ledgerFromRequest<>`.

## gRPC Protocol Support

The gRPC path enters through `ledgerFromRequest(T& ledger, GRPCContext<R> const& context)`, which extracts the `LedgerSpecifier` protobuf field from the request and delegates to `ledgerFromSpecifier()`. That function switches on the protobuf `LedgerCase` enum — `kHash`, `kSequence`, `kShortcut`, or unset — mapping each to the same underlying `getLedger()` overloads used by the JSON path. The three explicitly instantiated gRPC request types (`GetLedgerEntryRequest`, `GetLedgerDataRequest`, `GetLedgerRequest`) all follow this exact path.

## `lookupLedger()` — The Common Handler Entry Point

The two `lookupLedger()` overloads are the primary interface for JSON-RPC handlers. After resolving the ledger via `ledgerFromRequest()`, they populate the JSON response with the appropriate identifying fields: closed ledgers get `ledger_hash` and `ledger_index`, while the open (current) ledger gets only `ledger_current_index`. The `validated` boolean is added by calling `LedgerMaster::isValidated()` against the resolved ledger. The two-overload design separates the "fill an existing result object" pattern from the "create and return a result" pattern, giving callers flexibility in how they compose their response JSON.

## `getOrAcquireLedger()` — Network-Triggered Acquisition

This function is semantically distinct from the others: it can trigger active fetching of a missing ledger from the network via `InboundLedgers::acquire()`. It is used exclusively by the `ledger_request` admin command. Unlike the other helpers, it mandates exactly one of `ledger_hash` or `ledger_index` — the deprecated `ledger` field is not supported — and returns `Expected<std::shared_ptr<Ledger const>, Json::Value>` rather than a `Status`, encoding both success value and error in a single return.

The sequence-index path has an interesting two-step lookup: it first tries to find the target ledger's hash directly from the validated ledger's skip list via `hashOfSeq()`. If the skip list doesn't reach that far back (typical for very old ledgers), it computes a "candidate" reference ledger at an aligned boundary via `getCandidateLedger()`, acquires *that* ledger first, then uses its skip list to locate the actual target's hash. If neither step yields a result, `acquire()` schedules a network fetch and the function returns a JSON object describing the in-progress acquisition — a polling model rather than a blocking one. In standalone mode, the network acquisition path is bypassed and the function falls back to `getLedgerByHash()`, since there are no peers to fetch from.