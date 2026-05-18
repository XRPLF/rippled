# `include/xrpl/core/StartUpType.h`

## Role in the System

This header defines the `StartUpType` scoped enumeration, which encodes how a `rippled` node should initialize its ledger state when it comes online. It is a small but structurally important piece of configuration: the value selected here drives branching logic in both the application bootstrap sequence (`Application.cpp`) and database-connection setup (`DatabaseCon.h`), making it a shared vocabulary type that must be visible to multiple layers of the stack without pulling in heavyweight dependencies.

## The Enum Values

`StartUpType` declares six mutually exclusive startup modes:

- **`Normal`** — The default (`Config::START_UP` is initialized to this value). The node resumes from whatever ledger state is already persisted in its local databases, then syncs with the network as usual.
- **`Fresh`** — Triggered by the `--start` CLI flag. The node creates a brand-new genesis ledger, activating all currently desired amendments immediately. This is used when bootstrapping a new private network from scratch.
- **`Load`** — Triggered by `--ledger <seq/hash>` or `--load` / `FAST_LOAD`. The node loads a specific ledger by sequence number or hash from its local database and begins from there.
- **`LoadFile`** — Triggered by `--ledgerfile <path>`. Similar to `Load`, but reads ledger state from an external file path rather than the internal database.
- **`Replay`** — Triggered by `--ledger <seq/hash>` combined with `--replay`. The node loads the specified ledger and replays its transactions, optionally with a transaction trap (`--trap_tx_hash`) for debugging.
- **`Network`** — Triggered by `--net`. The node requests the current ledger directly from network peers rather than relying on any local state. The comment in `Application.cpp` notes this "should probably become the default once we have a stable network."

## Design Decisions

The `operator<<` overload streams the enum as its raw underlying integer rather than a human-readable name. This is intentional for compactness in log output — the call site in `Application.cpp` is a debug-level log line (`JLOG(m_journal.debug()) << "startUp: " << startUp`), where a numeric value is acceptable. The implementation uses `static_cast<std::underlying_type_t<StartUpType>>(type)`, which is the idiomatic, zero-overhead way to expose an enum's numeric identity without coupling to any string table.

The `#include <iosfwd>` (rather than `<ostream>`) keeps the header lightweight: `iosfwd` only provides the forward declaration of `std::ostream`, which is sufficient for the `operator<<` signature. The full `<ostream>` definition is deferred to translation units, avoiding unnecessary header bloat in every file that includes `Config.h` or `DatabaseCon.h`.

## Impact on Database Setup

One non-obvious consequence of `StartUpType` appears in `DatabaseCon.h`. When the node is running in standalone mode, `DatabaseCon`'s constructor decides whether to use an ephemeral in-memory SQLite database or a real on-disk file. The rule is: standalone + any mode *except* `Load`, `LoadFile`, or `Replay` → use a temporary (in-memory) database; otherwise use the configured data directory. This means that even in standalone mode, if you are loading or replaying a historical ledger you need durable storage, so the on-disk path is kept. The enum values thus implicitly categorize into "needs persistent storage" (`Load`, `LoadFile`, `Replay`) versus "ephemeral-compatible" (`Fresh`, `Normal`, `Network`).

## Relationship to Config

`Config.h` holds the `START_UP` member of type `StartUpType`, defaulted to `StartUpType::Normal`. The CLI parsing logic in `Main.cpp` mutates this field based on command-line flags, and `ApplicationImp::setup()` reads it once to decide the initial ledger-loading strategy — either calling `startGenesisLedger()`, `loadOldLedger()`, or `setNeedNetworkLedger()` on the network operations subsystem. The enum thus cleanly separates the concern of *which mode was requested* (owned by `Config`) from the concern of *what to do about it* (owned by `Application`).