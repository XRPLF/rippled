# `MPTIssue.cpp` — MPT Issuance Identity and Serialization

## Role in the System

`MPTIssue.cpp` implements the `MPTIssue` class, which wraps a single `MPTID` value to represent a Multi-Purpose Token (MPT) issuance on the XRP Ledger. Its purpose is to adapt the raw 192-bit `MPTID` type into an object with the same interface contract as the existing `Issue` class (which represents XRP or IOU currencies). This interface parity is the key architectural motivation: by sharing methods like `getIssuer()`, `native()`, `integral()`, `getText()`, and `setJson()`, `MPTIssue` can participate in the `Asset` variant and other static-polymorphism patterns throughout the ledger engine without requiring a rewrite of amount-handling code.

## The `MPTID` Encoding

`MPTID` is defined in `UintTypes.h` as `base_uint<192>` — a 192-bit fixed-width integer. Its binary layout is a direct concatenation of a 32-bit account sequence number followed by the 160-bit `AccountID` of the issuer. This encoding is not incidental; `Indexes.h` confirms it with the comment *"MPTID is a 192-bit concatenation of a 32-bit account sequence and a 160-bit account id."* The encoding lets a single opaque identifier carry both pieces of information needed to uniquely identify an issuance without any additional lookup.

## Constructors

Two constructors are provided. The primary one takes a pre-formed `MPTID` directly. The convenience overload accepts a `uint32_t sequence` and an `AccountID`, delegating to `xrpl::makeMptID(sequence, account)` (declared in `Indexes.h`) to perform the packing. The two-argument form exists so callers with the raw components don't have to manually invoke `makeMptID` before constructing the object.

## Issuer Extraction via `reinterpret_cast`

`getIssuer()` is the most low-level method in this file. It must recover the `AccountID` from the back 20 bytes of the packed `MPTID`. It does this with a `reinterpret_cast<AccountID const*>(mptID_.data() + sizeof(std::uint32_t))` and immediately dereferences the resulting pointer. This is exactly the kind of pointer aliasing that invites undefined behavior in C++ — but it is explicitly guarded by a compile-time `static_assert` that confirms `sizeof(MPTID) == sizeof(uint32_t) + sizeof(AccountID)`. The assert is both documentation and a guarantee: if the layout ever changes (e.g., padding is introduced), the build fails rather than silently reading the wrong bytes at runtime.

It is worth noting that the header defines a standalone free function `getMPTIssuer()` which uses `std::copy_n` followed by `std::bit_cast<AccountID>` to achieve the same extraction. The `bit_cast` approach is stricter (it respects object-model rules, is constexpr-eligible) while the `reinterpret_cast` in the member method is older style but equally correct given the same `static_assert` guard.

## JSON Serialization and Deserialization

Serialization is straightforward: `setJson()` writes a single key `mpt_issuance_id` whose value is the hex string form of the `MPTID`. The standalone `to_json()` function constructs a fresh `Json::Value` object and delegates to `setJson()`. This split — a mutating `setJson` that writes into a caller-supplied object, plus a `to_json` wrapper that owns the object — lets higher-level code merge the MPT representation into a larger JSON structure without an intermediate copy.

`mptIssueFromJson()` performs defensive parsing in a specific order. First it verifies the input is a `Json::Value` of object type, throwing `std::runtime_error` otherwise. Then it checks that neither `currency` nor `issuer` fields are present — a deliberate exclusion that guards against callers accidentally passing an IOU-style amount object instead of an MPT object. This is important because both types appear in quantity specifications throughout the protocol's JSON API, and the distinction matters. The third check ensures `mpt_issuance_id` is present and is a string type. Finally, `MPTID::parseHex()` validates that the string is a well-formed 192-bit hex encoding.

Two distinct exception types are used: `std::runtime_error` covers structural problems (wrong JSON type, forbidden fields), while `Json::error` is reserved for field-level format failures (wrong value type, invalid hex). This separation lets callers that need to distinguish parse errors from protocol misuse catch at the right level.

## Interface Contracts for Polymorphism

`native()` always returns `false` and `integral()` always returns `true`. These are not trivial stubs — they carry semantic meaning that drives behavior elsewhere. `native()` returning `false` distinguishes MPT amounts from XRP in generic code. `integral()` returning `true` signals that MPT quantities are whole-number values (no fractional units), which affects amount arithmetic and display in the `STAmount` system. The implicit conversion `operator MPTID const&()` allows an `MPTIssue` to be passed wherever a raw `MPTID` is expected, reducing friction at call sites that deal with the underlying identifier directly.