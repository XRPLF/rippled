## CTID.h — Concise Transaction ID Encoding/Decoding

This header implements the **Concise Transaction ID (CTID)** specification from [XLS-15d](https://github.com/XRPLF/XRPL-Standards/discussions/34), a compact encoding that uniquely identifies a transaction by its position in a ledger *on a specific network*. A bare transaction hash says nothing about which network it appeared on or where within a ledger it lives — CTID packs all three coordinates (ledger sequence, transaction index, network ID) into a single 16-character hex string that is easier to communicate and validate than a 64-character hash.

### Binary Layout

The CTID is a 64-bit value formatted as 16 uppercase hex digits:

```
[4 bits: 0xC] [28 bits: ledgerSeq] [16 bits: txnIndex] [16 bits: networkID]
```

The top nibble is always `C` (binary `1100`), acting as a magic prefix that distinguishes a CTID from arbitrary hex strings and enables fast structural validation. During encoding the constant `0xC000'0000ULL` is added to the ledger sequence before shifting it into the high 32 bits. Any valid CTID therefore begins with `C`, so decoders can immediately reject non-CTID values by masking with `0xF000'0000'0000'0000` and comparing against `0xC000'0000'0000'0000`. The practical capacity is ~268 million ledgers, 65,536 transactions per ledger, and 65,536 distinct networks.

### `encodeCTID`

`encodeCTID` is `inline` and `noexcept`, returning `std::optional<std::string>` to signal failure without exceptions. Range checks are explicit upfront: ledger sequence is capped at 28 bits (`0x0FFF'FFFF`), while `txnIndex` and `networkID` are each bounded to 16 bits. Any out-of-range input returns `std::nullopt` immediately — the right choice over silent truncation, which would silently produce a CTID pointing to the wrong transaction.

The output uses `std::stringstream` with `std::hex`, `std::uppercase`, `std::setw(16)`, and `std::setfill('0')` to guarantee exactly 16 characters regardless of leading zeros. This zero-padding is load-bearing: the decoder validates a fixed 16-character length as its first step.

### `decodeCTID`

The template design avoids separate overloads for each string-like type. A single function body uses `if constexpr` to dispatch between string types (`std::string`, `std::string_view`, `char*`, `const char*`) and integral types. Unsupported types fall through to `return std::nullopt`, making the template safe to instantiate with unexpected types without a compile error.

For string inputs, two guards fire before any numeric conversion: a length check (exactly 16 characters) and a `boost::regex_match` against `^[0-9A-Fa-f]{16}$`. The `std::stoull` call that follows is wrapped in a `try/catch` block annotated with `LCOV_EXCL_START/STOP` — an explicit acknowledgment that this exception path is theoretically unreachable given the prior validation, but guarded anyway for defensive correctness. Using `boost::regex` rather than `std::regex` is consistent with the broader XRPL codebase and avoids the historically poor performance of several `std::regex` implementations. The `<regex>` header included at the top is not used directly and appears to be a vestige.

For integral inputs the cast to `uint64_t` is direct, and the same prefix mask check applies. This centralizes the structural invariant after type-specific parsing rather than duplicating it per branch.

The return type `std::optional<std::tuple<uint32_t, uint16_t, uint16_t>>` reflects actual bit widths precisely: ledger sequence returns as `uint32_t` (28 bits fit) while `txnIndex` and `networkID` return as `uint16_t`, enabling compile-time range reasoning by callers.

### Call Sites

In `Tx.cpp` (the `tx` RPC handler), `encodeCTID` is called on outbound transaction responses once the transaction is confirmed in a validated ledger — reading ledger sequence, transaction index from `sfTransactionIndex`, and the node's network ID from the application-level `NetworkIDService`. The result populates the `ctid` field in the JSON response. On inbound requests, `decodeCTID` parses a caller-supplied `ctid` parameter to extract the ledger and index needed for lookup, after first verifying the decoded network ID matches the local node's ID (to reject CTIDs from other networks).

In `Transaction.cpp` (the `Transaction` class's `getJson` path), CTID encoding similarly gates on having a validated ledger index and a transaction sequence. Notably, if the serialized transaction carries an `sfNetworkID` field, that value overrides the local node's network ID when computing the CTID — supporting cross-network transaction objects that carry their origin network identity inline.

### Design Characteristics

The header is fully self-contained: its only dependencies are `boost/regex.hpp`, `<optional>`, and `<sstream>`. It carries no ledger, session, or application state, making it safe to include from any layer without dragging in heavy subsystem headers. Both functions are `inline` header-only implementations, appropriate given their small size and the template requirement for `decodeCTID`. The `noexcept` specification is honest: both functions guarantee no exceptions will escape, handling all failure modes through `std::nullopt` returns.