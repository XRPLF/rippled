# `LedgerHeader.cpp` — `doLedgerHeader` RPC Handler

This file implements `doLedgerHeader`, the handler for the `ledger_header` JSON-RPC command. The command's specific purpose — distinct from the broader `ledger` command — is to expose the raw binary encoding of a ledger's header alongside its JSON representation, giving clients everything they need to verify the ledger's cryptographic integrity independently of the server.

## What the Handler Does

`doLedgerHeader` follows a tight three-step pattern. First, `RPC::lookupLedger` resolves the requested ledger from `context.params`, accepting either a `ledger_hash` (256-bit hex) or a `ledger_index` (integer or a string shortcut such as `"current"`, `"closed"`, or `"validated"`). If the ledger cannot be found or the parameters are malformed, `lookupLedger` populates `jvResult` with an error code (`rpcLGR_NOT_FOUND`, `rpcINVALID_PARAMS`, etc.) and returns a null `shared_ptr`, causing `doLedgerHeader` to return the error immediately. No input validation lives in this file itself — it is fully delegated to the RPC framework and `lookupLedger`.

Second, it serializes the ledger header into canonical binary form. `addRaw(lpLedger->header(), s)` invokes the two-argument overload of `addRaw` declared in `include/xrpl/protocol/LedgerHeader.h`, which defaults `includeHash` to `false`. The serialization encodes the header's fields in a fixed wire layout: sequence number, total XRP drops, parent hash, transaction tree hash, account-state tree hash, parent close time, close time, close-time resolution, and close flags. Because the hash itself is excluded, the resulting bytes are precisely the pre-image that, when prefixed with `HashPrefix::ledgerMaster` and hashed with SHA-512 half, should reproduce the ledger's hash. A client who receives `ledger_data` and the accompanying `ledger_hash` can therefore verify the server's claim without trusting the server. This is the non-obvious design motivation for returning the binary at all: it enables trustless verification of the header fields.

Third, `addJson(jvResult, {*lpLedger, &context, 0})` appends the human-readable JSON representation of those same header fields. The `LedgerFill` struct is constructed with `options = 0`, which means no transaction dump, no state dump, and no full expansion — only the header-level fields are populated. Passing `&context` into `LedgerFill` also causes its constructor to call `context.ledgerMaster.getCloseTimeBySeq(ledger.seq())`, fetching the actual network close time from the ledger master's internal index and storing it as `closeTime` on the fill object so `addJson` can include it.

## The Trust Caveat

The comment just before the `addJson` call is architecturally important:

> This information isn't verified: they should only use it if they trust us.

The raw `ledger_data` blob is self-verifying — the client can recompute the hash. The JSON fields derived from `addJson` are not: they are the server's interpretation of the ledger contents and cannot be independently re-verified from the response alone. This distinction explains the dual-output design rather than returning only JSON. The binary form is the trust anchor; the JSON form is a convenience layer.

## Relationship to Sibling Handlers

Within the `handlers/ledger/` module, `LedgerHeader.cpp` occupies a narrow, specialized niche. `LedgerClosed.cpp` returns only the sequence number and hash of the most recently closed ledger — no binary, no full JSON. `LedgerData.cpp` returns ledger state objects in bulk. `Ledger.cpp` (the main `ledger` command) is the broader handler that optionally includes transactions and state data via flags. `LedgerHeader.cpp` is the one handler where a client explicitly wants the binary-serialized header, which makes it most relevant to use cases involving proof of ledger integrity, archive verification, or inter-server cross-checking.

## Resource and Memory Safety

The ledger is held through a `std::shared_ptr<ReadView const>`, keeping the ledger object alive for the duration of the call and guaranteeing cleanup at function return without any manual memory management. `Serializer s` is stack-allocated; `s.peekData()` returns a const reference to its internal buffer, which is valid until `s` goes out of scope — `strHex` converts it to a `std::string` before that happens, so there is no lifetime hazard. The overall function is short-lived and synchronous, with no concurrency concerns internal to its implementation.