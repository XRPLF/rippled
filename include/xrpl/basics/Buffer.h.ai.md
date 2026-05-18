# `include/xrpl/basics/Buffer.h`

## Role in the System

`Buffer` is the XRPL codebase's canonical owning byte container. It occupies a distinct position alongside two other byte-handling types: `Slice`, which is a non-owning, immutable view into existing memory, and `Blob` (a typedef for `std::vector<unsigned char>`), which is a general-purpose growable sequence. `Buffer` fills the gap between these: it owns its memory exclusively, is mutable, but makes no provision for incremental growth. When you need to allocate a block of bytes, write into it once, and pass it around by move, `Buffer` is the right tool.

The class also satisfies an informal `BufferFactory` concept used by compression utilities — a callable that accepts a size and returns a `void*` to writable memory. This dual role as both a container and an allocator-callback is the most distinctive design choice in the file.

## Ownership and Internal Layout

The backing store is a `std::unique_ptr<std::uint8_t[]>`, giving the class clear exclusive ownership with automatic deallocation. The invariant enforced throughout is that an empty buffer (`size_ == 0`) always holds a null pointer — never a zero-byte allocation. This is visible in the size constructor: `new std::uint8_t[size]` is called only when `size` is non-zero, and `alloc()` resets to `nullptr` if `n == 0`. The test suite verifies this invariant explicitly via its `sane()` helper, which asserts `data() == nullptr` iff `empty()`. Treating null as the canonical empty state avoids any ambiguity at the call site and makes zero-initialization checks safe without checking both pointer and size.

## The `alloc()` Pattern — Discard, Don't Resize

The central API difference from `std::vector` is `alloc(std::size_t n)`, which reallocates the buffer to exactly `n` bytes and discards any existing content. Unlike `vector::resize()`, there is no attempt to preserve data. This is intentional: the primary workload for `Buffer` is receiving output from operations like decompression, where the caller pre-computes the required size and wants a fresh block to write into. Reallocation is skipped entirely if the requested size equals the current size, avoiding a pointless free/alloc cycle when the same `Buffer` is reused across calls of equal output length.

The `operator()(std::size_t n)` overload simply delegates to `alloc()` and returns a `void*`, satisfying the `BufferFactory` concept expected by `lz4Compress` in `CompressionAlgorithms.h`. That template function calls `bf(outCapacity)` to obtain the destination buffer — passing a `Buffer` object directly fills both roles (allocation and storage) in a single object.

## Slice Integration

`Buffer` is tightly coupled to `Slice`. It provides an implicit conversion `operator Slice() const noexcept`, so any `Buffer` can be passed wherever a `Slice` is expected without an explicit cast. The reverse — constructing a `Buffer` from a `Slice` — is marked `explicit`, preventing accidental copies of view-only data.

The `operator=(Slice)` assignment requires particular attention: before copying, it checks via `XRPL_ASSERT` that the source slice does not overlap with the `Buffer`'s own storage. The danger is that `alloc()` frees the old memory first, and if the incoming `Slice` pointed into that memory, the subsequent `memcpy` would be a use-after-free. The assertion guards against this specific self-overlapping scenario. Note that `operator=(Buffer const&)` uses a different path through `alloc()` + `memcpy`, which naturally handles self-assignment because `alloc()` is a no-op when sizes match — the existing pointer is reused and then `memcpy`-d over itself (which is defined behavior for `memcpy` with identical source and destination).

## Move Semantics

Both move constructor and move assignment are `noexcept`, a static guarantee the test suite verifies with `static_assert`. This ensures `Buffer` can be held in standard containers like `std::vector` without triggering copies on reallocation. After a move, the source is left in a valid empty state: `p_` is null (via `unique_ptr` move semantics) and `size_` is explicitly reset to zero.

## Comparison and Iteration

Equality comparison is implemented as a free function using `std::memcmp` after a size check. The class exposes only `const_iterator` (raw `uint8_t const*` pointers), meaning range-for loops and standard algorithms can consume the buffer's contents read-only. Mutable iteration is available only through `data()`, keeping the interface honest about the distinction between reading and writing into the buffer.

## Contrast with `Blob`

`Blob` (`std::vector<unsigned char>`) is still used extensively in the codebase for cases where the byte sequence grows incrementally, such as serialization output. `Buffer` is preferred when the size is known upfront, ownership transfer by move is the primary operation, or the `BufferFactory` pattern is required — for example, storing the output of an LZ4 decompression call without needing the capacity/size distinction that `vector` maintains internally.