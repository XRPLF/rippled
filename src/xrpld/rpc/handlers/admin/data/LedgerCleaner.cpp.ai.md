# `rpc/handlers/admin/data/LedgerCleaner.cpp`

This file is the RPC entry point for the `ledger_cleaner` admin command. It contains a single four-line function, `doLedgerCleaner`, that bridges the JSON-RPC layer to the background `LedgerCleaner` subsystem responsible for auditing and repairing ledger and transaction database continuity on a running node.

## Role in the RPC Dispatch Layer

The handler is registered in `src/xrpld/rpc/detail/Handler.cpp` as:

```cpp
{"ledger_cleaner", byRef(&doLedgerCleaner), Role::ADMIN, NEEDS_NETWORK_CONNECTION}
```

The `Role::ADMIN` constraint means the command is only reachable from a privileged (local or explicitly-credentialed) connection — it cannot be triggered by an arbitrary network peer. `NEEDS_NETWORK_CONNECTION` gates it further, requiring the node to be in an active network state before the cleaner can be started.

## What `doLedgerCleaner` Does

```cpp
Json::Value
doLedgerCleaner(RPC::JsonContext& context)
{
    context.app.getLedgerCleaner().clean(context.params);
    return RPC::makeObjectValue("Cleaner configured");
}
```

The entire function body is two statements. It forwards the raw `context.params` JSON directly to `LedgerCleaner::clean()` without transformation or pre-validation, and immediately returns a static confirmation string. This is a deliberate fire-and-forget design: `clean()` is documented to schedule work asynchronously on an implementation-defined internal thread and return without blocking.

## Intentional Delegation of Validation

Because `LedgerCleaner` is an abstract class (`clean()` is pure virtual), all parameter interpretation — including which ledger sequence range to clean, whether to fix transaction entries, etc. — lives in the concrete implementation rather than here. The handler has no knowledge of what parameters are meaningful; it just provides a channel. This keeps the handler trivially auditable and concentrates the business logic in one place.

## Contrast with Sibling Handlers

The adjacent `CanDelete.cpp` handler takes the opposite approach: it does all validation inline (parsing ledger IDs, hash lookups, keyword values like `"now"` / `"always"` / `"never"`) and is synchronous. `LedgerRequest.cpp` is similarly synchronous and returns structured ledger data. `doLedgerCleaner` is unique in this group for being purely write-through and asynchronous — the caller gets no status about what the cleaner did, only a confirmation that it was configured. Any runtime progress would need to be observed through logs or the `PropertyStream` interface that `LedgerCleaner` inherits from `beast::PropertyStream::Source`.