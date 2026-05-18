# `SubmitMultiSigned.cpp` — RPC Handler for Multi-Signed Transaction Submission

## Role in the System

This file implements `doSubmitMultiSigned`, the entry point for the `submit_multisigned` JSON-RPC command. It sits at the top of the call chain for clients who have already collected multiple signatures on a transaction (using `sign_for`) and now want to broadcast it to the network. Its place in `src/xrpld/rpc/handlers/transaction/` puts it alongside `Submit.cpp`, `Simulate.cpp`, and the other per-command handlers, each of which is a thin bridge between the raw `RPC::JsonContext` and the richer business logic in `RPC::detail`.

## What the Handler Does

The function body is intentionally compact:

```cpp
Json::Value
doSubmitMultiSigned(RPC::JsonContext& context)
{
    context.loadType = Resource::feeHeavyBurdenRPC;
    auto const failHard = context.params[jss::fail_hard].asBool();
    auto const failType = NetworkOPs::doFailHard(failHard);

    return RPC::transactionSubmitMultiSigned(
        context.params,
        context.apiVersion,
        failType,
        context.role,
        context.ledgerMaster.getValidatedLedgerAge(),
        context.app,
        RPC::getProcessTxnFn(context.netOps));
}
```

Three things happen before the real work is dispatched:

**Load classification.** The first statement marks the request as `Resource::feeHeavyBurdenRPC`, which carries a weight of 3000 — the highest tier in the ledger's resource fee schedule. Multi-signed submission is legitimately expensive: the callee must check each signer's account in the open ledger, verify multiple ECDSA or Ed25519 signatures, parse and serialize the full transaction, and enqueue it for network broadcast. Marking load before doing any work ensures the resource manager can apply rate-limiting even if the call ultimately fails early.

**`fail_hard` extraction.** The JSON parameter is pulled via `jss::fail_hard` and converted with `asBool()`. If the field is absent, `Json::Value::asBool()` returns `false` by default, making the flag optional. The boolean is then mapped to `NetworkOPs::FailHard` via `NetworkOPs::doFailHard()`, a conversion that produces an enum value understood by the network operations layer. `FailHard::yes` tells the network to reject the transaction outright if it cannot be applied to the open ledger, while the default (`no`) allows it to be queued. This maps exactly to the `tx_blob`-path behaviour in `doSubmit`.

**Delegation.** All structural validation, cryptographic checking, ledger lookups, and actual submission are handled by `RPC::transactionSubmitMultiSigned` in `src/xrpld/rpc/detail/TransactionSign.cpp`. The `context.params` block is forwarded as-is (the callee receives it by value, so it may modify a local copy). The handler also passes `context.ledgerMaster.getValidatedLedgerAge()`, which the callee uses to detect a stale validated ledger and refuse fee auto-fill — a defensive measure against submitting into an outdated view of the network.

## Design Contrast with `Submit.cpp`

Looking at the sibling `doSubmit` handler makes the separation of concerns clear. `doSubmit` contains substantial inline logic: hex-decoding a raw `tx_blob`, constructing an `STTx`, calling `checkValidity`, managing a `Transaction` object, and catching exceptions from `processTransaction`. Multi-signed submission avoids all that inline complexity because the transaction is provided as `tx_json` (JSON fields, not a pre-serialised blob), so parsing, validation, and signature aggregation are more structured and can be neatly encapsulated in `transactionSubmitMultiSigned`.

## Dependency Injection via `getProcessTxnFn`

Rather than calling `context.netOps.processTransaction(...)` directly, the handler passes a `ProcessTransactionFn` closure produced by `RPC::getProcessTxnFn(context.netOps)`. This is a `std::function` that captures a reference to `NetworkOPs` and forwards the `processTransaction` call. The pattern allows `RPC::transactionSubmitMultiSigned` — which lives in a layer below the RPC context — to invoke network operations without taking a dependency on the full `NetworkOPs` interface or the `JsonContext`. It also makes the business logic in `TransactionSign.cpp` easier to unit-test in isolation by injecting a mock function.

## Validation Architecture

Input validation is split across two layers. The handler itself performs only the minimum type coercion (`asBool()` for `fail_hard`). All deeper validation — presence and type of `SigningAccounts`, structure of `tx_json`, required transaction fields, fee adequacy, source account existence, and multi-signature correctness — is delegated to `transactionSubmitMultiSigned`. This delegation is intentional: those checks require access to the open ledger, the fee tracker, and the transaction queue, which are all passed through the handler's arguments rather than accessed inline.