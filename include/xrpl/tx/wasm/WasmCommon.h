#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/HostConstants.h>

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

template <typename T>
constexpr T
adjustWasmEndianess(T x)
{
    static_assert(std::is_integral_v<T>, "Only integral types");
    if constexpr (std::endian::native == std::endian::big)
    {
        return std::byteswap(x);  // LCOV_EXCL_LINE
    }
    return x;
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
