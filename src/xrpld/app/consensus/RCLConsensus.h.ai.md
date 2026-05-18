# `RCLConsensus.h` — XRP Ledger Consensus Orchestrator

## Role in the System

`RCLConsensus` is the bridge between the abstract, ledger-agnostic consensus algorithm (`Consensus<Adaptor>`) and the concrete XRP Ledger application. The generic engine in `src/xrpld/consensus/Consensus.h` is a policy-based template — it knows nothing about XRPL ledger format, peer networking, or the SHAMap transaction set representation. `RCLConsensus` supplies all of that context through a nested `Adaptor` class that satisfies the `Consensus<>` template requirements, while also providing a clean, mutex-protected public interface that the rest of `xrpld` interacts with.

## Two-Layer Architecture

The file declares two cooperating classes within the `xrpl` namespace.

**`RCLConsensus`** is the externally visible class. It holds a `recursive_mutex` that guards all access to the internal `Consensus<Adaptor>` object. Every public method — `startRound()`, `timerEntry()`, `gotTxSet()`, `peerProposal()`, `simulate()` — acquires this lock before delegating to the inner `consensus_` member. The `prevLedgerID()` method is a textbook example: it takes the lock and immediately reads a value, ensuring callers from multiple threads see a consistent result. The recursive nature of the mutex is deliberate: some code paths reenter the lock from within the same thread during complex state transitions.

**`RCLConsensus::Adaptor`** is the private implementation class. It holds all the application-level services (`Application&`, `LedgerMaster&`, `LocalTxs&`, `InboundTransactions&`) and implements the callbacks that `Consensus<Adaptor>` calls at each phase transition. Crucially, the code comment in the header is explicit: these private callback methods are *only* ever called by `Consensus<Adaptor>` (via friend declaration), which means they execute while the outer mutex is already held — except for the `onAccept` dispatched job (see below). This single-writer invariant is what makes the callback implementations safe without needing to re-acquire locks of their own.

## Adaptor Type Bindings

`Adaptor` defines the associated type aliases that parameterize the generic algorithm:

- `Ledger_t = RCLCxLedger` — thin shared-ptr wrapper over `Ledger const`
- `TxSet_t = RCLTxSet` — backed by a `SHAMap` snapshot
- `PeerPosition_t = RCLCxPeerPos` — a signed proposal from a peer
- `NodeID_t = NodeID`, `NodeKey_t = PublicKey`

These bindings mean the generic `Consensus<>` state machine never sees raw XRPL types — it works through these adaptor types, which allows the algorithm to be tested in isolation with lightweight stubs.

## State-Accessible Atomics

Four `Adaptor` members are `std::atomic`: `validating_`, `prevProposers_`, `prevRoundTime_`, and `mode_`. These have corresponding public getters (`validating()`, `prevProposers()`, `prevRoundTime()`, `mode()`) on both `Adaptor` and `RCLConsensus`. The atomic storage is intentional: callers like RPC handlers and monitoring code need to read consensus status without contending for the main consensus mutex. The rest of the application can sample the current consensus mode or round statistics at any time with no lock overhead.

## Lifecycle Callbacks

The consensus cycle flows through three primary `Adaptor` callbacks:

**`onClose()`** fires when the open ledger closes. It snapshots the open ledger's transactions into an immutable `SHAMap`, then conditionally injects pseudo-transactions for fee voting, amendment voting, and negative-UNL voting based on whether the current ledger is a flag ledger or voting ledger. It also calls `censorshipDetector_.propose()` with the initial transaction set, beginning censorship tracking for this round.

**`onAccept()`** is the most architecturally interesting callback. Rather than directly processing the agreed ledger, it dispatches a `jtACCEPT` job onto the application's `JobQueue`. The comment in the implementation explicitly explains why no lock is held in that job: the generic `Consensus<>` guarantees that once `onAccept` is called, the consensus result state won't change until `startRound` is called by `endConsensus()`. This deferred, lock-free dispatch avoids blocking the consensus timer while the expensive ledger-building and validation I/O happens on a worker thread.

**`onForceAccept()`** handles simulation and forced-accept scenarios — it calls `doAccept()` directly, synchronously, without the job queue indirection.

**`doAccept()`** is the shared implementation backing both paths. It builds a `CanonicalTXSet` from the agreed transaction set (using the SHAMap hash as a seed for deterministic-but-unpredictable ordering), calls `buildLCL()` to apply transactions and produce the new closed ledger, runs censorship detection, optionally calls `validate()` to sign and broadcast a validation, builds a new open ledger from disputed and retried transactions, and finally updates the time-keeper using a weighted average of peer close-time reports.

## Censorship Detection

`RCLCensorshipDetector<TxID, LedgerIndex>` tracks transactions the local node has proposed that haven't made it into a consensus ledger. The `censorshipWarnInternal` constant of 15 means a warning is emitted every 15 ledgers that a proposed transaction remains excluded. The detector is reset via `onModeChange()` whenever the node leaves the proposing or observing modes, preventing stale warnings after network reconnects.

## Validation Cookie

During `Adaptor` construction, `valCookie_` is assigned a randomly chosen non-zero `uint64_t`. This cookie is embedded in outgoing `STValidation` messages to let recipients distinguish fresh validations from replayed ones, providing a lightweight replay-protection mechanism that doesn't require persistent state across restarts.

## `preStartRound()` — The Gatekeeper

Before each new consensus round, `preStartRound()` determines whether the node should actively propose. It checks that validator keys are configured, that the previous ledger sequence exceeds `getMaxDisallowedLedger()` (preventing validation of ledgers before the server fully synced), that the node is not amendment-blocked, and that the UNL has not expired. If all checks pass and the node is in `OperatingMode::FULL`, it returns `true`, meaning the node will enter the round as a proposer. Otherwise, it participates only as an observer.

## `RclConsensusLogger`

The companion `RclConsensusLogger` class is an RAII timing helper. Constructed with a label and a `validating` flag, it allocates a `stringstream` only when logging is warranted (always for validators, otherwise at `INFO` level). On destruction it appends the elapsed time in seconds and flushes the accumulated log content at the appropriate level. It exists because consensus heartbeat processing spans multiple function calls — the logger correlates all of them into a single timestamped trace entry rather than emitting disconnected log lines.