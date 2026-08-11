#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/TER.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace xrpl {

using Bytes = std::vector<std::uint8_t>;
using Hash = xrpl::uint256;
using FloatPair = std::pair<int64_t, int32_t>;

enum class HostFunctionError : int32_t {
    Unimplemented = -1,
    FieldNotFound = -2,
    BufferTooSmall = -3,
    NoArray = -4,
    NotLeafField = -5,
    LocatorMalformed = -6,
    SlotOutRange = -7,
    SlotsFull = -8,
    EmptySlot = -9,
    LedgerObjNotFound = -10,
    OutOfTransferLimit = -11,
    DataFieldTooLarge = -12,
    PointerOutOfBounds = -13,
    NoMemExported = -14,
    InvalidParams = -15,
    InvalidAccount = -16,
    InvalidField = -17,
    IndexOutOfBounds = -18,
    FloatInputMalformed = -19,
    FloatComputationError = -20,

    // The call was not served at all, so the engine stops the run and the transaction is
    // tecINTERNAL rather than the contract being handed a code to interpret. `guarded`
    // answers it for a host body that throws.
    //
    // Outside the -1 ..= -20 range that a contract reads, and the only entry that is: it
    // needs no number in that range, and INT32_MIN cannot collide with a code appended
    // above. Negative so that a reader treating it as an ordinary failure is still right.
    InternalFatal = std::numeric_limits<int32_t>::min(),
};

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

class FieldLocator
{
    int32_t const* ptr_ = nullptr;
    uint32_t size_ = 0;
    std::vector<int32_t> buf_;

public:
    FieldLocator(std::vector<int32_t>&& buf)
        : ptr_(&buf[0]), size_(buf.size()), buf_(std::move(buf))
    {
    }

    FieldLocator(int32_t const* ptr, uint32_t const size) : ptr_(ptr), size_(size)
    {
    }

    FieldLocator(FieldLocator const&) = delete;
    FieldLocator&
    operator=(FieldLocator const&) = delete;
    FieldLocator(FieldLocator&&) = default;
    FieldLocator&
    operator=(FieldLocator&&) = default;

    int32_t
    operator[](unsigned i) const
    {
        if (i >= size_)
            Throw<std::runtime_error>("index out of bounds");
        return ptr_[i];
    }

    [[nodiscard]] uint32_t
    size() const
    {
        return size_;
    }

    [[nodiscard]] int32_t const*
    data() const
    {
        return ptr_;
    }

    [[nodiscard]] bool
    empty() const
    {
        return size_ == 0;
    }
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

constexpr int32_t
hfErrorToInt(HostFunctionError e)
{
    return static_cast<int32_t>(e);
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
