# `include/xrpl/basics/Blob.h`

`Blob.h` introduces a single named type alias used throughout the XRPL codebase for owning, mutable binary data:

```cpp
using Blob = std::vector<unsigned char>;
```

Its role is to give raw byte sequences a meaningful, searchable name rather than leaving `std::vector<unsigned char>` scattered as an anonymous type across the protocol and serialization layers. `Blob` appears as the internal storage buffer inside `Serializer` (`mData`), as the return type of serialization helpers, and in `StringUtilities` for hex encoding and SQL blob literals.

`Blob` sits at one corner of the three-type binary data model in `xrpl::basics`:

- **`Blob`** (`std::vector<unsigned char>`) — mutable, dynamically resizable, owns its memory. The right choice when data is built up incrementally, as in `Serializer`.
- **`Buffer`** — fixed-size block allocated with `unique_ptr<uint8_t[]>`, no capacity overhead, suitable when size is known upfront and resizing is not required.
- **`Slice`** — a non-owning, read-only `(pointer, length)` view. Cheap to copy and pass; `makeSlice()` factory overloads accept both `Blob` and `Buffer` seamlessly.

The choice of `unsigned char` rather than `char` is deliberate: it avoids signed/unsigned arithmetic warnings when working with raw binary values and aligns with the `uint8_t` element type used by `Slice` and `Buffer`. Because `Blob` is simply a `std::vector`, callers get the full standard iterator interface, `push_back`, `resize`, and range-insert without any additional wrapper API.