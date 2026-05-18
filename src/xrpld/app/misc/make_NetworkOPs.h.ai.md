# `make_NetworkOPs.h` — Factory Declaration for the Network Operations Subsystem

This header exposes a single factory function, `make_NetworkOPs`, that constructs and returns the node's central `NetworkOPs` object. It is the only public surface for creating what is one of the most complex objects in `rippled`: the component that orchestrates consensus participation, transaction submission, subscription management, and server-state tracking for an XRPL node.

## The Factory Pattern and Why It Matters Here

The concrete implementation, `NetworkOPsImp`, is defined entirely inside `NetworkOPs.cpp` and never exposed in any header. Callers only ever see the abstract `NetworkOPs` interface (pulled in via `<xrpl/server/NetworkOPs.h>`). This strict separation means the thousands of lines in `NetworkOPs.cpp` never contaminate compilation units that only need to call into `NetworkOPs` methods — a meaningful build-time benefit given how widely the object is used. The pattern matches other factory headers in the same directory (`make_Overlay.h`, `setup_HashRouter.h`), reflecting a consistent subsystem-construction idiom across the codebase.

Ownership is transferred to the caller as `std::unique_ptr<NetworkOPs>`, making the lifetime contract unambiguous: whoever calls this function owns the object and is responsible for its destruction.

## Parameter Roles and Their Influence on Initial State

The parameters are more than passive configuration — several actively shape the `NetworkOPsImp` state machine at construction time:

- **`standalone`** — a boolean that, when true, tells the node it will never have peers. This eliminates peer-count quorum checks and is used for private or testing deployments.
- **`startValid`** — when true, the node's `OperatingMode` is initialized to `FULL` rather than `DISCONNECTED`. The constructor also sets `minPeerCount_` to zero in this case, because a node declared valid at startup does not need to wait for peer connections before processing ledgers.
- **`minPeerCount`** — otherwise taken from `config_->NETWORK_QUORUM` in `Application.cpp`; it sets the floor of connected peers required before the node considers itself networked.

These three flags together encode the node's participation role at startup and are the primary inputs that drive how quickly (or whether) the node transitions from `DISCONNECTED` through `SYNCING` to `FULL`.

The remaining parameters are injected services that `NetworkOPsImp` holds by reference or shared ownership throughout its lifetime: `ServiceRegistry` provides access to other application-level services; `clock` is the monotonic clock used for timing consensus rounds and heartbeat timers; `JobQueue` offloads async work; `LedgerMaster` is the authoritative ledger store; `ValidatorKeys` carries the node's validator identity and master public key; `ioCtx` backs the internal Asio timers (`heartbeatTimer_`, `clusterTimer_`, `accountHistoryTxTimer_`); `journal` and `collector` handle logging and metrics respectively.

## Call Site

`make_NetworkOPs` is called exactly once, inside the `Application` constructor in `Application.cpp`, where it initializes the `m_networkOPs` member alongside every other major subsystem. The result is stored as `std::unique_ptr<NetworkOPs>`, and the raw pointer is subsequently shared throughout the application via `Application::getOPs()`.