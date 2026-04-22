#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>

#include <functional>
#include <optional>
#include <stdexcept>
#include <vector>

namespace xrpl {

using Bytes = std::vector<std::uint8_t>;
using Hash = xrpl::uint256;
using FloatPair = std::pair<int64_t, int32_t>;

enum class HostFunctionError : int32_t {
    Internal = -1,
    FieldNotFound = -2,
    BufferTooSmall = -3,
    NoArray = -4,
    NotLeafField = -5,
    LocatorMalformed = -6,
    SlotOutRange = -7,
    SlotsFull = -8,
    EmptySlot = -9,
    LedgerObjNotFound = -10,
    Decoding = -11,
    DataFieldTooLarge = -12,
    PointerOutOfBounds = -13,
    NoMemExported = -14,
    InvalidParams = -15,
    InvalidAccount = -16,
    InvalidField = -17,
    IndexOutOfBounds = -18,
    FloatInputMalformed = -19,
    FloatComputationError = -20,
    NoRuntime = -21,
    OutOfGas = -22,
    OutOfTransferLimit = -23,
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
    return;
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
