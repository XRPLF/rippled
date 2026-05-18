# `src/libxrpl/protocol/STParsedJSON.cpp`

## Role in the System

This file is the JSON-to-protocol-object deserializer for the XRPL ledger. Every RPC call that submits a transaction, queries ledger state, or manipulates protocol data passes through this code before any validation or execution. Its job is to convert untyped `Json::Value` trees into the strongly-typed Serialized Type (`ST*`) object graph the rest of the system operates on. The public interface is deliberately simple: construct an `STParsedJSONObject` with a name and JSON, then check `object` (populated on success) or `error` (populated on failure). No exceptions escape; all errors surface as a `Json::Value` carrying RPC error codes.

## Architecture: Three Parsing Layers

All implementation lives in the anonymous `STParsedJSONDetail` namespace. The parsing logic is split into three mutually recursive functions:

**`parseLeaf()`** handles every primitive (non-container) type. It does a `switch` on `field.fieldType`, covering the full `SerializedTypeID` enum: `STI_UINT8` through `STI_UINT256`, `STI_INT32`, `STI_VL` (variable-length blobs), `STI_AMOUNT`, `STI_NUMBER`, `STI_ACCOUNT`, `STI_ISSUE`, `STI_CURRENCY`, `STI_XCHAIN_BRIDGE`, `STI_VECTOR256`, and `STI_PATHSET`. Each case returns a `std::optional<detail::STVar>`, where `nullopt` signals failure and `error` (an output parameter) carries the human-readable RPC error.

**`parseObject()`** iterates over a JSON object's member names, looks each name up in the global `SField` registry, and dispatches to `parseLeaf()` for leaf types, recursing into itself for `STI_OBJECT`/`STI_TRANSACTION`/`STI_LEDGERENTRY`/`STI_VALIDATION` children, and into `parseArray()` for `STI_ARRAY` children. After all fields are parsed, it calls `data.applyTemplateFromSField(inName)`, which retroactively enforces the `SOTemplate` associated with the field — rejecting unknown fields, catching missing required fields, and verifying that default-valued optional fields were not explicitly set. A mismatch throws `STObject::FieldErr`, caught and translated into a `template_mismatch` RPC error.

**`parseArray()`** handles `STArray` values. A critical design constraint here: each element in a JSON array encoding an `STArray` must be a JSON object with exactly one key. This enforces the XRPL canonical convention where array elements are tagged with their field name, e.g., `[{"Memo": {...}}, {"Memo": {...}}]`. Null or multi-keyed elements are rejected as `singleton_expected`. Each element's inner object is parsed via `parseObject()`, and after parsing, the result must have a field type of `STI_OBJECT`; any other type is rejected as `non_object_in_array`.

## Non-Obvious Type-Specific Behaviors

Several leaf cases contain special-casing that encodes XRPL protocol knowledge:

`parseUint16` (called for `STI_UINT16`) recognizes `sfTransactionType` and `sfLedgerEntryType` fields and accepts human-readable names like `"Payment"` or `"Offer"` in addition to numeric values. These names are resolved through the `TxFormats` and `LedgerFormats` singleton registries. Critically, when parsing at the top level with `sfGeneric` as the template sentinel, this function also upgrades `name` to `sfTransaction` or `sfLedgerEntry` so that the subsequent `applyTemplateFromSField()` call enforces the correct field schema for the specific transaction or ledger entry type.

`parseUint32` (called for `STI_UINT32`) recognizes `sfPermissionValue` and translates granular permission names or transaction-type names into their numeric representations via `Permission::getInstance()`.

`STI_UINT8` contains special handling for `sfTransactionResult`, accepting TER result code strings (e.g. `"tesSUCCESS"`, `"tecPATH_DRY"`) by calling `transCode()` and then `TERtoInt()`, including a range check to ensure the code fits in a `uint8_t`.

`STI_UINT64` uses `std::from_chars` for hexadecimal parsing by default, but checks `field.shouldMeta(SField::sMD_BaseTen)` to switch to base-10 parsing for fields whose metadata marks them as decimal amounts.

`STI_PATHSET` contains the most complex leaf logic. It handles both traditional IOU paths (with `account`, `currency`, `issuer`) and MPT (Multi-Purpose Token) paths (with `mpt_issuance_id`). When both `currency` and `mpt_issuance_id` are present simultaneously, the parse is rejected. For MPT paths, it also validates that the issuer field, if present, matches the issuer embedded in the MPTID itself.

## Safety and Defensive Coding

A `maxDepth = 64` constant guards both `parseObject()` and `parseArray()`. Any JSON structure nesting deeper than 64 levels is rejected as `too_deep`, preventing runaway recursion from crafted inputs. This is the JSON counterpart to the binary deserialization depth guard in `STArray` (which uses a maximum of 10 at the binary level).

The `to_unsigned<U, S>` and `to_unsigned<U1, U2>` template pair in `STParsedJSONDetail` are SFINAE-constrained to signed→unsigned and unsigned→unsigned conversions respectively. Each variant throws `std::runtime_error` on range violation. This eliminates silent truncation: if JSON delivers `-1` for a `uint32_t` field, the parse fails with an explicit `invalid_data` error rather than wrapping to `UINT32_MAX`.

The `static_assert(std::is_same_v<decltype(value.asInt()), std::int32_t>)` inside the `STI_INT32` case is a forward-compatibility guard: if the upstream JSON library ever widens its `asInt()` return type, the compiler will signal that the bounds checking logic needs revisiting.

## Error Reporting Strategy

Error handling uses a hybrid model. Internally, parsing functions throw standard exceptions on individual conversion failures (via `beast::lexicalCastThrow`, `Throw<std::runtime_error>`, etc.), and each caller wraps the failure in a `catch (std::exception const&)` block that records a specific RPC error in the `error` output parameter and returns `nullopt`. This keeps the control flow clean — an exception in a deeply nested helper propagates to the nearest catch, which converts it to a structured error without needing to thread error codes through every intermediate return.

Error messages are path-qualified using `make_name()`, which concatenates `object.field` strings. As parsing recurses, each level prepends its own JSON path component, so a deeply nested error like `"Field 'tx_json.Memos.[0].Memo.MemoData' has bad type"` is reported with the full path to the offending field.

## Integration with `detail::STVar`

`parseLeaf()` returns `std::optional<detail::STVar>`, not `std::optional<STBase>`. `STVar` is a type-erased "variant" container with a small-object optimization: types of 72 bytes or fewer are stored inline in aligned storage, avoiding heap allocation for common small types like `STUInt32` or `STAccount`. Each successful leaf parse calls `detail::make_stvar<STResult>(...)` to construct the appropriately typed `ST*` object inside the `STVar`. The parent `STObject` accumulates these via `emplace_back`, which takes ownership of the `STVar`.

## Usage Context

`STParsedJSONObject` is constructed in `TransactionSign.cpp` whenever an RPC call provides a `tx_json` field to be signed or submitted. The caller checks `parsed.object.has_value()` and if true promotes the result to `STTx` for further processing; if false, `parsed.error` is forwarded directly to the RPC response. The same pattern appears in the `Simulate` RPC handler and in the `jtx` testing framework, making `STParsedJSONObject` the universal gateway between untyped JSON and the typed protocol object graph.