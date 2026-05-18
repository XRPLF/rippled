# `JsonPropertyStream.h` — JSON-backed Property Stream Sink

`JsonPropertyStream` is a concrete implementation of `beast::PropertyStream` that captures a hierarchical diagnostic snapshot as a `Json::Value` object tree. Its only production consumer is the `doPrint` admin RPC handler (`src/xrpld/rpc/handlers/admin/status/Print.cpp`), where it bridges the `PropertyStream` tree-walk API and the JSON response envelope sent back to operators.

## The `PropertyStream` Framework

`beast::PropertyStream` (in `include/xrpl/beast/utility/PropertyStream.h`) is an abstract sink whose interface mirrors a generic property tree: `map_begin`/`map_end` delimit named objects, `array_begin`/`array_end` delimit sequences, and two families of overloaded `add` methods write leaf values — one family is keyed (object fields), the other is positional (array elements). `PropertyStream::Source` subclasses override `onWrite(Map&)` and can carry a tree of child `Source` objects, allowing the entire application-level component hierarchy to be dumped recursively by calling `Source::write(stream)`.

`JsonPropertyStream` does nothing more than implement all of those pure virtual methods against a live `Json::Value` tree.

## Stack-Based Tree Construction

The class maintains two public members: `m_top`, a `Json::Value` initialized to `objectValue` in the constructor, and `m_stack`, a `std::vector<Json::Value*>` that tracks the currently-active nesting level. The constructor pre-reserves 64 entries in the stack — a practical upper bound on diagnostic nesting depth — to avoid heap reallocations during a write pass.

The pointer discipline relies on a key property of `Json::Value`: once a child node is inserted via `operator[]` or `append()`, the returned reference remains stable because `Json::Value` manages its children through a separately allocated map/array rather than by value inside a resizing container. This means the raw `Json::Value*` pointers stored in `m_stack` stay valid for the lifetime of `m_top`.

When `map_begin(key)` is called, the implementation does `top[key] = Json::objectValue` and pushes the address of that newly-created child. The keyless `map_begin()` instead calls `top.append(Json::objectValue)`, meaning the parent must already be an array. The symmetry holds for `array_begin`: the keyed variant attaches to a map, the keyless variant nests inside an array. Both `map_end` and `array_end` simply pop the stack.

## Type Narrowing for `long`

One non-obvious implementation detail: both `add(std::string const& key, long v)` and the array-mode `add(long v)` explicitly cast `v` to `int` before storing into `Json::Value`. `Json::Value` exposes `Int` and `UInt` storage (32-bit) alongside `Int64`/`UInt64`, and on platforms where `long` is 64 bits the jsoncpp API would select a different overload. The explicit cast is a deliberate narrowing to keep the generated JSON representation consistent across LP64 and ILP32 platforms. This is a subtle correctness tradeoff: values larger than `INT_MAX` will silently truncate, but the diagnostic properties this class serializes (counter values, sizes, peer counts) are all expected to fit.

## Usage in the Admin RPC

`doPrint` instantiates a `JsonPropertyStream` on the stack, calls `context.app.write(stream)` (or the path-filtered overload when a `params` argument is present), and returns `stream.top()` directly as the RPC result. This makes the entire `PropertyStream::Source` hierarchy of the running application introspectable over the admin interface with a single pass — every component that subclasses `Source` and overrides `onWrite` contributes its properties to the returned JSON object without any bespoke serialization code.

The public exposure of `m_top` and `m_stack` (rather than keeping them private) is worth noting: it is not idiomatic for an encapsulated class, but it enables direct in-place construction of nested `Json::Value` nodes and avoids any intermediate copy. Since the class is a short-lived sink rather than a long-lived data structure, the loose encapsulation is acceptable.