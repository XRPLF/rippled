#pragma once

#include <rust/cxx.h>

#include <cstdint>

namespace xrpl {
// `xrpl::HostFunctions` is forward-declared rather than included: this header is
// `include!()`d by the cxxbridge-generated translation unit, whose target gets only the
// project's `include/` directory - not the Boost paths that HostFunc.h -> Slice.h ->
// strHex.h transitively need. A reference member and declarations alone do not require a
// complete type; HostContext.cpp, compiled into libxrpl, includes the real header.
class HostFunctions;

// The host handed to the Rust wasm engine: one method per entry in the wasm host ABI,
// each forwarding to `xrpl::HostFunctions` - the single source of truth for ledger
// access - and lowering its typed `std::expected` result onto the ABI's wire form.
//
// Every method is `noexcept`, and every body catches everything: a C++ exception
// unwinding into the Rust frames that called it would be undefined behaviour, so a
// failure leaves here as -1, which the engine reads as a fatal error and reports as
// `tecINTERNAL`.
//
// Not an owner: it borrows `hf` for the length of one run. Declared `struct` because the
// Rust side only ever sees an opaque pointer.
class HostContext
{
    // Non-const so a host function that mutates (`cacheLedgerObj`, `updateData`) can be
    // reached from the `const` methods below: constness of the reference is not
    // constness of the referent.
    HostFunctions& hostFunctions_;

public:
    HostContext(HostFunctions& hostFunctions);

    // A byte-producing call is handed `out` - a slice aliasing either guest linear
    // memory or the engine's output buffer - writes the value only if the whole of it
    // fits, and returns the value's *true* length, which may exceed `out`. That is how a
    // guest learns the size to ask for, and it is why these methods never need to know
    // the guest's capacity: the engine owns the buffer-fit, field-cap and transfer-budget
    // rules and derives all three from the length returned here.
    //
    // A negative return is a `HostFunctionError` code.
    [[nodiscard]] std::int32_t
    getLedgerSqn(rust::Slice<std::uint8_t> out) const noexcept;

    [[nodiscard]] std::int32_t
    getParentLedgerTime(rust::Slice<std::uint8_t> out) const noexcept;

    [[nodiscard]] std::int32_t
    getParentLedgerHash(rust::Slice<std::uint8_t> out) const noexcept;

    [[nodiscard]] std::int32_t
    getBaseFee(rust::Slice<std::uint8_t> out) const noexcept;

    // The amendment is either a 32-byte id or a name; a 32-byte input is tried as an
    // id first and falls back to a name lookup. Answers 1 or 0, or a negative
    // `HostFunctionError` code.
    [[nodiscard]] std::int32_t
    isAmendmentEnabled(rust::Slice<std::uint8_t const> amendment) const noexcept;

    // The object id must be a 32-byte uint256, else `InvalidParams`. `cacheIdx` selects
    // the slot (0 = pick a free one). Answers the slot used, or a negative
    // `HostFunctionError` code.
    [[nodiscard]] std::int32_t
    cacheLedgerObj(rust::Slice<std::uint8_t const> objId, std::int32_t cacheIdx) const noexcept;

    [[nodiscard]] std::int32_t
    getTxField(std::int32_t field, rust::Slice<std::uint8_t> out) const noexcept;

    [[nodiscard]] std::int32_t
    getCurrentLedgerObjField(std::int32_t field, rust::Slice<std::uint8_t> out) const noexcept;

    [[nodiscard]] std::int32_t
    getLedgerObjField(std::int32_t cacheIdx, std::int32_t field, rust::Slice<std::uint8_t> out)
        const noexcept;

    // The locator is a path of little-endian i32 steps, so its byte length must be a
    // non-zero multiple of 4, else `LocatorMalformed`.
    [[nodiscard]] std::int32_t
    getTxNestedField(rust::Slice<std::uint8_t const> locator, rust::Slice<std::uint8_t> out)
        const noexcept;

    [[nodiscard]] std::int32_t
    getCurrentLedgerObjNestedField(
        rust::Slice<std::uint8_t const> locator,
        rust::Slice<std::uint8_t> out) const noexcept;

    [[nodiscard]] std::int32_t
    getLedgerObjNestedField(
        std::int32_t cacheIdx,
        rust::Slice<std::uint8_t const> locator,
        rust::Slice<std::uint8_t> out) const noexcept;

    // Answers the array's element count directly, or a negative `HostFunctionError`
    // code (`NoArray` if the field is not an array).
    [[nodiscard]] std::int32_t
    getTxArrayLen(std::int32_t field) const noexcept;

    [[nodiscard]] std::int32_t
    sha512Half(rust::Slice<std::uint8_t const> data, rust::Slice<std::uint8_t> out) const noexcept;

    // A call with no value to report answers 0, or a negative `HostFunctionError` code.
    [[nodiscard]] std::int32_t
    trace(rust::Str msg, rust::Slice<std::uint8_t const> data, bool asHex) const noexcept;

    [[nodiscard]] std::int32_t
    traceNum(rust::Str msg, std::int64_t number) const noexcept;
};

}  // namespace xrpl
