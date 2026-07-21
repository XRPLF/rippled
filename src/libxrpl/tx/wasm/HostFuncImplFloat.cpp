#include <xrpl/basics/Expected.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/HostFuncImpl.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <boost/algorithm/hex.hpp>

#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

#ifdef _DEBUG
// #define DEBUG_OUTPUT 1
#endif

namespace xrpl {

namespace wasm_float {

namespace detail {

// Decode a serialized STNumber float payload. Returns nullopt if the data is
// not a well-formed encoding.
std::optional<Number>
floatDecode(Slice const& data)
{
    static unsigned constexpr encodedFloatSize = 12;
    if (data.size() != encodedFloatSize)
        return std::nullopt;
    try
    {
        SerialIter it(data);
        return STNumber(it, sfNumber).value();
    }
    catch (...)
    {
        return std::nullopt;
    }
}

// Build a Number from a raw mantissa/exponent pair. Returns nullopt if the
// value cannot be represented, e.g. the exponent is out of range.
std::optional<Number>
numberFromMantExp(int64_t mantissa, int32_t exponent)
{
    try
    {
        return Number(mantissa, exponent);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

// Serialize a Number to the STNumber float encoding.
Expected<Bytes, HostFunctionError>
floatEncode(Number const& n)
{
    Serializer msg;
    STNumber(sfNumber, n).add(msg);
    auto data = msg.getData();

#ifdef DEBUG_OUTPUT
    std::cout << "m: " << std::setw(20) << n.mantissa() << ", e: " << std::setw(12) << n.exponent()
              << ", hex: ";
    std::cout << std::hex << std::uppercase << std::setfill('0');
    for (auto const& c : data)
        std::cout << std::setw(2) << (unsigned)c << " ";
    std::cout << std::dec << std::setfill(' ') << std::endl;
#endif
    return Expected<Bytes, HostFunctionError>(std::move(data));
}

struct FloatState
{
    // Set only when the requested mode is valid; sets the rounding mode on
    // construction and restores the previous mode on destruction.
    std::optional<NumberRoundModeGuard> guard;

    explicit FloatState(int32_t mode)
    {
        if (auto const rm = Number::checkedRoundingMode(mode))
            guard.emplace(*rm);
    }

    explicit
    operator bool() const
    {
        return guard.has_value();
    }
};

}  // namespace detail

std::string
floatToString(Slice const& data)
{
    // set default mode as we don't expect it will be used here
    detail::FloatState const rm(static_cast<int32_t>(Number::RoundingMode::ToNearest));
    auto const num = detail::floatDecode(data);
    if (!num)
    {
        std::string hex;
        hex.reserve(data.size() * 2);
        boost::algorithm::hex(data.begin(), data.end(), std::back_inserter(hex));
        return "Invalid data: " + hex;
    }
    return to_string(*num);
}

Expected<Bytes, HostFunctionError>
floatFromIntImpl(int64_t x, int32_t mode)
{
    try
    {
        detail::FloatState const rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FloatInputMalformed);

        return detail::floatEncode(Number(x));
    }
    // LCOV_EXCL_START
    catch (...)
    {
        return Unexpected(HostFunctionError::FloatComputationError);
    }
    // LCOV_EXCL_STOP
}

Expected<Bytes, HostFunctionError>
floatFromUintImpl(uint64_t x, int32_t mode)
{
    try
    {
        detail::FloatState const rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FloatInputMalformed);

        return detail::floatEncode(Number(x, 0, Number::Normalized{}));
    }
    // LCOV_EXCL_START
    catch (...)
    {
        return Unexpected(HostFunctionError::FloatComputationError);
    }
    // LCOV_EXCL_STOP
}

Expected<Bytes, HostFunctionError>
floatFromSTAmountImpl(STAmount const& x, int32_t mode)
{
    try
    {
        detail::FloatState const rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FloatInputMalformed);

        return detail::floatEncode(static_cast<Number>(x));
    }
    // LCOV_EXCL_START
    catch (...)
    {
        return Unexpected(HostFunctionError::FloatComputationError);
    }
    // LCOV_EXCL_STOP
}

Expected<Bytes, HostFunctionError>
floatFromSTNumberImpl(STNumber const& x, int32_t mode)
{
    try
    {
        detail::FloatState const rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FloatInputMalformed);

        return detail::floatEncode(x.value());
    }
    // LCOV_EXCL_START
    catch (...)
    {
        return Unexpected(HostFunctionError::FloatComputationError);
    }
    // LCOV_EXCL_STOP
}

