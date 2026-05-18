# LedgerEntryHelpers.h

## Role in the System

`LedgerEntryHelpers.h` is a header-only utility namespace (`xrpl::LedgerEntryHelpers`) that provides the low-level JSON parsing and validation vocabulary for the `ledger_entry` RPC command. It exists because `LedgerEntry.cpp` must dispatch across roughly twenty-five distinct ledger object types — each with its own set of required fields, type constraints, and error messages — and sharing that plumbing in a single place keeps the per-type parse functions in `LedgerEntry.cpp` readable and consistent. No other translation unit includes this header; it is private infrastructure for one consumer.

## The Two-Layer Parse Pattern

The central design is a pair of composable layers: a low-level `parse<T>()` template family and a higher-level `required<T>()` combinator.

`parse<T>()` is declared as a template but implemented only via explicit specializations for the concrete types the RPC layer cares about: `AccountID`, `uint256`, `uint192`, `std::uint32_t`, and `Asset`. Each specialization takes a single `Json::Value` and returns `std::optional<T>` — either the successfully decoded value or `std::nullopt`, with no error detail. This keeps the parsing logic pure and composable; callers that want to try-parse without an immediate error (e.g., `parseDirectoryNode` in `LedgerEntry.cpp` probing whether a field is a hash before treating it as a named owner) can call `parse<T>()` directly.

`required<T>()` wraps `parse<T>()` with presence and null checks and lifts the result into `Expected<T, Json::Value>`. This is XRPL's pre-C++23 approximation of `std::expected`, implemented over `boost::outcome`. When the field is absent or null, `required<T>()` calls `missingFieldError`; when it is present but parses as the wrong type, it calls `invalidFieldError`. The result propagates to callers via `Unexpected(result.error())` return sites in `LedgerEntry.cpp`, enabling clean early-exit error propagation without exceptions.

Concrete wrappers — `requiredAccountID`, `requiredUInt32`, `requiredUInt256`, `requiredUInt192`, `requiredAsset` — are thin shims that pass the appropriate human-readable type name string (`"AccountID"`, `"number"`, `"Hash256"`, etc.) to `required<T>()`, which feeds it into the `error_message` via `RPC::expected_field_message`. This keeps type description strings co-located with the parse logic rather than scattered across call sites.

## Error Construction

Three error builders cover all failure modes:

- `missingFieldError(field, err)` — for fields that are absent or null. The `err` parameter overrides the default `"malformedRequest"` error code string, which enables domain-specific codes like `"malformedLockingChainDoor"` when checking the bridge sub-fields.
- `invalidFieldError(err, field, type)` — for fields that are present but fail parsing. The `type` string (e.g., `"AccountID"`, `"Hash256"`) is passed to `RPC::expected_field_message` to form a human-readable `error_message`.
- `malformedError(err, message)` — for semantic failures where both the error code and the full message are caller-defined (e.g., "Cannot have a trustline to self.", "Must have exactly one of `owner` and `dir_root` fields.").

All three set `error_code` to `rpcINVALID_PARAMS` and return `Unexpected<Json::Value>`, which is immediately compatible with `Expected<T, Json::Value>` return types throughout `LedgerEntry.cpp`.

## Noteworthy Specializations

**`parse<AccountID>`** includes a `isZero()` guard after `parseBase58`. The all-zero account ID is a sentinel in the XRPL data model that cannot correspond to a real account; rejecting it here prevents it from silently producing an incorrect keylet.

**`parse<uint32_t>`** accepts both JSON integer and JSON string forms via `beast::lexicalCastChecked`. Some clients serialize numbers as strings, and this accommodates them defensively. The check `param.isInt() && param.asInt() >= 0` allows positive signed integers to be treated as unsigned, rejecting negative values cleanly.

**`parseHexBlob` / `requiredHexBlob`** are not part of the template system because they take an additional `maxLength` parameter — the credential-type field, for instance, is length-bounded. These functions decode a hex string via `strUnHex` and reject empty blobs or blobs exceeding the caller-specified maximum.

**`hasRequired`** accepts an `initializer_list<Json::StaticString>` and short-circuits on the first missing or null field, returning the corresponding `missingFieldError`. This is used as a pre-flight check in parsers that need multiple fields before attempting individual extraction — `parseAMM`, `parseRippleState`, `parseBridgeFields`, and `parseAuthorizeCredentials` in `LedgerEntry.cpp` all call it before parsing individual fields.

## `parseBridgeFields`

The most complex helper in the namespace assembles an `STXChainBridge` from a nested JSON object containing four sub-fields: `LockingChainDoor`, `LockingChainIssue`, `IssuingChainDoor`, `IssuingChainIssue`. The door fields are `AccountID`s parsed via `requiredAccountID`; the issue fields are `Issue` structs parsed via `issueFromJson`, which throws `std::runtime_error` on malformed input — caught here and converted to `invalidFieldError`. The resulting `STXChainBridge` value is returned as `Expected<STXChainBridge, Json::Value>`. This helper is reused across three distinct bridge-related object types in `LedgerEntry.cpp` (`parseBridge`, `parseXChainOwnedClaimID`, `parseXChainOwnedCreateAccountClaimID`).

## Design Rationale

Keeping all functions `inline` in a header rather than compiled into a `.cpp` file is appropriate given the single-consumer design — there is no ODR risk and the compiler sees all definitions when compiling `LedgerEntry.cpp`. The `Expected<T, Json::Value>` discipline, rather than returning sentinel values or throwing, means that every parse function in `LedgerEntry.cpp` propagates errors via the same `if (!result) return Unexpected(result.error())` idiom, making the control flow uniform and auditable across all twenty-five object types.