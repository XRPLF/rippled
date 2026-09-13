#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/protocol/TER.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace xrpl {

using Bytes = std::vector<std::uint8_t>;
using Hash = xrpl::uint256;
using FloatPair = std::pair<int64_t, int32_t>;

// Error signals that cross the wasm boundary as trap messages (the C API has no
// trap code). WasmiEngine::call maps them to TER: hfErrInternal -> tecINTERNAL,
// hfErrOutOfGas / wasmi's OutOfFuel -> tecOUT_OF_GAS, anything else ->
// tecFAILED_PROCESSING.
//
// Matched as substrings, not by equality: the C API returns the Rust Debug form
// of the error, e.g. `Error { kind: Message("HfInternal") }` or
// `Error { kind: TrapCode(OutOfFuel) }`.
std::string_view inline constexpr hfErrInternal = "HfInternal";
std::string_view inline constexpr hfErrOutOfGas = "HfOutOfGas";
std::string_view inline constexpr wasmiTrapOutOfFuel = "OutOfFuel";

// Guest ABI, mirrored in the wasm stdlib: append only, never renumber. Starts at
// 1 so a zeroed data_type is rejected rather than treated as Int64.
enum class TraceDataType : std::int32_t {
    Int64 = 1,
    Uint64,
    Xfloat,
    Account,
    Amount,
    AsHex,   // raw bytes, hex-encoded by the host before printing
    AsText,  // bytes printed verbatim as text
};

enum class HostFunctionError : int32_t {
    Success = 0,
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
    SubmitTxnFailure = -21,
    InvalidState = -22,
};

enum class WasmTypes { WtI32, WtI64 };

struct Wmem
{
    std::uint8_t* p = nullptr;
    std::size_t s = 0;

    Wmem() = default;
    Wmem(void* ptr, std::size_t size) : p(reinterpret_cast<std::uint8_t*>(ptr)), s(size)
    {
    }
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

class WasmRuntimeWrapper
{
public:
    virtual ~WasmRuntimeWrapper() = default;

    virtual Wmem
    getMem() = 0;

    virtual std::int64_t
    getGas() = 0;

    virtual std::int64_t
    setGas(std::int64_t gas) = 0;

    virtual std::int64_t
    getTransferLimit() = 0;

    virtual std::int64_t
    setTransferLimit(std::int64_t transferLimit) = 0;
};
using RTOptRef = std::optional<std::reference_wrapper<WasmRuntimeWrapper>>;

struct WasmParam
{
    // We are not supporting float/double

    WasmTypes type = WasmTypes::WtI32;
    union
    {
        std::int32_t i32;
        std::int64_t i64 = 0;
    } of;
};

template <class... Types>
inline void
wasmParamsHlp(std::vector<WasmParam>& v, std::int32_t p, Types&&... args)
{
    v.push_back({.type = WasmTypes::WtI32, .of = {.i32 = p}});
    wasmParamsHlp(v, std::forward<Types>(args)...);
}

template <class... Types>
inline void
wasmParamsHlp(std::vector<WasmParam>& v, std::int64_t p, Types&&... args)
{
    v.push_back({.type = WasmTypes::WtI64, .of = {.i64 = p}});
    wasmParamsHlp(v, std::forward<Types>(args)...);
}

inline void
wasmParamsHlp(std::vector<WasmParam>& v)
{
}

template <class... Types>
inline std::vector<WasmParam>
wasmParams(Types&&... args)
{
    std::vector<WasmParam> v;
    v.reserve(sizeof...(args));
    wasmParamsHlp(v, std::forward<Types>(args)...);
    return v;
}

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

}  // namespace xrpl