Expected<int64_t, HostFunctionError>
floatToIntImpl(Slice const& x, int32_t mode)
{
    try
    {
        detail::FloatState const rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FloatInputMalformed);

        auto const num = detail::floatDecode(x);
        if (!num)
            return Unexpected(HostFunctionError::FloatInputMalformed);  // LCOV_EXCL_LINE
        return static_cast<int64_t>(*num);
    }
    // LCOV_EXCL_START
    catch (...)
    {
        return Unexpected(HostFunctionError::FloatComputationError);
    }
    // LCOV_EXCL_STOP
}

Expected<FloatPair, HostFunctionError>
floatToMantExpImpl(Slice const& x)
{
    try
    {
        detail::FloatState const rm(static_cast<int32_t>(Number::RoundingMode::ToNearest));
        if (!rm)
            return Unexpected(HostFunctionError::FloatInputMalformed);

        auto const num = detail::floatDecode(x);
        if (!num)
            return Unexpected(HostFunctionError::FloatInputMalformed);  // LCOV_EXCL_LINE

        return FloatPair(num->mantissa(), num->exponent());
    }
    // LCOV_EXCL_START
    catch (...)
    {
        return Unexpected(HostFunctionError::FloatComputationError);
    }
    // LCOV_EXCL_STOP
}

Expected<Bytes, HostFunctionError>
floatFromMantExpImpl(int64_t mantissa, int32_t exponent, int32_t mode)
{
    try
    {
        detail::FloatState const rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FloatInputMalformed);
        auto const num = detail::numberFromMantExp(mantissa, exponent);
        if (!num)
            return Unexpected(HostFunctionError::FloatInputMalformed);
        return detail::floatEncode(*num);
    }
    catch (...)
    {
        return Unexpected(HostFunctionError::FloatComputationError);
    }
}

Expected<int32_t, HostFunctionError>
floatCompareImpl(Slice const& x, Slice const& y)
{
    try
    {
        // set default mode as we don't expect it will be used here
        detail::FloatState const rm(static_cast<int32_t>(Number::RoundingMode::ToNearest));

        auto const xx = detail::floatDecode(x);
        if (!xx)
            return Unexpected(HostFunctionError::FloatInputMalformed);
        auto const yy = detail::floatDecode(y);
        if (!yy)
            return Unexpected(HostFunctionError::FloatInputMalformed);
        if (*xx < *yy)
            return 2;
        if (*xx == *yy)
            return 0;
        return 1;
    }
    // LCOV_EXCL_START
    catch (...)
    {
        return Unexpected(HostFunctionError::FloatComputationError);
    }
    // LCOV_EXCL_STOP
}

Expected<Bytes, HostFunctionError>
floatAddImpl(Slice const& x, Slice const& y, int32_t mode)
{
    try
    {
        detail::FloatState const rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FloatInputMalformed);

        auto const xx = detail::floatDecode(x);
        if (!xx)
            return Unexpected(HostFunctionError::FloatInputMalformed);
        auto const yy = detail::floatDecode(y);
        if (!yy)
            return Unexpected(HostFunctionError::FloatInputMalformed);

        return detail::floatEncode(*xx + *yy);
    }
    // LCOV_EXCL_START
    catch (...)
    {
        return Unexpected(HostFunctionError::FloatComputationError);
    }
    // LCOV_EXCL_STOP
}

Expected<Bytes, HostFunctionError>
floatSubtractImpl(Slice const& x, Slice const& y, int32_t mode)
{
    try
    {
        detail::FloatState const rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FloatInputMalformed);
        auto const xx = detail::floatDecode(x);
        if (!xx)
            return Unexpected(HostFunctionError::FloatInputMalformed);
        auto const yy = detail::floatDecode(y);
        if (!yy)
            return Unexpected(HostFunctionError::FloatInputMalformed);

        return detail::floatEncode(*xx - *yy);
    }
    // LCOV_EXCL_START
    catch (...)
    {
        return Unexpected(HostFunctionError::FloatComputationError);
    }
    // LCOV_EXCL_STOP
}

Expected<Bytes, HostFunctionError>
floatMultiplyImpl(Slice const& x, Slice const& y, int32_t mode)
{
    try
    {
        detail::FloatState const rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FloatInputMalformed);
        auto const xx = detail::floatDecode(x);
        if (!xx)
            return Unexpected(HostFunctionError::FloatInputMalformed);
        auto const yy = detail::floatDecode(y);
        if (!yy)
            return Unexpected(HostFunctionError::FloatInputMalformed);

        return detail::floatEncode(*xx * *yy);
    }
    // LCOV_EXCL_START
    catch (...)
    {
        return Unexpected(HostFunctionError::FloatComputationError);
    }
    // LCOV_EXCL_STOP
}

