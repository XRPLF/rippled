# `Fee.cpp` — `fee` RPC Handler

## Role in the System

`Fee.cpp` implements `doFee`, the server-side handler for the XRPL `fee` RPC command. This command exposes the node's current transaction fee environment to clients — covering both the raw drop amounts and the normalized fee-level representation used internally by the transaction queue. It sits in the `server_info` handler directory alongside other node-introspection endpoints such as `ServerInfo.cpp` and `ServerState.cpp`.

The handler is registered in `Handler.cpp` as:

```cpp
{"fee", byRef(&doFee), Role::USER, NEEDS_CURRENT_LEDGER}
```

Two things stand out here. First, the `Role::USER` designation means any connected client can call this endpoint without admin credentials — fee visibility is intentionally public. Second, the `NEEDS_CURRENT_LEDGER` condition means the framework gates the call: if no current open ledger exists, the handler is never reached and the framework itself returns an error. This pre-condition makes the null-view check inside `TxQ::doRPC` a belt-and-suspenders guard rather than a genuine runtime branch.

## Design: Pure Delegation

The body of `doFee` is deliberately minimal:

```cpp
auto result = context.app.getTxQ().doRPC(context.app);
if (result.type() == Json::objectValue)
    return result;
```

All actual data collection and JSON construction is owned by `TxQ::doRPC`. That method queries the current open ledger view to compute `TxQ::Metrics`, then assembles a JSON object containing two sub-objects: `levels` (fee levels in normalized fee-level units) and `drops` (absolute amounts in XRP drops). The fields it populates include `ledger_current_index`, `current_queue_size`, `max_queue_size`, `expected_ledger_size`, the four `levels` entries (`reference_level`, `minimum_level`, `median_level`, `open_ledger_level`), and four `drops` entries (`base_fee`, `median_fee`, `minimum_fee`, `open_ledger_fee`).

This separation is a consistent pattern across the `server_info` directory: the RPC layer provides the dispatch point and structural contract (returns `Json::Value`, handles errors via `inject_error`), while the application layer owns the logic. `doFee` is the most extreme case — it contributes nothing but the `objectValue` type check.

## The Type Check and the Unreachable Path

The `result.type() == Json::objectValue` guard protects against `TxQ::doRPC` returning something other than a JSON object. Looking at `TxQ::doRPC`, the only way it returns a non-object is if `app.getOpenLedger().current()` returns null, in which case it returns a default-constructed `Json::Value` (type `nullValue`) after triggering a `BOOST_ASSERT`. That path exists because `TxQ::doRPC` may theoretically be called from contexts beyond just this handler.

In practice, for `doFee` specifically, this cannot happen: the `NEEDS_CURRENT_LEDGER` condition in Handler.cpp ensures the open ledger is available before `doFee` is invoked. Accordingly, the failure branch is annotated `LCOV_EXCL_START / LCOV_EXCL_STOP` — explicitly excluded from coverage reporting — and the `UNREACHABLE` macro marks it as a programming-error trap rather than a recoverable condition. If somehow reached, `inject_error(rpcINTERNAL, context.params)` mutates the incoming `context.params` and returns it as an error response.

This pattern — validate the contract even when the contract is already guaranteed by the dispatch layer — reflects XRPL's defensive philosophy: each layer checks its own invariants rather than relying entirely on callers to uphold them.