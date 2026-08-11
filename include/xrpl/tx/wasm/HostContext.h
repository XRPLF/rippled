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

// Defined by the cxx bridge, which emits it into `xrpl_wasm_vm_ffi_cxxbridge/lib.h` from the
// declaration in `crates/xrpl-wasm-vm-ffi` - so the data types and their wire values are
// written once, in Rust, rather than kept in step with a copy here.
//
// Forward-declared for the reason `HostFunctions` above is: that generated header includes
// this one, so naming its definition here would be circular. A scoped enum with a fixed
// underlying type needs no definition to appear in a signature; `HostContext.cpp` includes
// the generated header for the `switch`.
enum class TraceDataType : std::int32_t;

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
    getCurrentLedgerObjField(std::int32_t field, rust::Slice<std::uint8_t> out) const noexcept;

    [[nodiscard]] std::int32_t
    sha512Half(rust::Slice<std::uint8_t const> data, rust::Slice<std::uint8_t> out) const noexcept;

    // Renders `data` as `dataType` says, and hands the text to `HostFunctions::trace`, which
    // is what puts it in this node's log.
    //
    // The one call that answers nothing: the guest's wasm function has no result, and this
    // node's own log is the only thing a trace touches, so a buffer that does not hold what
    // it claims is logged here and dropped rather than reported to a contract.
    void
    trace(rust::Str msg, rust::Slice<std::uint8_t const> data, TraceDataType dataType)
        const noexcept;
};

}  // namespace xrpl