Expected<Bytes, HostFunctionError>
floatDivideImpl(Slice const& x, Slice const& y, int32_t mode)
{
    try
    {
        detail::FloatState const rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FloatInputMalformed);
        auto const xx = detail::floatDecode(x);
        if (!xx)
            return Unexpected(HostFunctionError::FloatInputMalformed);
        auto const yy = detail::floatDecode(y);
        if (!yy)
            return Unexpected(HostFunctionError::FloatInputMalformed);

        return detail::floatEncode(*xx / *yy);
    }
    catch (...)
    {
        return Unexpected(HostFunctionError::FloatComputationError);
    }
}

Expected<Bytes, HostFunctionError>
floatRootImpl(Slice const& x, int32_t n, int32_t mode)
{
    try
    {
        if (n < 1)
            return Unexpected(HostFunctionError::FloatInputMalformed);

        detail::FloatState const rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FloatInputMalformed);

        auto const xx = detail::floatDecode(x);
        if (!xx)
            return Unexpected(HostFunctionError::FloatInputMalformed);

        return detail::floatEncode(root(*xx, n));
    }
    // LCOV_EXCL_START
    catch (...)
    {
        return Unexpected(HostFunctionError::FloatComputationError);
    }
    // LCOV_EXCL_STOP
}

Expected<Bytes, HostFunctionError>
floatPowerImpl(Slice const& x, int32_t n, int32_t mode)
{
    try
    {
        if ((n < 0) || (n > Number::kMaxExponent))
            return Unexpected(HostFunctionError::FloatInputMalformed);

        detail::FloatState const rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FloatInputMalformed);

        auto const xx = detail::floatDecode(x);
        if (!xx)
            return Unexpected(HostFunctionError::FloatInputMalformed);
        if (*xx == Number() && (n == 0))
            return Unexpected(HostFunctionError::InvalidParams);

        return detail::floatEncode(power(*xx, n, 1));
    }
    // LCOV_EXCL_START
    catch (...)
    {
        return Unexpected(HostFunctionError::FloatComputationError);
    }
    // LCOV_EXCL_STOP
}

}  // namespace wasm_float

// =========================================================
// ACTUAL HOST FUNCTIONS
// =========================================================

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatFromInt(int64_t x, int32_t mode) const
{
    return wasm_float::floatFromIntImpl(x, mode);
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatFromUint(uint64_t x, int32_t mode) const
{
    return wasm_float::floatFromUintImpl(x, mode);
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatFromSTAmount(STAmount const& x, int32_t mode) const
{
    return wasm_float::floatFromSTAmountImpl(x, mode);
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatFromSTNumber(STNumber const& x, int32_t mode) const
{
    return wasm_float::floatFromSTNumberImpl(x, mode);
}

Expected<int64_t, HostFunctionError>
WasmHostFunctionsImpl::floatToInt(Slice const& x, int32_t mode) const
{
    return wasm_float::floatToIntImpl(x, mode);
}

Expected<FloatPair, HostFunctionError>
WasmHostFunctionsImpl::floatToMantExp(Slice const& x) const
{
    return wasm_float::floatToMantExpImpl(x);
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatFromMantExp(int64_t mantissa, int32_t exponent, int32_t mode) const
{
    return wasm_float::floatFromMantExpImpl(mantissa, exponent, mode);
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::floatCompare(Slice const& x, Slice const& y) const
{
    return wasm_float::floatCompareImpl(x, y);
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatAdd(Slice const& x, Slice const& y, int32_t mode) const
{
    return wasm_float::floatAddImpl(x, y, mode);
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatSubtract(Slice const& x, Slice const& y, int32_t mode) const
{
    return wasm_float::floatSubtractImpl(x, y, mode);
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatMultiply(Slice const& x, Slice const& y, int32_t mode) const
{
    return wasm_float::floatMultiplyImpl(x, y, mode);
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatDivide(Slice const& x, Slice const& y, int32_t mode) const
{
    return wasm_float::floatDivideImpl(x, y, mode);
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatRoot(Slice const& x, int32_t n, int32_t mode) const
{
    return wasm_float::floatRootImpl(x, n, mode);
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatPower(Slice const& x, int32_t n, int32_t mode) const
{
    return wasm_float::floatPowerImpl(x, n, mode);
}

}  // namespace xrpl
