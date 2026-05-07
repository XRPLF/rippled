#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/tx/wasm/HostFuncImpl.h>

#ifdef _DEBUG
// #define DEBUG_OUTPUT 1
#endif

namespace xrpl {

namespace wasm_float {

namespace detail {

class WasmNumber : public Number
{
protected:
    static unsigned constexpr encodedFloatSize = 12;
    bool good_ = false;

public:
    WasmNumber(Slice const& data)
    {
        if (data.size() != encodedFloatSize)
            return;
        try
        {
            SerialIter it(data);
            Number const x = STNumber(it, sfNumber).value();
            *static_cast<Number*>(this) = x;
        }
        catch (...)
        {
            return;
        }

        good_ = true;
    }

    WasmNumber(Number const& n) : WasmNumber(n.mantissa(), n.exponent())  // ensure Number canonized
    {
    }

    template <class T>
    WasmNumber(T mantissa = 0, int32_t exponent = 0)
    {
        try
        {
            Number n;
            if constexpr (std::is_signed_v<T>)
            {
                n = Number(static_cast<int64_t>(mantissa), exponent);
            }
            else
            {
                n = Number(static_cast<uint64_t>(mantissa), exponent, Number::normalized());
            }
            *static_cast<Number*>(this) = n;
        }
        catch (...)
        {
            return;
        }
        good_ = true;
    }

    WasmNumber&
    operator=(WasmNumber const&) = default;

    explicit
    operator bool() const
    {
        return good_;
    }

    explicit
    operator int64_t() const
    {
        return Number::operator int64_t();
    }

    Expected<Bytes, HostFunctionError>
    toBytes() const
    {
        Serializer msg;
        STNumber(sfNumber, *this).add(msg);
        auto data = msg.getData();

#ifdef DEBUG_OUTPUT
        std::cout << "m: " << std::setw(20) << mantissa() << ", e: " << std::setw(12) << exponent()
                  << ", hex: ";
        std::cout << std::hex << std::uppercase << std::setfill('0');
        for (auto const& c : data)
            std::cout << std::setw(2) << (unsigned)c << " ";
        std::cout << std::dec << std::setfill(' ') << std::endl;
#endif
        return Expected<Bytes, HostFunctionError>(std::move(data));
    }
};

struct FloatState
{
    Number::rounding_mode oldMode_;
    bool good_ = false;

    FloatState(int32_t mode) : oldMode_(Number::getround())
    {
        if (mode < Number::rounding_mode::to_nearest || mode > Number::rounding_mode::upward)
            return;
        Number::setround(static_cast<Number::rounding_mode>(mode));
        good_ = true;
    }

    ~FloatState()
    {
        Number::setround(oldMode_);
    }

    operator bool() const
    {
        return good_;
    }
};

}  // namespace detail

std::string
floatToString(Slice const& data)
{
    // set default mode as we don't expect it will be used here
    detail::FloatState const rm(Number::rounding_mode::to_nearest);
    detail::WasmNumber const num(data);
    if (!num)
    {
        std::string hex;
        hex.reserve(data.size() * 2);
        boost::algorithm::hex(data.begin(), data.end(), std::back_inserter(hex));
        return "Invalid data: " + hex;
    }
    auto const s = to_string(num);
    return s;
}

Expected<Bytes, HostFunctionError>
floatFromIntImpl(int64_t x, int32_t mode)
{
    try
    {
        detail::FloatState const rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        detail::WasmNumber const num(x);
        if (!num)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);  // LCOV_EXCL_LINE
        auto const r = num.toBytes();
        return r;
    }
    // LCOV_EXCL_START
    catch (...)
    {
        return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
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
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        detail::WasmNumber const num(x);
        if (!num)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);  // LCOV_EXCL_LINE
        auto const r = num.toBytes();
        return r;
    }
    // LCOV_EXCL_START
    catch (...)
    {
        return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
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
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        detail::WasmNumber const num(static_cast<Number>(x));
        if (!num)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);  // LCOV_EXCL_LINE
        auto const r = num.toBytes();
        return r;
    }
    // LCOV_EXCL_START
    catch (...)
    {
        return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
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
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        detail::WasmNumber const num(x.value());
        if (!num)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);  // LCOV_EXCL_LINE
        auto const r = num.toBytes();
        return r;
    }
    // LCOV_EXCL_START
    catch (...)
    {
        return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
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
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        detail::WasmNumber const num(x);
        if (!num)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);  // LCOV_EXCL_LINE
        int64_t const r(num);
        return r;
    }
    // LCOV_EXCL_START
    catch (...)
    {
        return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
    }
    // LCOV_EXCL_STOP
}

