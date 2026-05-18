# `FetchInfo.cpp` — `fetch_info` Admin RPC Handler

This file implements `doFetchInfo`, the server-side handler for the `fetch_info` admin RPC command. Its role is to expose the internal state of the inbound ledger fetch subsystem to node operators and, optionally, to clear recorded fetch failures on demand.

## Context in the Admin RPC Layer

`doFetchInfo` lives in `src/xrpld/rpc/handlers/admin/status/` alongside a set of single-purpose diagnostic handlers — `doConsensusInfo`, `doValidatorInfo`, `doGetCounts`, `doPrint`, and others. All follow the same structural pattern: receive an `RPC::JsonContext`, delegate to a method on `context.netOps` (the `NetworkOPs` abstraction), and return a `Json::Value`. The handler is registered in `Handler.cpp` under the command name `"fetch_info"` with `Role::ADMIN`, meaning it is only accessible to callers presenting admin credentials.

## What the Handler Does

The function performs two operations, one conditional:

1. **Optional state reset**: If the incoming JSON parameters include a `"clear"` field that evaluates to `true`, `context.netOps.clearLedgerFetch()` is called. This delegates immediately to `InboundLedgers::clearFailures()`, which wipes the set of ledger hashes that have been marked as failed fetches. This is useful when an operator wants to retry fetching ledgers that were previously abandoned — for example, after a transient network partition healed. The response echoes `"clear": true` to confirm the action was taken.

2. **Status snapshot**: Regardless of the `clear` flag, the handler calls `context.netOps.getLedgerFetchInfo()`, which routes to `InboundLedgers::getInfo()`. The returned `Json::Value` snapshot is placed under the top-level `"info"` key in the response.

## Design Notes

The `clear` parameter is handled with a deliberate double-guard: `isMember(jss::clear)` confirms the field exists before `asBool()` reads it. This prevents a type-coercion exception if the field is absent, and `asBool()` handles non-boolean values gracefully via `Json::Value`'s internal coercion rules. No explicit error is thrown for unexpected types — the framework's JSON coercion is treated as sufficient protection, consistent with how sibling handlers in this directory deal with optional parameters.

The clear-then-read ordering is intentional: failures are cleared first so that the immediately returned `"info"` snapshot reflects the post-clear state. This makes a single `fetch_info {"clear": true}` call both an action and a confirmation, eliminating the need for a separate read round-trip.

Because the handler contains no business logic beyond delegation and optional state reset, the `NetworkOPs` abstraction is the real complexity boundary. The thin adapter design keeps RPC handlers auditable and ensures the actual fetch tracking state lives exclusively in the `InboundLedgers` subsystem.