#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/TER.h>

// Generated from the Rust ABI. Exported so that including this header counts as including those for
// misc-include-cleaner.
#include <xrpl_host_functions.h>  // IWYU pragma: export

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <optional>
#include <source_location>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace xrpl {

using Bytes = std::vector<std::uint8_t>;
using Hash = xrpl::uint256;
using FloatPair = std::pair<int64_t, int32_t>;

template <typename T>
struct WasmResult
{
    T result;
    int64_t cost;
};
using EscrowResult = WasmResult<int32_t>;

// Engine error when wasm does not run to completion. `cost` is the gas consumed
// when meaningful (tecOUT_OF_GAS / tecFAILED_PROCESSING; caller writes it to tx
// metadata); std::nullopt for tecINTERNAL and malformed input (no gas reported).
struct WasmTER
{
    TER ter;
    std::optional<int64_t> cost;
};

template <typename T, size_t Size = sizeof(T)>
constexpr T
adjustWasmEndianessHlp(T x)
{
    static_assert(std::is_integral_v<T>, "Only integral types");
    if constexpr (Size > 1)
    {
        using U = std::make_unsigned_t<T>;
        U u = static_cast<U>(x);
        U const low = (u & 0xFF) << ((Size - 1) << 3);
        u = adjustWasmEndianessHlp<U, Size - 1>(u >> 8);
        return static_cast<T>(low | u);
    }

    return x;
}

template <typename T, size_t Size = sizeof(T)>
constexpr T
adjustWasmEndianess(T x)
{
    // LCOV_EXCL_START
    static_assert(std::is_integral_v<T>, "Only integral types");
    if constexpr (std::endian::native == std::endian::big)
    {
        return adjustWasmEndianessHlp(x);
    }
    return x;
    // LCOV_EXCL_STOP
}

class FieldLocator
{
    std::span<uint8_t const> bytes_;

public:
    explicit FieldLocator(std::span<uint8_t const> bytes) : bytes_(bytes)
    {
    }

    FieldLocator(FieldLocator const&) = delete;
    FieldLocator&
    operator=(FieldLocator const&) = delete;
    FieldLocator(FieldLocator&&) = default;
    FieldLocator&
    operator=(FieldLocator&&) = default;

    int32_t
    operator[](size_t i) const
    {
        if (i >= size())
        {
            // LCOV_EXCL_START
            UNREACHABLE("xrpl::FieldLocator::operator[] : index out of bounds");
            Throw<std::runtime_error>("index out of bounds");
            // LCOV_EXCL_STOP
        }
        auto step = int32_t{};
        std::memcpy(&step, bytes_.data() + (i * sizeof(int32_t)), sizeof(int32_t));
        return adjustWasmEndianess(step);
    }

    [[nodiscard]] size_t
    size() const
    {
        return bytes_.size() / sizeof(int32_t);
    }
};

constexpr int32_t
hfErrorToInt(HostFunctionError e)
{
    return static_cast<int32_t>(e);
}

constexpr int32_t
floatOrderingToInt(FloatOrdering o)
{
    return static_cast<int32_t>(o);
}

template <class Body>
std::invoke_result_t<Body>
guarded(
    beast::Journal journal,
    std::invoke_result_t<Body> onThrow,
    Body&& body,
    std::source_location const location = std::source_location::current()) noexcept
{
    try
    {
        return body();
    }
    catch (std::exception const& e)
    {
        JLOG(journal.error()) << "wasm: " << location.function_name() << " threw: " << e.what();
    }
    catch (...)
    {
        JLOG(journal.error()) << "wasm: " << location.function_name() << " threw";
    }

    return onThrow;
}

}  // namespace xrpl
