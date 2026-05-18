# `STBlob` — Variable-Length Binary Field for XRPL Serialized Types

## Role in the System

`STBlob` is the serialized-type representation of an opaque, variable-length byte string within the XRPL binary protocol. It serves as the concrete type for any ledger or transaction field whose wire format is declared as `STI_VL` (variable-length), and — notably — also for `STI_ACCOUNT` fields (20-byte account IDs), both of which are serialized as length-prefixed blobs. It lives in the `include/xrpl/protocol/` layer alongside its sibling types (`STAmount`, `STArray`, `STObject`, etc.), all sharing the `STBase` contract for named, serializable fields.

## Class Hierarchy and Responsibilities

`STBlob` multiply-inherits from `STBase` and `CountedObject<STBlob>`.

`STBase` provides the field-naming system (`SField`) and the virtual interface — `getSType()`, `getText()`, `add()`, `isEquivalent()`, `isDefault()`, `copy()`, `move()` — that the XRPL serialization engine calls polymorphically. Every `STBase` carries an `SField const*` that identifies which ledger field this instance represents (e.g., `sfSignature`, `sfAccount`). The class comment in `STBase.h` warns explicitly against using `STBase`-derived objects in standard containers due to copy-assignment semantics that deliberately do not propagate field names — only values — which is used by the transaction engine to "slide" field values during object mutation.

`CountedObject<STBlob>` adds zero-overhead live-instance counting via a static atomic counter registered in a global lock-free linked list. This enables diagnostics via `CountedObjects::getInstance().getCounts(threshold)` without impacting hot paths.

## Storage Model: Owning Buffer, Non-Owning View

The sole data member is `Buffer value_`, a heap-owning wrapper around `unique_ptr<uint8_t[]>`. The `Buffer` class provides value semantics with explicit copy and move, and an implicit conversion to `Slice`.

`STBlob`'s public type alias `using value_type = Slice` and the `value()` accessor expose the contents as a `Slice` — a lightweight, non-owning `{pointer, size}` pair. This is the canonical read-only access pattern: callers inspect data through a cheap `Slice` without triggering any allocation or copy. Mutation must go through one of the two assignment operators: `operator=(Slice const&)` allocates a fresh `Buffer` and copies the bytes in, while `operator=(Buffer&&)` and `setValue(Buffer&&)` transfer ownership in O(1) via move. This duality makes the ownership model explicit at the call site.

## Constructors and Deserialization

Four construction paths exist:

- **From raw memory**: `STBlob(SField const&, void const*, std::size_t)` copies bytes into an owned `Buffer`. Used when constructing blobs from already-decoded data.
- **From a moved `Buffer`**: `STBlob(SField const&, Buffer&&)` takes ownership without copying, preferred for performance-sensitive construction.
- **Empty blob**: `STBlob(SField const&)` creates a zero-length blob; `isDefault()` returns `true` in this state (the buffer is empty).
- **From a `SerialIter`**: The deserialization constructor `STBlob(SerialIter&, SField const&)` (defined in `STBlob.cpp`) calls `st.getVLBuffer()`, which reads the VL-prefix and returns a `Buffer` filled with the wire bytes. This is how blobs are reconstituted from a raw ledger stream.

## Serialization and Wire Format

`getSType()` returns `STI_VL`, the XRPL type tag for variable-length fields. `add(Serializer& s)` calls `s.addVL(value_.data(), value_.size())`, which writes the standard XRPL VL prefix followed by the raw bytes. The method asserts two invariants: the field must be declared binary (`getFName().isBinary()`), and the field's type must be either `STI_VL` or `STI_ACCOUNT`. The second assertion is the key design detail — account IDs on the XRPL wire are 20-byte opaque blobs, so `STBlob` serves double duty as the backing store for account fields. The VL encoding handles both cases uniformly.

`getText()` returns the contents as an uppercase hex string via `strHex()`, used for human-readable logging and JSON output.

`isEquivalent()` performs a `dynamic_cast` to confirm the other object is also an `STBlob`, then delegates to `Buffer::operator==` which does a `memcmp`. It does not compare field names — only content — consistent with the `STBase` contract.

## In-Place Copy and Move for `STVar`

The private `copy(std::size_t n, void* buf) const` and `move(std::size_t n, void* buf)` overrides delegate to `STBase::emplace()`, which does placement-new into `buf` if `sizeof(STBlob) <= n`, otherwise falls back to heap allocation. This is how `detail::STVar` — the variant-like field container used inside `STObject` — implements a small-buffer optimization: small serialized types are stored inline in a fixed-size buffer, avoiding a separate heap allocation per field. `detail::STVar` is declared a `friend` in both `STBase` and `STBlob` to access these private factory methods.

## Design Tradeoffs

The split between `Buffer` (owning) and `Slice` (non-owning) could have been collapsed into a single interface, but keeping them separate enforces at the type level that callers who hold a `Slice` have no ownership claim. The choice to expose `value_type = Slice` rather than `const Buffer&` is deliberate: it prevents callers from taking a mutable reference to the underlying storage and sidesteps object lifetime confusion when the `STBlob` is moved.

The `setValue(Buffer&&)` method is redundant with `operator=(Buffer&&)` but exists as an explicit named setter for code sites where the intent of "set the content" is more readable than an assignment expression.