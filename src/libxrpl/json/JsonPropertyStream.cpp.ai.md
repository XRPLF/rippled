# `JsonPropertyStream.cpp` — JSON Sink for the PropertyStream Framework

## Role and Context

`JsonPropertyStream` is a concrete implementation of `beast::PropertyStream`, the abstract base class that models a write-once property tree. The `PropertyStream` hierarchy exists to let subsystems expose diagnostic state without coupling them to any particular output format — a `PropertyStream::Source` subclass just calls `map_begin`, `add`, `map_end`, etc. on whatever stream it receives, and the stream decides how to materialise the output. `JsonPropertyStream` is the XRPL node's production sink: it converts those calls into a `Json::Value` tree of type `objectValue`, suitable for returning directly as an RPC response.

The sole real consumer is `doPrint()` in `src/xrpld/rpc/handlers/admin/status/Print.cpp`, which is the handler for the admin `print` command. It constructs a `JsonPropertyStream`, calls `context.app.write(stream, ...)` to traverse the application's entire `Source` hierarchy, then returns `stream.top()` — the completed JSON tree — to the caller. This makes `JsonPropertyStream` the bridge between the internal diagnostics graph and the JSON-RPC layer.

## Stack-Based Construction

The class maintains two member variables: `m_top`, a root `Json::Value` of type `objectValue`, and `m_stack`, a `vector<Json::Value*>`. The stack always holds raw pointers into the `Json::Value` tree rooted at `m_top`. Because `m_top` owns the entire tree (Json::Value uses value semantics and copies/moves children as the tree grows), pointer stability is ensured — appending a child via `top.append(...)` or `top[key] = ...` returns a reference to the child living inside `m_top`, and those child addresses remain stable for the lifetime of `m_top`. The stack thus tracks the "current insertion point" at any moment during the write traversal.

The constructor pushes `&m_top` as the initial stack entry and pre-reserves 64 slots with `m_stack.reserve(64)`. The reservation avoids reallocation during a deep traversal; if the diagnostic tree is fewer than 64 levels deep — which it always is in practice — there will be no heap allocation after construction.

## Map and Array Lifecycle

`map_begin()` and `array_begin()` each come in two overloads that reflect the two contexts a writer can be in:

- **Inside a map (keyed child):** `map_begin(key)` writes `top[key] = Json::objectValue` and pushes a pointer to that new child. `array_begin(key)` does the same with `arrayValue`.
- **Inside an array (anonymous child):** `map_begin()` calls `top.append(Json::objectValue)` and pushes a pointer to the appended element. `array_begin()` similarly appends an array.

The comments in the source make the precondition explicit (`// top is array`, `// top is a map`), but there is no runtime enforcement. Misuse — calling the key-less variant when the current context is a map, or the keyed variant inside an array — would silently produce malformed JSON. The `beast::PropertyStream` base class's RAII wrappers (`Map` and `Set`) are what enforce correct pairing: they call `map_begin`/`map_end` and `array_begin`/`array_end` symmetrically in their constructors and destructors, so misuse is prevented structurally at the call sites rather than defensively inside this class.

`map_end()` and `array_end()` are symmetric: both simply call `m_stack.pop_back()`, restoring the insertion point to the parent context. There is no type check on pop — the stack encodes context solely through the nesting depth, not through tagged pointers.

## The `add()` Overloads

Scalar insertion splits into two families: keyed (`add(key, value)`) for use inside a map, and unkeyed (`add(value)`) for use inside an array. Each numeric C++ type gets its own explicit overload, matching the virtual signatures declared in `beast::PropertyStream`. Most overloads are straightforward assignments or appends into the current top-of-stack `Json::Value`.

One notable exception is the `long` overload, in both keyed and unkeyed forms:

```cpp
void JsonPropertyStream::add(std::string const& key, long v)
{
    (*m_stack.back())[key] = Json::Value(int(v));
}
```

`long` is explicitly narrowed to `int` before being stored. This is deliberate: the underlying `Json::Value` library has no native 64-bit signed integer type in this codebase, so a `long` that is wider than 32 bits on a 64-bit platform will silently truncate. The decision accepts this limitation to keep JSON compatibility with the rest of the node's JSON handling.

## Ownership and Safety

`m_stack` holds non-owning raw pointers into `m_top`. This is safe as long as the stack is not used after `m_top` is destroyed — both are members of the same `JsonPropertyStream` object, so they share the same lifetime. No entries in `m_stack` are ever deleted individually; they are all implicitly invalidated when the `JsonPropertyStream` itself is destroyed. The class does not expose `m_stack` to external callers (it is not used after `top()` is called), so there is no practical risk.

There is no synchronization. The class is designed for single-threaded use within a single write traversal and is not safe to write to from multiple threads.