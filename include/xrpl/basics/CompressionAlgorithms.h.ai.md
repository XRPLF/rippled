# `CompressionAlgorithms.h` — LZ4 Block Compression Primitives

This header lives in `include/xrpl/basics/` and provides the low-level LZ4 compression and decompression routines used by the XRPL peer overlay network. It sits one abstraction layer below `src/xrpld/overlay/Compression.h`, which adds algorithm-selection logic and error suppression on top of what this file exposes.

## Architectural Role

When XRPL nodes exchange P2P messages they can optionally compress the payload before transmission. The overlay layer negotiates compression during the connection handshake and then routes compressed messages through the functions defined here. `CompressionAlgorithms.h` isolates the raw LZ4 calls — the `int`-based C API hazards, buffer management, and stream chunking — from the policy-level decisions that live in `Compression.h`.

The functions are entirely in the `xrpl::compression_algorithms` namespace. There are no classes, no state, no singletons — just three free functions.

## `lz4Compress` — Template with BufferFactory

```cpp
template <typename BufferFactory>
std::size_t lz4Compress(void const* in, std::size_t inSize, BufferFactory&& bf)
```

The design choice to accept a `BufferFactory` callable rather than returning a `std::vector` is deliberate and important. The caller knows its allocation context: in the overlay code it may be writing into a Protobuf `CodedOutputStream` region or a pooled buffer. The factory receives the worst-case compressed size from `LZ4_compressBound` and returns a raw pointer; the template accepts any callable that satisfies this contract without virtual dispatch overhead.

The sole pre-condition check guards against input larger than `UINT32_MAX`. LZ4's block API uses `int` internally, so exceeding that limit would silently truncate the size argument. The function throws via `Throw<std::runtime_error>`, which logs a call stack through `contract.h` before throwing — consistent with XRPL's "crash loudly with context" philosophy for invariant violations.

## `lz4Decompress` — Raw Buffer Overload

```cpp
inline std::size_t lz4Decompress(
    std::uint8_t const* in, std::size_t inSizeUnchecked,
    std::uint8_t* decompressed, std::size_t decompressedSizeUnchecked)
```

The `Unchecked` naming in the parameters is the code's way of signalling that the `size_t` → `int` narrowing has not yet been validated. The function immediately casts both sizes to `int` and checks for `<= 0`. This catches two distinct failure modes: a genuinely zero-length buffer, and a `size_t` value large enough that the narrowing wrap produces a non-positive `int`. Separating these checks with distinct error messages makes debugging easier.

`LZ4_decompress_safe` is used rather than the faster `LZ4_decompress_fast`. The safe variant takes the output buffer capacity as a bound and will not write past it even if the compressed data is malformed — essential when the input arrives from an untrusted peer on the network.

The function enforces an exact-size postcondition: if `LZ4_decompress_safe` returns anything other than the expected `decompressedSize` it throws. This reflects the fact that, in the overlay protocol, the original message size is transmitted in the message header; any mismatch means either corruption or a peer bug.

## `lz4Decompress` — Streaming ZeroCopyInputStream Overload

```cpp
template <typename InputStream>
std::size_t lz4Decompress(
    InputStream& in, std::size_t inSize,
    std::uint8_t* decompressed, std::size_t decompressedSize)
```

This overload works with Protobuf-style `ZeroCopyInputStream` objects that expose data as a series of chunks rather than a single contiguous buffer. The key optimization is the fast path: if the very first chunk returned by `in.Next()` is at least `inSize` bytes long, the function uses that chunk's pointer directly and avoids any allocation. In practice, compressed P2P messages typically arrive in a single TCP read buffer, so this path is taken most of the time.

When the data spans multiple chunks, the function lazily allocates a `std::vector<std::uint8_t>` of exactly `inSize` bytes (note the `compressed.resize(inSize)` is only reached on the second iteration) and copies chunks into it until the full compressed message is assembled. After reading, any bytes that were consumed from the stream beyond `inSize` are returned via `in.BackUp()`, preserving the stream cursor for the next message in the framing protocol.

The final validation before delegating to the raw overload checks that the amount actually read matches what was requested. This guards against a stream that ends early — e.g., a truncated TCP connection or a framing bug where the declared size doesn't match the available data.

## Relationship to `Compression.h`

The overlay's `Compression.h` wraps these two functions inside `compress()` and `decompress()` functions that add an `Algorithm` enum parameter (currently `Algorithm::LZ4 = 0x90` or `Algorithm::None`). Those wrappers catch all exceptions from the functions here and return `0` on failure, converting the throw-on-error contract into a return-zero-on-error contract. The distinction is intentional: the raw primitives throw so that callers who want structured error handling can use them; the overlay wrapper normalises failures to a `0` return value to simplify the state machine in the peer message processing loop.