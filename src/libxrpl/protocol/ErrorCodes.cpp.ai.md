# `ErrorCodes.cpp` — RPC Error Code Registry and JSON Serialization

## Role in the System

This file is the single source of truth for every named error condition that the XRPL RPC layer can return to a client. It provides three things: a compile-time validated lookup table mapping `error_code_i` integers to human-readable metadata, a small set of JSON-serialization utilities that stamp that metadata onto response objects, and an HTTP-status mapping used by the transport layer to signal retryability to load balancers.

## The `ErrorInfo` Lookup Table

The central data structure is the `detail::sortedErrorInfos` array, a `constexpr std::array<ErrorInfo, rpcLAST>` that is built at compile time from the unordered initializer list `unorderedErrorInfos`. Each `ErrorInfo` entry holds an `error_code_i` code, a short camelCase `token` (the machine-readable name sent to API clients), a human-readable `message`, and an `http_status` integer that defaults to `200` when not explicitly specified.

The deliberate separation of the authoring list from the storage array is the key architectural insight here. The developer-maintained `unorderedErrorInfos` list can be written in any order, grouped thematically, without worrying about the numeric gaps in `error_code_i`. The `sortErrorInfos<rpcLAST>()` template function then re-indexes every entry by `code - 1` into the correctly-sized output array. This turns what would otherwise be a maintenance burden — keeping a parallel list in strict numeric order — into a compiler responsibility.

## Compile-Time Validation via `sortErrorInfos`

`sortErrorInfos` is more than a sort; it is a full integrity check executed at compile time (the comment acknowledges it should become `consteval` in C++20). It enforces three invariants:

1. **Range check** — every code must satisfy `rpcSUCCESS < code <= rpcLAST`. Codes outside that window throw `std::out_of_range`.
2. **Uniqueness** — if a slot in the output array is already occupied when a second entry tries to claim it, `std::invalid_argument` is thrown for a duplicate.
3. **Contiguity** — after placement, the function walks the array and checks that every non-`rpcUNKNOWN` entry exactly matches its expected index position. This rejects the subtle case where an entry was silently placed at the wrong index because of an integer typo. It also confirms the total count of recognized entries matches `N`, the size of the input array.

The result is that a developer adding a new error code who accidentally reuses an integer, uses a value outside the declared range, or forgets to add the new code to `unorderedErrorInfos` will see a compile-time failure rather than a silent runtime bug.

## O(1) Lookup

`get_error_info()` exploits the layout guarantee established by `sortErrorInfos`: a code at value `k` lives at index `k - 1`. The implementation is therefore a direct array subscript, not a search. Out-of-range codes return a reference to `detail::unknownError`, the default-constructed `ErrorInfo` whose code is `rpcUNKNOWN` and whose HTTP status is 200.

## HTTP Status Philosophy

The file's own comment explains the design tension honestly. Every RPC error originally returned HTTP 200, which is semantically correct for a well-formed JSON-RPC transport — the HTTP transaction succeeded even though the application-level call failed. The current assignment of 4xx/5xx codes is deliberately narrow and targets a specific use case: load-balancer failover.

When a request fails on one node but *might* succeed on another — for example because the node is amendment-blocked (`503`), not yet synced (`503`), or internally overwhelmed (`503 tooBusy` / `429 slowDown`) — a non-200 status causes upstream load balancers to retry on a healthy peer. Errors that reflect permanent or client-side conditions (`400 badSyntax`, `403 badSecret`, `404 lgrNotFound`) are not retryable and correctly signal that retrying elsewhere would be futile. `rpcLGR_NOT_VALIDATED` is `202` (Accepted but not finalized) — a nuanced choice indicating the ledger is real but consensus is still pending. Errors without an explicit `http_status_` constructor argument keep 200, preserving the original behavior for anything not yet categorized for failover purposes.

## Public API

`inject_error()` mutates an existing `Json::Value` by writing `error`, `error_code`, and `error_message` fields onto it. The overload accepting a `std::string message` replaces the default message with the caller-supplied one, enabling context-specific diagnostics while preserving the stable token and code. `make_error()` wraps `inject_error()` in a factory that constructs and returns a fresh `Json::Value`.

`contains_error()` provides a lightweight probe — it checks only for the presence of an `"error"` member on a JSON object — useful for callers that receive a `Json::Value` and need to branch on success vs. failure without inspecting the specific code.

`error_code_http_status()` extracts just the status integer, used by the HTTP transport layer when constructing the HTTP response header.

`rpcErrorString()`, living one namespace level up in `xrpl` rather than `xrpl::RPC`, concatenates the `error` token and `error_message` fields of a JSON error value into a single string. It is accompanied by an `XRPL_ASSERT` that the input already contains an error, making misuse diagnosable in debug builds.

## Stability Contract

The `error_code_i` enum in the companion header carries an explicit warning: although the numeric values were never intended to be stable, they were inadvertently exposed through API responses, and some clients depend on them. The enum is therefore treated as append-only — gaps must not be filled, retired codes must not be reused, and the comment marks several such reserved slots explicitly. The `sortErrorInfos` validation enforces this implicitly: a reassigned integer would either collide with an existing entry or fail the contiguity check.