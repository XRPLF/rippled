# `src/xrpld/rpc/handlers/ledger/Ledger.cpp`

This file is the implementation hub for the XRPL `ledger` RPC command, serving two distinct transport layers from the same source: the JSON-RPC path via `LedgerHandler` and the gRPC path via `doLedgerGrpc`. Its central job is to locate a ledger, apply the caller's requested output options, and serialize the result in the appropriate wire format.

## `LedgerHandler` — JSON-RPC path

`LedgerHandler` follows the two-phase handler pattern used throughout the XRPL RPC framework: `check()` runs first to validate, resolve, and prepare state; `writeResult()` runs second to emit the response. This split ensures that authorization and resource checks occur before any serialization work begins.

### `check()`

The method starts with a deliberate early-out: if none of `ledger`, `ledger_hash`, or `ledger_index` are present in the request, it returns immediately with `Status::OK` without setting `ledger_`. This is intentional — it signals `writeResult()` to fall back to the dual-ledger response mode (described below).

When a ledger identifier is present, `lookupLedger` resolves and populates `ledger_` and seeds `result_` with preliminary ledger metadata. All flag parameters (`full`, `transactions`, `accounts`, `expand`, `binary`, `owner_funds`, `queue`) are then OR'd together into `options_` as a `LedgerFill::Options` bitmask, decoupling the parameter-parsing phase from the later serialization call.

Two permission gates protect expensive operations. Requesting `full` or `accounts` (which dumps the entire account state trie) is restricted to admin/unlimited roles via `isUnlimited`. Even for admin users, if the local server is under high load (`getFeeTrack().isLoadedLocal()`), the request is rejected with `rpcTOO_BUSY`. The comment in the source acknowledges this is a practical throttle: *"Until some sane way to get full ledgers has been implemented, disallow retrieving all state nodes."* Binary mode incurs `feeMediumBurdenRPC` while JSON mode incurs `feeHeavyBurdenRPC`, reflecting the real CPU cost difference.

The `queue` flag has a separate structural constraint: the transaction queue only exists for the open ledger. Requesting queue state against a validated or historical ledger is semantically meaningless, so `check()` enforces `ledger_->open()` and returns `rpcINVALID_PARAMS` otherwise.

### `writeResult()`

If `ledger_` is set, `writeResult()` calls `copyFrom` to merge any metadata already accumulated in `result_` (populated by `lookupLedger`) and then invokes `addJson` with the resolved ledger, context, options bitmask, and queue transactions. The `LedgerFill` struct passed to `addJson` bundles all of these together and also fetches the ledger's close time from `LedgerMaster`.

If `ledger_` is *not* set (the no-identifier case), the method emits *both* the current closed and the current open ledger as sibling fields. This gives callers a consistent snapshot of the validator frontier without requiring them to know sequence numbers.

Before returning, `writeResult()` checks for the deprecated `type` field and, if present, appends a `warnings` array entry with `warnRPC_FIELDS_DEPRECATED`. This follows the XRPL API convention of using structured warnings rather than errors for backward-compatibility notifications.

## `doLedgerGrpc` — gRPC path

The gRPC handler is a standalone free function targeting a protobuf `GetLedgerRequest`. It shares the ledger-lookup plumbing via `RPC::ledgerFromRequest` but diverges significantly in what it can return, as it is designed for efficient state synchronization by external indexing clients (primarily Clio).

**Header serialization** is unconditional: the ledger header is serialized with `addRaw` and emitted as a raw binary blob, giving clients the canonical header bytes for hash verification.

**Transaction output** is straightforward but guarded by a try-catch around the iteration of `ledger->txs`. If any transaction fails to deserialize mid-iteration, the handler logs an error and breaks — the caller receives whatever transactions were processed. This partial-failure tolerance is intentional: it is preferable to return a usable partial response and log the anomaly than to abort the entire RPC for a single corrupt entry.

**State diff via `SHAMap::Delta`** is the most architecturally significant feature. When `get_objects` is set, the handler loads the parent ledger (sequence − 1) from `LedgerMaster`, then calls `base->stateMap().compare(desired->stateMap(), differences, maxDifferences)`. Both ledgers must be `dynamic_pointer_cast`-able to `Ledger const` — plain `ReadView` is insufficient — because `stateMap()` is only exposed on the concrete `Ledger` class. The diff produces a map of keys to before/after blob pairs, from which CREATED, MODIFIED, and DELETED entries are classified. This mechanism is far more bandwidth-efficient than dumping entire ledger state for each sequence, and it is the primary reason gRPC was introduced alongside JSON-RPC.

**DEX book successor tracking** is the most intricate portion. When `get_object_neighbors` is enabled, for any `ltDIR_NODE` entry that lacks an `sfOwner` field (indicating it is an order book quality directory node rather than an account owner directory), the handler checks if it represents the *first* quality tier for a given offer book. If a quality node was just created, the handler records the book base and the node's key as the new best offer pointer. If deleted, it finds the new first remaining node in `desired->stateMap()`. This data allows Clio to track DEX best-offer positions across ledger transitions without a full trie scan, using `keylet::quality` and `getQualityNext` to bound the search range.

**Performance instrumentation** wraps the entire function body: wall-clock duration in milliseconds is logged at `warn` level along with object and transaction counts. The division of duration by count is computed even when counts are zero, which would produce a divide-by-zero in practice — this is a minor robustness gap in the observability code path.

## Relationship to Sibling Files

The `ledger/` directory contains several related handlers (`LedgerData`, `LedgerEntry`, `LedgerDiff`, `LedgerHeader`, etc.), each addressing a narrower slice of ledger query functionality. `Ledger.cpp` is the broadest, handling the general-purpose `ledger` command that most clients use first. The shared lookup infrastructure (`lookupLedger`, `ledgerFromRequest`) is centralized in `RPCLedgerHelpers.h`, keeping ledger resolution logic out of individual handlers. The `LedgerFill` struct in `LedgerToJson.h` acts as the bridge between this handler's parameter interpretation and the actual JSON serialization logic.