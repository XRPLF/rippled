#pragma once

#include <xrpl/tx/wasm/HostConstants.h>

#include <cstdint>
#include <ostream>
#include <variant>

namespace xrpl {

// How a host function fails: with a code the contract reads, or with one that stops the run.
// The C++ side of the Rust ABI's `HostFailure`; both kinds cross the bridge as one `i32`,
// which the engine splits by value.
class HostFailure
{
    std::variant<HostFunctionError, HostTrapError> error_;

public:
    // Implicit, so `std::unexpected(HostFunctionError::X)` converts into an
    // `std::expected<T, HostFailure>`.
    HostFailure(HostFunctionError error);  // NOLINT(google-explicit-constructor)
    HostFailure(HostTrapError error);      // NOLINT(google-explicit-constructor)

    [[nodiscard]] std::int32_t
    toInt() const;

    friend bool
    operator==(HostFailure const&, HostFailure const&) = default;
};

std::ostream&
operator<<(std::ostream& os, HostFailure failure);

}  // namespace xrpl
