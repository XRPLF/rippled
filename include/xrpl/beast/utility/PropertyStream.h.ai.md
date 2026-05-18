# `PropertyStream.h` — Structured Hierarchical Diagnostic Introspection

This header defines the entire `PropertyStream` framework: an abstract streaming interface for exposing internal XRPL subsystem state as structured key-value trees, along with the RAII scope-guard wrappers and a self-registering `Source` tree that make it usable without coupling components to any particular output format.

## The Core Problem

XRPL's rippled process hosts dozens of concurrent subsystems — peer finders, ledger cleaners, resource managers, overlay management — all of which need to expose diagnostic state for operator inspection, typically over an RPC endpoint. The naive approach (returning hardcoded JSON from each component) couples the diagnostic logic to the serialization format. `PropertyStream` solves this by defining a pure-virtual interface that accepts structured writes, so subsystems describe *what* they want to emit while the concrete stream implementation decides *how*. The `JsonPropertyStream` class (in `include/xrpl/json/JsonPropertyStream.h`) is the primary concrete implementation, directing all calls into a `Json::Value` object tree.

## The Abstract Stream Interface

`PropertyStream` is an abstract base class with two families of pure-virtual methods: the `map_begin()`/`map_end()` pair for named object scopes, and the `array_begin()`/`array_end()` pair for anonymous array scopes. On top of these, an overloaded `add()` family covers every primitive C++ numeric type plus `bool` and `std::string`. Non-`string` types are routed through the protected `lexical_add()` template, which serializes into a `std::stringstream` then delegates to the pure-virtual `add(std::string const&, std::string const&)`. The `bool` overloads are deliberately special-cased at the virtual level to allow implementors to emit `true`/`false` as JSON booleans rather than integers.

## RAII Scope Guards: `Map` and `Set`

`Map` and `Set` are non-copyable RAII wrappers that bracket `PropertyStream` structural calls. `Map`'s constructor calls either `map_begin()` or `map_begin(key)` depending on whether the map is named, and the destructor calls `map_end()`. `Set` does the same for arrays. This guarantees structural balance — a requirement for any hierarchical format — without placing the burden on calling code. The multiple `Map` constructors accepting `PropertyStream&`, `Set&`, or another `Map& ` parent allow natural nesting: `Map inner("stats", outer)` opens a named sub-object within an existing object scope.

`Map` also exposes `operator[]`, which returns a `Proxy` rather than accepting a value directly. The `Proxy` holds an `std::ostringstream` and commits to the stream only in its destructor, enabling the expression `map["key"] << computedValue` to work naturally: the temporary `Proxy` is streamed into, then destroyed at the end of the expression, at which point it calls `m_map->add(m_key, buffer.str())` if the buffer is non-empty. The `operator=` on `Proxy` provides the simpler assignment form `map["key"] = value`.

## The `Source` Tree

`Source` is the class that subsystems inherit from. Each `Source` has a string name, a `std::recursive_mutex`, a raw `parent_` pointer, an `Item` (its handle in the parent's child list), and a `List<Item>` of children. The relationship between `Item` and `Source` is intentionally indirect: `Item` inherits from `List<Item>::Node` (an intrusive doubly-linked list node from `beast::List`) and stores a back-pointer to its owning `Source`. This avoids separate heap allocations for list management while still keeping the parent's child list traversable via `Item::source()` dereferences.

Registering a child calls `Source::add(Source& child)`, which acquires both `lock_` and `child.lock_` via `std::lock()` in deadlock-safe order. The destructor of `Source` acquires `lock_`, removes itself from its parent (if any), then calls `removeAll()` to detach all children. Children must outlive or be removed before the parent that holds their `Item` nodes, so the ownership discipline is entirely caller-managed by object lifetime.

Concrete subsystems override `onWrite(Map&)`, which by default does nothing. When diagnostic output is requested, `write(PropertyStream&)` locks `lock_`, calls `onWrite()` to populate a `Map` from the current `Source`, then recursively calls `write()` on each child. `write_one()` skips the recursive descent. Both entry points bracket the output in a keyed `Map` named after the `Source`.

## Path Navigation

`find(std::string path)` implements a mini-language for targeting specific nodes in the tree. Paths are dot-delimited names; a trailing `*` signals recursive output. The static helpers `peel_leading_slash()`, `peel_trailing_slashstar()`, and `peel_name()` tokenize the path string by mutating a local copy. `find()` returns a `std::pair<Source*, bool>` where the bool indicates whether the `*` wildcard was present, allowing the caller to choose between `write_one()` and `write()`. This design lets a single HTTP endpoint serve both summary (`"ledgercleaner"`) and deep-dive (`"ledgercleaner.*"`) requests without any per-subsystem routing logic.

## Relationship to Dependent Files

`List.h` provides the intrusive doubly-linked list that `Source` uses to track children without heap allocation overhead. `JsonPropertyStream.h` provides the concrete implementation; its stack of `Json::Value*` pointers mirrors the nesting depth opened by `Map` and `Set` RAII guards. `LedgerCleaner` is a representative subsystem that inherits `PropertyStream::Source("ledgercleaner")` and implements `onWrite()` to stream current cleaner state into the diagnostic tree, illustrating how the framework integrates naturally into service class hierarchies.