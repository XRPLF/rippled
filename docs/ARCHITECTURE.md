# rippled Architecture Map

> **Purpose**: Tell agents _where_ things are. Not _why_ they exist.
> **Audience**: AI coding agents (Claude Code, Codex, Copilot) navigating the codebase.
> **Principle**: "Give agents a map, not an encyclopedia." — [Harness Engineering](./harness-engineering-report.md)

---

## Layer Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                     RPC Layer (Layer 5)                     │
│  HTTP/WebSocket → ServerHandler → 72 RPC Handlers           │
│  src/xrpld/rpc/                                             │
├─────────────────────────────────────────────────────────────┤
│                   Application Layer (Layer 4)               │
│  NetworkOPs · TxQ · PathFinding · LedgerMaster              │
│  src/xrpld/app/                                             │
├─────────────────────────────────────────────────────────────┤
│                   Consensus Layer (Layer 3)                 │
│  Consensus<Adaptor> · RCLConsensus · Validations            │
│  src/xrpld/consensus/ + src/xrpld/app/consensus/            │
├─────────────────────────────────────────────────────────────┤
│                   Overlay Layer (Layer 2)                   │
│  OverlayImpl · PeerImp · PeerFinder · Message               │
│  src/xrpld/overlay/ + src/xrpld/peerfinder/                 │
├─────────────────────────────────────────────────────────────┤
│                   Storage Layer (Layer 1)                   │
│  NodeStore (RocksDB/SQLite) · SHAMap · RDB                  │
│  src/libxrpl/nodestore/ + src/libxrpl/shamap/               │
├─────────────────────────────────────────────────────────────┤
│                   Foundation (Layer 0)                      │
│  beast:: (I/O, insight, clock) · protocol · crypto · json   │
│  src/libxrpl/ + include/xrpl/                               │
└─────────────────────────────────────────────────────────────┘
```

---

## Module Map

### xrpld (the daemon) — `src/xrpld/`

| Directory         | Purpose                                             | Key Files                                             |
| ----------------- | --------------------------------------------------- | ----------------------------------------------------- |
| `app/main/`       | Daemon entry point, Application lifecycle           | `Main.cpp`, `Application.h/.cpp`, `BasicApp.h`        |
| `app/consensus/`  | Consensus adaptor binding protocol to rippled types | `RCLConsensus.h/.cpp`                                 |
| `app/ledger/`     | Ledger management, building, acquiring              | `LedgerMaster.h`, `InboundLedgers.h`                  |
| `app/misc/`       | NetworkOPs, TxQ, HashRouter, Amendments             | `NetworkOPs.h/.cpp`, `TxQ.h`, `HashRouter.h`          |
| `app/tx/`         | Transaction application engine                      | `apply.h/.cpp`, `applySteps.h`                        |
| `app/paths/`      | Payment path finding                                | `Pathfinder.h`, `PathRequest.h`                       |
| `app/rdb/`        | Relational DB backend for state                     | `RelationalDatabase.h`                                |
| `consensus/`      | Generic consensus protocol (template-based)         | `Consensus.h/.cpp`, `ConsensusParms.h`                |
| `core/`           | Node configuration, time keeping                    | `Config.h/.cpp`, `TimeKeeper.h`                       |
| `overlay/`        | P2P network: peer connections, message routing      | `Overlay.h`, `Peer.h`, `Message.h`                    |
| `overlay/detail/` | Overlay implementation internals                    | `OverlayImpl.h/.cpp`, `PeerImp.h/.cpp`, `Handshake.h` |
| `peerfinder/`     | Peer discovery and bootstrap                        | `PeerfinderManager.h`, `detail/Logic.h`               |
| `perflog/`        | Performance logging (pre-OTel telemetry)            | `detail/PerfLogImp.h/.cpp`                            |
| `rpc/`            | JSON-RPC server and routing                         | `detail/Handler.h`, `RPCHandler.h`                    |
| `rpc/handlers/`   | **72 RPC command implementations**                  | `AccountInfo.cpp`, `Submit.cpp`, `Tx.cpp`, etc.       |
| `shamap/`         | SHAMap tree operations (daemon-side)                |                                                       |

### libxrpl (shared library) — `src/libxrpl/`

| Directory         | Purpose                                           | Key Files                                        |
| ----------------- | ------------------------------------------------- | ------------------------------------------------ |
| `basics/`         | Logging, string utils, tagged integers            | `Log.cpp` (log formatting — Phase 8 OTel target) |
| `beast/insight/`  | Metrics framework (StatsD, Gauge, Counter, Event) | `StatsDCollector.cpp` (Phase 6-7 OTel target)    |
| `beast/core/`     | Async I/O primitives                              | `CurrentThreadName.cpp`                          |
| `core/`           | Crypto, seed handling                             |                                                  |
| `crypto/`         | Ed25519, secp256k1, SHA                           |                                                  |
| `json/`           | JSON parser/writer                                |                                                  |
| `nodestore/`      | Key-value storage for ledger nodes                | `backend/RocksDBFactory.cpp`                     |
| `protocol/`       | XRPL protocol: serialization, STObject, STTx      | `STTx.cpp`, `STObject.cpp`                       |
| `server/`         | HTTP/WebSocket server core                        | `ServerHandler.h`                                |
| `tx/transactors/` | Per-transaction-type execution logic              | `Payment.cpp`, `CreateOffer.cpp`, etc.           |
| `tx/invariants/`  | Transaction invariant checks                      | `TransactionCheck.cpp`                           |

### Public Headers — `include/xrpl/`

Mirrors `src/libxrpl/` structure. Agent-accessible API surface for external consumers of libxrpl.

Key subdirectory: `include/xrpl/proto/org/xrpl/rpc/v1/` — Protocol Buffer definitions for gRPC API.

---

## Entry Points

| Operation              | Where execution starts                                                                     | Hot path        |
| ---------------------- | ------------------------------------------------------------------------------------------ | --------------- |
| **RPC request**        | `src/xrpld/rpc/detail/ServerHandler.cpp` → handler lookup → `src/xrpld/rpc/handlers/*.cpp` | Layer 5 → 4     |
| **Peer message**       | `src/xrpld/overlay/detail/PeerImp.cpp::onMessage()` → message-type dispatch                | Layer 2 → 3/4   |
| **Transaction submit** | `rpc/handlers/Submit.cpp` → `app/misc/NetworkOPs.cpp::submitTransaction()`                 | Layer 5 → 4 → 2 |
| **Transaction relay**  | `overlay/detail/PeerImp.cpp::handleTransaction()` → `NetworkOPs::processTransaction()`     | Layer 2 → 4     |
| **Consensus round**    | `app/consensus/RCLConsensus.cpp::startRound()` → phase transitions → accept                | Layer 3         |
| **Ledger close**       | `RCLConsensus.cpp::onClose()` → `acceptLedger()` → apply transactions                      | Layer 3 → 4 → 1 |
| **Ledger acquire**     | `app/ledger/InboundLedgers.cpp::acquire()` → peer fetch → SHAMap                           | Layer 4 → 2 → 1 |
| **Startup**            | `app/main/Main.cpp::main()` → `Application::setup()` → start all subsystems                | Layer 0-5       |

---

## Data Flow: Transaction Lifecycle

```
Client
  │
  ▼
RPC Submit (Layer 5)
  │ src/xrpld/rpc/handlers/Submit.cpp
  ▼
NetworkOPs::submitTransaction() (Layer 4)
  │ src/xrpld/app/misc/NetworkOPs.cpp
  ├──▶ TxQ::apply() — queue management
  │     src/xrpld/app/misc/TxQ.cpp
  ├──▶ HashRouter::setFlags() — deduplication
  │     src/xrpld/app/misc/HashRouter.cpp
  ▼
Overlay::relay() (Layer 2)
  │ src/xrpld/overlay/detail/OverlayImpl.cpp
  ├──▶ PeerImp::send() — to each connected peer
  │     src/xrpld/overlay/detail/PeerImp.cpp
  ▼
[Remote Node] PeerImp::handleTransaction() (Layer 2)
  │ src/xrpld/overlay/detail/PeerImp.cpp
  ▼
Consensus::startRound() (Layer 3)
  │ src/xrpld/consensus/Consensus.h
  │ src/xrpld/app/consensus/RCLConsensus.cpp
  ├──▶ propose() — send position to peers
  ├──▶ onClose() — close the ledger
  ▼
acceptLedger() → applyTransactions() (Layer 3 → 1)
  │ src/xrpld/app/consensus/RCLConsensus.cpp
  ▼
NodeStore::store() (Layer 1)
  │ src/libxrpl/nodestore/
```

---

## Build Targets

| Target               | Type                      | Source                           |
| -------------------- | ------------------------- | -------------------------------- |
| `xrpl`               | Shared library (libxrpl)  | `src/libxrpl/` + `include/xrpl/` |
| `xrpld`              | Executable (daemon)       | `src/xrpld/`                     |
| `rippled_unit_tests` | Unit test binary          | `src/test/`                      |
| `libxrpl_tests`      | Library integration tests | `src/tests/libxrpl/`             |

**Build command**: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel`
**With telemetry**: `cmake -B build -DXRPL_ENABLE_TELEMETRY=ON`

---

## Test Frameworks

| Framework       | Location                                 | Purpose                                                       |
| --------------- | ---------------------------------------- | ------------------------------------------------------------- |
| **JTx**         | `src/test/jtx/`                          | Transaction testing — simulates full ledger environment       |
| **CSF**         | `src/test/csf/`                          | Consensus Simulation Framework — multi-node consensus testing |
| **Unit tests**  | `src/test/app/`, `src/test/beast/`, etc. | Per-module unit tests                                         |
| **Integration** | `src/tests/libxrpl/`                     | Library-level integration tests                               |

---

## Telemetry Map

Links OTel instrumentation to codebase locations. See [09-data-collection-reference.md](../../OpenTelemetryPlan/09-data-collection-reference.md) for the full inventory.

| Span          | Source Location                            | Layer |
| ------------- | ------------------------------------------ | ----- |
| `rpc.*`       | `src/xrpld/rpc/detail/ServerHandler.cpp`   | 5     |
| `tx.submit`   | `src/xrpld/app/misc/NetworkOPs.cpp`        | 4     |
| `tx.relay`    | `src/xrpld/overlay/detail/PeerImp.cpp`     | 2     |
| `peer.send`   | `src/xrpld/overlay/detail/PeerImp.cpp`     | 2     |
| `consensus.*` | `src/xrpld/app/consensus/RCLConsensus.cpp` | 3     |
| `ledger.*`    | `src/xrpld/app/ledger/`                    | 4     |

| Metrics Source            | Code Location                                   | Phase        |
| ------------------------- | ----------------------------------------------- | ------------ |
| `beast::insight` (StatsD) | `src/libxrpl/beast/insight/StatsDCollector.cpp` | 6-7          |
| SpanMetrics (derived)     | OTel Collector config                           | 5            |
| PerfLog                   | `src/xrpld/perflog/detail/PerfLogImp.cpp`       | 9 (gap fill) |

---

## Key Abstractions

| Abstraction                 | Interface                                | Implementation                                                | What it does                           |
| --------------------------- | ---------------------------------------- | ------------------------------------------------------------- | -------------------------------------- |
| `Application`               | `src/xrpld/app/main/Application.h`       | `Application.cpp`                                             | Wires all subsystems together          |
| `Overlay`                   | `src/xrpld/overlay/Overlay.h`            | `detail/OverlayImpl.cpp`                                      | Manages P2P connections                |
| `Consensus<>`               | `src/xrpld/consensus/Consensus.h`        | Template, adaptor in `RCLConsensus.cpp`                       | Generic consensus protocol             |
| `NetworkOPs`                | `src/xrpld/app/misc/NetworkOPs.h`        | `NetworkOPs.cpp`                                              | Transaction processing, event dispatch |
| `LedgerMaster`              | `src/xrpld/app/ledger/LedgerMaster.h`    | `LedgerMaster.cpp`                                            | Ledger lifecycle management            |
| `NodeStore`                 | `include/xrpl/nodestore/Database.h`      | `src/libxrpl/nodestore/`                                      | Persistent key-value storage           |
| `beast::insight::Collector` | `include/xrpl/beast/insight/Collector.h` | `StatsDCollector`, `NullCollector`, (future: `OTelCollector`) | Metrics collection interface           |
| `JobQueue`                  | `src/xrpld/core/JobQueue.h`              | `JobQueue.cpp`                                                | Async task scheduling                  |
| `SHAMap`                    | `include/xrpl/shamap/SHAMap.h`           | `src/libxrpl/shamap/`                                         | Merkle tree for state                  |

---

## Dependency Rules

```
Layer 5 (RPC)        → may call → Layer 4 (App)
Layer 4 (App)        → may call → Layer 3 (Consensus), Layer 2 (Overlay), Layer 1 (Storage)
Layer 3 (Consensus)  → may call → Layer 4 (App via Adaptor), Layer 1 (Storage)
Layer 2 (Overlay)    → may call → Layer 4 (App for message handling)
Layer 1 (Storage)    → may call → Layer 0 (Foundation) only
Layer 0 (Foundation) → no upward dependencies
```

**Never**: Layer 0/1 must never depend on Layer 3/4/5. Layer 5 must never bypass Layer 4 to reach Layer 2/3 directly.

---

## Configuration

| File                    | Location | Purpose                   |
| ----------------------- | -------- | ------------------------- |
| `xrpld.cfg`             | `cfg/`   | Main daemon configuration |
| `validators.txt`        | `cfg/`   | Validator list            |
| `CMakeUserPresets.json` | Root     | Local build presets       |
| `conanfile.py`          | Root     | Dependency versions       |

---

_This document is a navigation aid for AI coding agents. For architectural rationale, see [docs/consensus.md](consensus.md). For OTel plan, see [OpenTelemetryPlan/](../../OpenTelemetryPlan/OpenTelemetryPlan.md). For coding conventions, see [docs/CodingStyle.md](CodingStyle.md)._
