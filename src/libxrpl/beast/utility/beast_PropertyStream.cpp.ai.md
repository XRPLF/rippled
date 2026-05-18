# `beast_PropertyStream.cpp` — Hierarchical Diagnostic Property Emission

## Role in the System

This file implements the `beast::PropertyStream` family of classes: an infrastructure for exposing structured, hierarchical runtime state from XRPL subsystems. Components such as `LedgerCleaner`, `PeerFinder`, `OverlayImpl`, and `ResourceManager` inherit from `PropertyStream::Source` and override `onWrite()` to report their internal state (counters, flags, status strings) in a tree structure suitable for diagnostics, RPC, or logging. The design separates *what to emit* (the `Source` subclass hierarchy) from *how to emit it* (the abstract `PropertyStream` backend, which concrete implementations serialize to JSON or other formats).

## RAII Scoping: `Map` and `Set`

`Map` and `Set` are the primary user-facing types. They are pure RAII wrappers around the abstract `PropertyStream` protocol. `Map` calls `map_begin()` in its constructor and `map_end()` in its destructor; `Set` does the same with `array_begin()` / `array_end()`. Because callers instantiate them as stack variables, the open/close bracketing of the output format is enforced automatically — nested `Map` objects produce nested maps in the output stream without any explicit close calls.

`Map` is deliberately non-copyable and non-movable. This enforces single ownership semantics: each `Map` instance corresponds to exactly one scope in the output. The multiple constructors (`Map(PropertyStream&)`, `Map(Set&)`, `Map(key, Map&)`, `Map(key, PropertyStream&)`) cover the compositional cases — a keyed sub-map within a parent map, an anonymous map within a set, and a root map wrapping a stream directly.

## The `Proxy` Pattern

`Map::operator[](key)` returns a `Proxy` rather than writing immediately. The `Proxy` holds a reference to the parent `Map`, the key string, and a `mutable std::ostringstream`. Callers write values into it using `<<` or `operator=`, and on destruction the proxy flushes the accumulated string to the map — but only if the string is non-empty. This deferred-commit pattern means `map["foo"] = 42` and `map["foo"] << "bar"` both work uniformly, and there is no entry emitted if nothing is written to the proxy. The `mutable` qualifier on `m_ostream` allows `const Proxy` objects returned from `const Map::operator[]` to still accumulate output.

## The `Source` Tree

`Source` forms a named tree of diagnostic providers. Each `Source` has a string name and a `List<Item> children_` intrusive list. Rather than storing `Source*` pointers directly, the list holds `Item` nodes, and each `Source` contains exactly one embedded `Item item_` that points back to itself. This indirection allows the `Source` object to be linked into the parent's `children_` list without allocating a separate node — the intrusive node is part of the `Source` itself.

`Source::add(Source&)` wires a child into the parent's list. The design enforces a strict invariant: `parent_` must be `nullptr` before attachment, checked via `XRPL_ASSERT`. This prevents a `Source` from being added to two parents simultaneously, which would corrupt both lists. The dual-lock acquisition in `add()` and `remove()` uses `std::lock()` (not sequential `lock()` calls) to avoid deadlock when two threads try to cross-link sources concurrently.

The `lock_` is a `std::recursive_mutex`. This is required because the destructor calls `parent_->remove(*this)` and then `removeAll()`, and `removeAll()` calls `remove()` while already holding `lock_` — the recursion must be re-entrant. The destructor's cleanup order (detach from parent, then detach children) prevents dangling `parent_` pointers in children and dangling child entries in the parent.

## Path Navigation

`Source::write(stream, path)` supports slash-delimited path queries for targeted diagnostic output. The path syntax is parsed using three mutating helper functions on a `std::string*`:

- `peel_leading_slash`: detects and strips a leading `/`, signaling a rooted (absolute) path.
- `peel_trailing_slashstar`: detects and strips a trailing `/*`, signaling a deep (recursive) query.
- `peel_name`: consumes the next path component up to the first `/`.

An unrooted path triggers `find_one_deep()`, which does a depth-first recursive search through the full subtree for the first `Source` whose name matches the first path component. Once a starting source is found, `find_path()` walks the remaining path components by repeatedly calling `find_one()` — which only examines immediate children. The `find()` function returns a `std::pair<Source*, bool>` where the boolean indicates whether the wildcard was present (meaning the write should recurse into children).

## Type Dispatch in `PropertyStream::add()`

The base class provides overloads of `add(key, value)` for every fundamental numeric type. All numeric overloads delegate to the template `lexical_add(key, value)`, which converts the value through a `std::stringstream` before calling the pure-virtual `add(key, std::string)`. Boolean values are special-cased to emit the strings `"true"` / `"false"` rather than `"1"` / `"0"`, which is consistent with how boolean state is expected to appear in XRPL diagnostic output. This overload structure keeps the subclass contract minimal: a concrete `PropertyStream` only needs to implement `add(key, std::string)`, `map_begin/end`, and `array_begin/end`.

## Concurrency Considerations

The design is multi-thread-aware but not designed for high-frequency concurrent writes. `Source::write()` acquires `lock_` only briefly while iterating over `children_` after calling `onWrite()`. This means the per-source write itself (`onWrite`) is not protected by the property stream lock — subclasses are responsible for protecting their own internal state (as `LedgerCleaner` does, using its own `mutex_` inside `onWrite`). The tree structure (parent/child linkage) is protected, but diagnostic emission is not serialized across the tree. A `Source` being destroyed concurrently with a traversal is safe only within the constraints of the per-`Source` lock; callers should ensure tree topology is stable during a full `write()` pass.