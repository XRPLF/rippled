# `ServiceRegistry.h` — Dependency Injection Interface for the XRPL Application Layer

## Role and Motivation

`ServiceRegistry` solves a pervasive coupling problem in the XRPL codebase: historically, almost every component that needed access to any service held a reference to `Application`, the monolithic top-level object. This created a situation where a component needing only `LedgerMaster` and `JobQueue` would carry a full `Application&` dependency — dragging along lifecycle management, startup/shutdown logic, and every other subsystem as implicit dependencies.

`ServiceRegistry` is the first step in decomposing that monolith. It acts as a pure service-locator interface, separating service *access* from application *lifecycle management*. Components that only need to look up services can now depend on `ServiceRegistry&` instead of `Application&`, making their actual dependencies more explicit and their testability significantly higher. The comment `// This is temporary until we migrate all code to use ServiceRegistry` and the `getApp()` escape hatch at the bottom of the class confirm this is a live, in-progress migration rather than a finished design.

## Design: Why a Pure Abstract Interface?

The class has no data members, all methods are pure virtual, and the constructor and destructor are defaulted. This is deliberate for two reasons:

**Testability.** Any component that takes a `ServiceRegistry&` can be tested against a stub or mock implementation without instantiating the full XRPL application stack — no I/O threads, no databases, no network layer. The transaction processing pipeline has already migrated to this model: `Transactor`, `ApplyContext`, and the `apply()`/`preclaim()`/`doApply()` free functions in `include/xrpl/tx/` all take `ServiceRegistry&`, not `Application&`.

**Decoupled contract.** The concrete implementation lives in `Application` (which inherits from `ServiceRegistry` alongside `beast::PropertyStream::Source`), but the interface contract is stable and documented independently of the sprawling `Application` implementation details. Any future reimplementation or test double only needs to satisfy this interface.

The alternative — passing individual service references — would either cause constructor explosion or produce ad-hoc grouping structs that recreate the same coupling problem at a smaller scale.

## Forward Declarations and Type Aliases

The header deliberately uses forward declarations for all of its approximately 30 referenced types rather than including their headers. This is architecturally critical: `ServiceRegistry.h` is pulled in by a very large number of translation units, and including full headers for `LedgerMaster`, `Overlay`, `NetworkOPs`, and so on would create enormous transitive include chains. Forward declarations give callers the type name needed to hold a reference while deferring the full definition to the point of use.

Two type aliases are defined inline rather than delegated to another header: `CachedSLEs` (`TaggedCache<uint256, SLE const>`) and `NodeCache` (`TaggedCache<SHAMapHash, Blob>`). These are shared cache types accessed by multiple subsystems, and their full template signatures must be visible to any caller of `getCachedSLEs()` or `getTempNodeCache()`. Defining them here avoids duplication and keeps the interface self-consistent. For the same reason, the full `TaggedCache` template declaration is re-stated here even though it is also declared in `<xrpl/basics/TaggedCache.h>`.

## Service Groupings

The methods are organized into six logical categories reflecting the system's layered architecture:

**Core infrastructure** — `getJobQueue()`, `getTimeKeeper()`, `getNodeFamily()`, `getTempNodeCache()`, `getCachedSLEs()`, `getNetworkIDService()`, `getCollectorManager()`. These are cross-cutting services that most subsystems depend on directly.

**Protocol and validation** — `getAmendmentTable()`, `getHashRouter()`, `getFeeTrack()`, `getLoadManager()`, `getValidations()`, `getValidators()`, `getValidatorSites()`, `getValidatorManifests()`, `getPublisherManifests()`. Consensus and validator management, including separate caches for validator vs. publisher manifests.

**Network** — `getOverlay()`, `getCluster()`, `getPeerReservations()`, `getResourceManager()`. The peer-to-peer layer and its resource accounting.

**Storage** — `getNodeStore()`, `getSHAMapStore()`, `getRelationalDatabase()`. The persistence layer, with a clean separation between the key-value node store, the SHAMap-level storage management, and the relational (SQL) database.

**Ledger** — `getInboundLedgers()`, `getInboundTransactions()`, `getAcceptedLedgerCache()`, `getLedgerMaster()`, `getLedgerCleaner()`, `getLedgerReplayer()`, `getPendingSaves()`, `getOpenLedger()`. The open ledger accessor is provided in both mutable and `const` overloads — the only method pair with this distinction. This reflects that open ledger state is read far more often than it is mutated, and const-correctness here allows the compiler to enforce that separation at call sites.

**Transaction and server** — `getOPs()`, `getOrderBookDB()`, `getMasterTransaction()`, `getTxQ()`, `getPathRequestManager()`, `getServerHandler()`, `getPerfLog()`. Transaction processing and the RPC/WebSocket server.

The final group of utility methods — `isStopping()`, `getJournal()`, `getIOContext()`, `getLogs()`, `getTrapTxID()`, `getWalletDB()` — provide cross-cutting infrastructure that does not fit cleanly into a single subsystem.

## The `getApp()` Escape Hatch

The `getApp()` method returns a reference to the underlying `Application` object and is explicitly marked temporary with a comment directing future engineers to remove it once migration is complete. This is an honest acknowledgment that the refactor is incremental. Rather than forcing a big-bang migration or introducing subtle bugs by prematurely severing `Application` access, the design provides a controlled, visible escape hatch. The technical debt is explicit and trackable, which is the correct engineering trade-off during a large incremental refactor.

## Relationship to Sibling Files and Callers

Within `include/xrpl/core/`, `ServiceRegistry.h` is the aggregation point for many of the other components defined there. `JobQueue`, `HashRouter`, `NetworkIDService`, `PeerReservationTable`, and `PerfLog` are all forward-declared or referenced here, with concrete definitions in sibling headers. `ServiceRegistry` is the interface through which all of those are accessed by the broader application layer, making it the central hub of the dependency injection architecture for this module. Beyond the core module, the transaction processing pipeline (`Transactor`, `ApplyContext`, `apply`, `preclaim`, `doApply`) and subsystems like `DatabaseCon` checkpointing and overlay slot management have already adopted `ServiceRegistry&` in their interfaces, demonstrating the migration path that all remaining `Application&` callers are expected to follow.