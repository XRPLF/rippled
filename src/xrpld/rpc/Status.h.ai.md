# `Status.h` — Unified Error Result Type for the XRPL RPC Layer

## Role and Motivation

The XRPL codebase has two established error representation systems that predate any unified approach: `TER` (Transaction Engine Result), which flows through the transaction processing pipeline, and `error_code_i`, the RPC-layer error enumeration defined in `<xrpl/protocol/ErrorCodes.h>`. RPC handlers must frequently work with both — validating a transaction produces a `TER`, while reporting protocol-level problems produces an `error_code_i`. Without a unifying type, return paths become a tangle of conditionals and separate type conversions at every call site.

`Status` solves this by acting as a tagged union over these two legacy code spaces, plus a raw integer fallback, while exposing a single interface for the rest of the RPC infrastructure to consume. It also carries an optional list of freeform message strings, allowing callers to attach diagnostic context that neither `TER` nor `error_code_i` can express on their own. In practice, handlers like `doCommand()` and `callMethod()` in `RPCHandler.cpp` return `Status` directly, and `AccountTx.cpp` uses `std::variant<LedgerRange, RPC::Status>` to propagate errors through multi-step ledger range lookups.

## Struct Design

`Status` inherits from `std::exception`, signaling intent to support throw/catch flows even though the dominant usage pattern in the codebase is value-based error propagation. The `Type` enum (`none`, `TER`, `error_code_i`) serves as the discriminant tag for the stored `code_` integer. The raw `code_` is always stored as a plain `int`: `TERtoInt` converts a `TER` on construction, and `TER::fromInt` recovers it in `toTER()`.

The `OK` sentinel is `0`. This is coherent because both `TER` (`tesSUCCESS == 0`) and `error_code_i` (`rpcSUCCESS == 0`) use zero to mean success. As a consequence, `operator bool()` — returning `code_ != OK` — reads naturally as "if something went wrong" and works consistently regardless of which code space the `Status` was constructed from. The complementary `operator!()` lets call sites write `if (!status)` to test for clean success.

## Constructor Design and the `enable_if` Guard

There are four constructors. The template constructor accepting any integral type uses `std::enable_if_t<std::is_integral<T>::value>` to deliberately exclude enum types. This prevents silent narrowing: if an `error_code_i` enum value accidentally bound to the integer template path, `type_` would remain `Type::none` instead of `Type::error_code_i`, causing `inject()` and `toErrorCode()` to silently misbehave. The two explicit enum constructors — one for `TER`, one for `error_code_i` — each set `type_` correctly, so the guard enforces that only truly untyped integer codes (raw internal status values carrying no TER or RPC semantic) go through the generic path.

There is also a convenience constructor `Status(error_code_i, std::string const&)` that accepts a single string rather than a `std::vector<std::string>`, matching the common single-message case seen throughout handler code such as `RPC::Status{rpcINVALID_PARAMS, "ledgerHashMalformed"}`.

## `inject()` and the JSON Bridge

The `inject()` method is the primary bridge to the JSON response layer. It calls `toErrorCode()` and, if the result is non-zero, delegates to `inject_error()` from `<xrpl/protocol/ErrorCodes.h>`. That function populates the JSON object with `jss::error` (a human-readable token like `"invalidParams"`), an error code integer, and an error message from the `ErrorInfo` table. If the `Status` carries attached messages, the first one is forwarded as a supplemental message string, overriding the default message from `ErrorInfo`.

This design keeps JSON serialization deferred to output time rather than embedded in construction, which matters because not every `Status` ends up in a JSON response — some are used purely for internal control flow or later translated to other transport formats.

The `fillJson()` method is explicitly noted in the header as "not currently used." Its presence documents intent to support a fully spec-compliant JSON-RPC 2.0 error object (per the specification at `jsonrpc.org/specification#error_object`), distinct from the current ad-hoc `inject()` approach.

## Precondition Assertions on `toTER()` and `toErrorCode()`

Both downcast methods are guarded by `XRPL_ASSERT` on the `type_` tag. Calling `toTER()` on a `Status` whose type is `error_code_i` would produce a nonsensical `TER` value, and silently doing so could corrupt transaction result semantics. The assertions make this a detectable programming error rather than silent data corruption. Callers are expected to check `type()` before invoking either method — the pattern in `AccountTx.cpp` is representative: `error.toErrorCode() != rpcSUCCESS` guards the path before `error.inject(response)` is called.

## Relationship to Sibling Files

`RPCHandler.cpp` uses `Status` as the direct return type of `doCommand()` and `callMethod()`, making it the canonical result type of the entire handler dispatch loop. `ErrorCodes.h` supplies the `error_code_i` enumeration and the `inject_error()` and `ErrorInfo` machinery that `Status::inject()` delegates to. `TER.h` supplies the transaction engine result type that `Status::toTER()` recovers. Together, these three files define the full error vocabulary of the RPC subsystem — `Status.h` exists specifically as the adapter layer that lets handler code speak in all three dialects through one type.