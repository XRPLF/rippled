# `LedgerHandler` — `src/xrpld/rpc/handlers/ledger/Ledger.h`

## Role in the System

`LedgerHandler` is the server-side implementation of the `ledger` JSON-RPC command — the primary endpoint clients use to inspect ledger state, transactions, and the transaction queue. The header defines the class contract that the RPC dispatch framework uses to wire this command into the live handler table.

It is one of only two "new-style" class-based handlers in the codebase (the other being `VersionHandler`). All other RPC commands in `rippled` are registered as bare function pointers in the legacy `handlerArray` table in `Handler.cpp`. `LedgerHandler` and `VersionHandler` are added separately via the `addHandler<T>()` template during `HandlerTable` construction.

## Handler Protocol

The class follows a two-phase protocol enforced by `addHandler<T>()`:

1. **`check()`** — validates incoming request parameters, resolves the target ledger via `lookupLedger()`, builds the `options_` bitmask from boolean flags (`full`, `transactions`, `accounts`, `expand`, `binary`, `owner_funds`, `queue`), enforces permission checks, and optionally fetches transaction-queue snapshots. It returns a `Status` that aborts dispatch on error.

2. **`writeResult(Json::Value&)`** — serializes the response. If a specific ledger was resolved, it merges the intermediate JSON from `lookupLedger` with a full `addJson()` call that respects the `options_` bitmask and the captured `queueTxs_`. If no ledger selector was given at all, it takes the fallback path and returns *both* the current open and last-closed ledger headers under `open` and `closed` keys.

## Static Metadata as a Compile-Time Contract

The `static constexpr` members are not just documentation — `addHandler<T>()` reads them at registration time and validates them with `static_assert`:

```cpp
static constexpr char name[]          = "ledger";
static constexpr unsigned minApiVer   = RPC::apiMinimumSupportedVersion;
static constexpr unsigned maxApiVer   = RPC::apiMaximumValidVersion;
static constexpr Role role            = Role::USER;
static constexpr Condition condition  = NO_CONDITION;
```

`role = Role::USER` means the command is available to all authenticated callers; admin privilege is not required. `condition = NO_CONDITION` means the handler may run regardless of network synchronisation state — unlike commands that require `NEEDS_CURRENT_LEDGER` or `NEEDS_CLOSED_LEDGER`, the `ledger` RPC is safe to serve even during initial sync, because it can always return whatever state is locally available.

## Private State and Why It's Structured This Way

Four private members carry state across the two phases:

- `context_` — a reference to the `JsonContext` (which bundles `Application&`, `LedgerMaster&`, `NetworkOPs&`, role, API version, and the raw `Json::Value params`). Held by reference rather than copied because the context is owned by the request lifetime.
- `ledger_` — a `shared_ptr<ReadView const>` populated by `lookupLedger()`. The `ReadView` abstraction allows `check()` and `writeResult()` to be indifferent to whether the ledger is the open ledger, a closed ledger, or a historical ledger — all present the same read-only interface.
- `queueTxs_` — a `std::vector<TxQ::TxDetails>` snapshot of the transaction queue, fetched only when `queue=true` is requested *and* the resolved ledger is open. The validation in `check()` explicitly rejects `queue=true` against a closed/validated ledger with `rpcINVALID_PARAMS`, because the queue concept is only meaningful for the in-progress ledger.
- `options_` — an integer bitmask combining `LedgerFill` flag constants. Building this once in `check()` means `writeResult()` can pass a single integer to `addJson()` rather than re-parsing parameters.
- `result_` — an intermediate `Json::Value` populated by `lookupLedger()` during `check()`. Because `lookupLedger` may emit its own diagnostic fields, this is merged into the final output via `copyFrom()` in `writeResult()`.

## Permission and Load Guards

`check()` gates the expensive full-ledger and full-account-state dump operations behind `isUnlimited(context_.role)`, which returns `true` only for `IDENTIFIED` and `ADMIN` roles. If the local node is also under heavy load (`isLoadedLocal()`), even an admin caller is rejected with `rpcTOO_BUSY`. This deliberately prevents public WebSocket clients from triggering multi-megabyte serialisations of the entire state tree. Binary mode (`binary=true`) is charged at `feeMediumBurdenRPC` vs `feeHeavyBurdenRPC` for JSON, reflecting the significantly smaller output.

## gRPC Companion

The `.cpp` file also defines `doLedgerGrpc()` — a free function outside the `RPC` namespace that serves the equivalent protobuf `GetLedgerRequest`. This function shares no code path with `LedgerHandler`; it handles `get_objects` by computing a `SHAMap::Delta` between consecutive ledgers and optionally returning predecessor/successor neighbours for each changed object. The two implementations coexist in the same translation unit but represent entirely separate protocol surfaces with different capability sets.