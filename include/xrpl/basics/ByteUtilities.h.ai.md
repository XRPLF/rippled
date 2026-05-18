# `ByteUtilities.h` — Compile-Time Byte-Size Helpers

`ByteUtilities.h` is a minimal, header-only utility in `xrpl/basics` that provides two `constexpr` template functions — `kilobytes()` and `megabytes()` — for expressing byte-count constants in human-readable units at compile time. The file exists purely to eliminate magic numbers from sites that configure buffer sizes, memory limits, and slab allocator parameters throughout the XRPL codebase.

## The Functions

`kilobytes(value)` multiplies its argument by 1024. `megabytes(value)` composes that twice — it calls `kilobytes(kilobytes(value))` — which gives the correct factor of 1,048,576 (2²⁰) without any separate literal. Both functions are templated on `T`, so they work with any integral or arithmetic type and return the same type that the arithmetic produces, letting the caller's type context drive the result without an explicit cast. Both are `constexpr` and `noexcept`, meaning the computation happens entirely at compile time and has no runtime overhead whatsoever.

The `static_assert` lines immediately below the definitions act as inline tests: they verify `kilobytes(2) == 2048` and `megabytes(3) == 3145728` during every compilation, preventing any silent regression if the implementation were ever accidentally changed.

## Design Rationale

The template design over a fixed `size_t` signature is deliberate. Call sites like `megabytes(std::size_t(60))` in `SHAMapItem.h` need to produce `std::size_t` results for slab allocator configuration, while other uses such as `megabytes(256)` in `RPCCall.cpp` are happy with `int`-width results for comparison. By letting `auto` return the natural result of the arithmetic, the functions avoid both unwanted narrowing conversions and unwanted widening that could paper over a type mismatch.

The composition `kilobytes(kilobytes(value))` for megabytes is a small but telling choice: it reuses the already-tested primitive rather than independently writing `value * 1024 * 1024`, keeping the chain of trust short and making the relationship between units self-documenting.

## Usage Across the Codebase

The functions appear at exactly the kinds of boundaries where misreading a magnitude would have serious consequences:

- **Overlay message cap**: `src/xrpld/overlay/Message.h` defines `constexpr std::size_t maximumMessageSize = megabytes(64)`, bounding the maximum peer-to-peer message size to 64 MiB.
- **RPC reply limit**: `src/xrpld/rpc/detail/RPCCall.cpp` defines `constexpr auto RPC_REPLY_MAX_BYTES = megabytes(256)` to guard against unbounded JSON responses.
- **Ledger and open-view buffers**: `include/xrpl/ledger/OpenView.h` and `include/xrpl/ledger/detail/RawStateTable.h` both set `initialBufferSize = kilobytes(256)` for their serialisation scratch buffers.
- **ShaMap slab allocator**: `SHAMapItem.h` uses `megabytes()` to express the per-size-class allocation limits for the slab allocator pools (60 MB, 46 MB, etc.), and `TaggedPointer.ipp` uses `kilobytes(512)` for the slab block granularity.

The consistent use of these helpers rather than raw literals means that anyone reading any of those files immediately understands the intended scale without mental arithmetic, and the compiler catches any integer overflow that a bare literal might hide at the point of definition.