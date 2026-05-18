# `include/xrpl/server/NetworkOPs.h` — The Central Client-Facing Interface

## Role and Purpose

`NetworkOPs` is the primary abstraction boundary between client-facing code — RPC handlers, WebSocket sessions, backend applications — and the internal machinery of an XRP Ledger node. The class comment puts it plainly: "code that wants to do normal operations on the network such as creating and monitoring accounts, creating transactions, and so on should use this interface." In practice, this means most RPC command implementations are thin wrappers that delegate nearly all meaningful work to a `NetworkOPs` instance.

The design is a pure-virtual abstract class, meaning the header defines a contract with no implementation. This is classical dependency inversion: the entire RPC layer depends on this abstraction, not on `NetworkOPsImp` (the concrete implementation in `src/xrpld/app/misc/NetworkOPs.cpp`). Consumers need only include this header and a handful of protocol headers, because all referenced types — `Peer`, `STTx`, `ReadView`, `LedgerMaster`, `RCLCxPeerPos` — appear only as forward declarations. The concrete implementation carries the full weight of consensus, ledger, and overlay includes. This architectural choice is especially important given how widely `NetworkOPs` is used across the codebase.

## `OperatingMode` — The Server's Self-Assessment

The `OperatingMode` enum defines five states representing the server's confidence in its relationship to the broader network:

- `DISCONNECTED (0)` — cannot process requests at all
- `CONNECTED (1)` — in contact with the network but not yet aligned
- `SYNCING (2)` — briefly behind
- `TRACKING (3)` — aligned with network consensus
- `FULL (4)` — holds a complete ledger and can validate

The explicit warning in the comment — "do not change them without verifying each use" — signals that these integer values are compared ordinally in downstream code. The `isFull()` convenience method tests for the `FULL` state, which is the only mode where a node can meaningfully participate in validation. Exposing this state through the interface lets callers and RPC responses communicate node health accurately rather than trusting blindly. The implementation's `StateAccounting` inner class (visible in the `.cpp`) tracks transition counts and cumulative microseconds spent in each mode — that metric surface is exposed here via `stateAccounting()`.

## Transaction Processing Pipeline

The interface exposes two distinct transaction entry points reflecting different origins and latency requirements.

`submitTransaction()` bears the comment "must complete immediately" — a contract, not a suggestion. It handles transactions arriving from network peers or clients where the caller cannot block. `processTransaction()` is richer: it carries context about whether the submitting connection has elevated privileges (`bUnlimited`), whether the transaction originated locally (`bLocal`), and the `FailHard` disposition. Local transactions from privileged connections may receive different queuing or rejection behavior than peer-relayed traffic.

`FailHard` is a scoped enum (`yes`/`no`) rather than a raw boolean, accompanied by the `doFailHard()` factory that converts a boolean parameter. This pattern addresses a real footgun: a parameter named `noMeansDont` inverted into a `FailHard` assignment would be silently wrong with a bare `bool`. The enum makes intent explicit at every call site. The `TransactionStatus` struct inside `NetworkOPsImp` stores the `FailHard` value alongside the transaction and carries an assertion enforcing that only local transactions may have `FailHard::yes` — network-received transactions never carry that flag.

`processTransactionSet()` handles batch consensus-driven processing via a `CanonicalTXSet`, where ordering guarantees matter and the batch is expected to be applied atomically within a ledger round.

## Consensus Integration

The methods `beginConsensus()`, `endConsensus()`, `processTrustedProposal()`, `recvValidation()`, and `mapComplete()` expose the integration points where the consensus engine drives state through `NetworkOPs`. The `std::unique_ptr<std::stringstream>` passed into `beginConsensus()` and `endConsensus()` is a structured log accumulator threaded through the consensus round. Passing it explicitly through the call stack avoids global or thread-local logging state while still collecting fine-grained diagnostic output for each consensus event — important when debugging a distributed agreement protocol.

`acceptLedger()` is explicitly scoped to standalone mode (its doc comment makes this clear) and supports the `ledger_accept` RPC command, which forces a virtual consensus round for development and testing. The optional `consensusDelay` parameter allows simulating time-dependent consensus behavior without modifying clock infrastructure.

## Amendment and UNL Blocking

Two independent blocking mechanisms are exposed at the interface level, each with get/set/clear methods:

**Amendment blocking** (`isAmendmentBlocked`, `setAmendmentBlocked`) captures a critical safety invariant: if the network has activated ledger amendments this node doesn't understand, it must stop processing transactions to avoid diverging from the rest of the network. **Amendment warning** (`isAmendmentWarned`, `setAmendmentWarned`, `clearAmendmentWarned`) provides a softer early signal for upcoming amendments that have not yet activated.

**UNL blocking** (`isUNLBlocked`, `setUNLBlocked`, `clearUNLBlocked`) handles the case where the Unique Node List configuration is problematic — for instance, when the node cannot reach a quorum of its trusted validators. The combined `isBlocked()` method checks both amendment-blocked and UNL-blocked in a single call, useful for gating operations that require the node to be fully operational.

These are exposed at the abstract interface level because RPC responses — `server_info`, error replies to transaction submissions — must communicate these conditions to operators and monitoring systems.

## Subscription Publishing

`NetworkOPs` inherits from `InfoSub::Source` (defined in `InfoSub.h`), which provides the complete subscriber-management interface: subscribing and unsubscribing clients to account activity, ledger closes, book changes, validations, peer status, consensus events, and manifests. `NetworkOPs` provides the publisher half. When a ledger closes, `pubLedger()` is called; when a transaction enters the open ledger as a proposal, `pubProposedTransaction()` fires; when a validator vote is received, `pubValidation()` notifies subscribers. This dual role — operational interface for client commands and event source for streaming data feeds — means `NetworkOPs` is the single object that threads together the node's state machine and its outward-facing subscription system.

## Design Notes

The `clock_type` alias (`beast::abstract_clock<std::chrono::steady_clock>`) reflects a broader pattern in the XRPL codebase of injecting abstract clocks to support deterministic testing. The `setNeedNetworkLedger()` / `clearNeedNetworkLedger()` pair manages a flag signaling that the node has not yet acquired a reference ledger from the network — a necessary bootstrapping condition before the node can track consensus. `updateLocalTx()` and `getLocalTxCount()` expose the local transaction cache, which retains transactions submitted by this node across ledger closings so they can be re-applied if dropped from a consensus round.