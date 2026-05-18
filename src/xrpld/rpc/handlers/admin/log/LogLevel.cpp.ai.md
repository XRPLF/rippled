# `LogLevel.cpp` — `log_level` Admin RPC Handler

## Role in the System

This file implements `doLogLevel`, the sole handler for the `log_level` admin RPC command in the XRPL node. It is registered in the central dispatch table (`Handler.cpp`) as an `ADMIN`-role command, meaning it is only available to operators with privileged API access. Its purpose is runtime log-level control: operators can query the current severity thresholds across all logging partitions, or change them globally or per subsystem without restarting the node.

The sibling file `LogRotate.cpp` in the same directory handles the companion `logrotate` command; together they form the complete set of runtime log management primitives.

## How the Handler Works

`doLogLevel` implements a single three-mode dispatch off the shape of its JSON input:

**Query mode** (no `severity` field): Returns a JSON object keyed as `levels` containing the base threshold and all per-partition thresholds. It calls `context.app.getLogs().threshold()` to retrieve the global severity, then `partition_severities()` to enumerate every named subsystem (e.g., `Ledger`, `Consensus`, `Network`) alongside its current threshold as a human-readable string.

**Global set mode** (`severity` only, no `partition`): Parses the severity string and calls `context.app.getLogs().threshold(severity)` to update the global base threshold. All partitions that haven't been individually overridden will be filtered at this level.

**Partition set mode** (`severity` + `partition`): Sets the threshold on a specific named partition via `context.app.getLogs().get(partition).threshold(severity)`. The name `"base"` is treated as a special alias for the global threshold — detected via `boost::iequals` so that `"Base"`, `"BASE"`, or any mixed-case variant is handled identically.

## Severity Type Pipeline

The severity value traverses a notable two-step conversion chain. The raw JSON string is first converted to the deprecated `LogSeverity` enum via `Logs::fromString()`, which returns `lsINVALID` for any unrecognised string. This sentinel is checked immediately; an unrecognised severity string produces `rpcINVALID_PARAMS` rather than silently clamping or defaulting. The valid enum value is then converted to `beast::severities::Severity` via `Logs::toSeverity()` before being applied. This indirection exists because the codebase originally used the `LogSeverity` enum throughout but migrated to `beast::Journal`'s severity type; `fromString`/`toSeverity` are the translation boundary between the legacy interface and the current one, as `Log.h` explicitly marks `LogSeverity` as deprecated.

## The `Logs` Partitioning Model

The `Logs` class (declared in `xrpl/basics/Log.h`) maintains a `std::map` of named `Sink` objects, each wrapping a `beast::Journal::Sink`. Every subsystem acquires a named journal at startup (e.g., `app.getLogs().journal("Consensus")`), and each sink inherits the global threshold unless individually overridden. `partition_severities()` iterates that map to produce the name→severity-string pairs returned in query mode. The map key comparison uses `boost::beast::iless`, meaning partition names are themselves case-insensitive at the storage level — consistent with the `boost::iequals` check in the handler.

## Structural Oddity

There is a subtle unreachable code path at the bottom of the function. The handler checks `!context.params.isMember(jss::partition)` at line 41; if that passes, it sets the base threshold and returns. The immediately following `if (context.params.isMember(jss::partition))` at line 49 is therefore only reached when `partition` *is* present, making the condition trivially true every time control reaches it. The final `return rpcError(rpcINVALID_PARAMS)` at line 66 is unreachable. This is a structural artifact of incremental growth — the code reads as if a future branch was anticipated — but it introduces no functional defect.

## Validation Summary

Input validation is intentionally minimal and follows the handler idiom common across `xrpld`. Presence of `jss::severity` is used as the branch discriminator rather than a hard requirement — its absence is the legitimate query path. Once present, the severity string is validated by `Logs::fromString()` returning `lsINVALID`, which is the only error return in the entire function. Unknown partition names are silently accepted: `Logs::get()` creates a new sink on demand if none exists by that name, so mistyped partition names result in a newly created partition threshold rather than an error — operators should be careful with spelling.