Expected<FloatPair, HostFunctionError>
floatToMantExpImpl(Slice const& x)
{
    try
    {
        detail::FloatState const rm(Number::rounding_mode::to_nearest);
        if (!rm)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        detail::WasmNumber const num(x);
        if (!num)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);  // LCOV_EXCL_LINE

        return FloatPair(num.mantissa(), num.exponent());
    }
    // LCOV_EXCL_START
    catch (...)
    {
        return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
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
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        detail::WasmNumber const num(mantissa, exponent);
        if (!num)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        return num.toBytes();
    }
    catch (...)
    {
        return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
    }
}

Expected<int32_t, HostFunctionError>
floatCompareImpl(Slice const& x, Slice const& y)
{
    try
    {
        // set default mode as we don't expect it will be used here
        detail::FloatState const rm(Number::rounding_mode::to_nearest);

        detail::WasmNumber const xx(x);
        if (!xx)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        detail::WasmNumber const yy(y);
        if (!yy)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        if (xx < yy)
            return 2;
        if (xx == yy)
            return 0;
        return 1;
    }
    // LCOV_EXCL_START
    catch (...)
    {
        return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
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
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        detail::WasmNumber const xx(x);
        if (!xx)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        detail::WasmNumber const yy(y);
        if (!yy)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        detail::WasmNumber const res = xx + yy;

        return res.toBytes();
    }
    // LCOV_EXCL_START
    catch (...)
    {
        return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
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
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        detail::WasmNumber const xx(x);
        if (!xx)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        detail::WasmNumber const yy(y);
        if (!yy)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        detail::WasmNumber const res = xx - yy;

        return res.toBytes();
    }
    // LCOV_EXCL_START
    catch (...)
    {
        return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
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
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        detail::WasmNumber const xx(x);
        if (!xx)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        detail::WasmNumber const yy(y);
        if (!yy)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        detail::WasmNumber const res = xx * yy;

        return res.toBytes();
    }
    // LCOV_EXCL_START
    catch (...)
    {
        return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
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
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        detail::WasmNumber const xx(x);
        if (!xx)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        detail::WasmNumber const yy(y);
        if (!yy)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        detail::WasmNumber const res = xx / yy;

        return res.toBytes();
    }
    catch (...)
    {
        return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
    }
}

Expected<Bytes, HostFunctionError>
floatRootImpl(Slice const& x, int32_t n, int32_t mode)
{
    try
    {
        if (n < 1)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        detail::FloatState const rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        detail::WasmNumber const xx(x);
        if (!xx)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        detail::WasmNumber const res(root(xx, n));

        return res.toBytes();
    }
    // LCOV_EXCL_START
    catch (...)
    {
        return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
    }
    // LCOV_EXCL_STOP
}

Expected<Bytes, HostFunctionError>
floatPowerImpl(Slice const& x, int32_t n, int32_t mode)
{
    try
    {
        if ((n < 0) || (n > Number::maxExponent))
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        detail::FloatState const rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        detail::WasmNumber const xx(x);
        if (!xx)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        if (xx == Number() && (n == 0))
            return Unexpected(HostFunctionError::INVALID_PARAMS);

        detail::WasmNumber const res(power(xx, n, 1));

        return res.toBytes();
    }
    // LCOV_EXCL_START
    catch (...)
    {
        return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
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
