#include <xrpld/app/wasm/HostFuncImpl.h>

#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/digest.h>

#ifdef _DEBUG
// #define DEBUG_OUTPUT 1
#endif

namespace xrpl {

namespace wasm_float {

namespace detail {

class Number2 : public Number
{
protected:
    static Bytes const floatNull;
    static unsigned constexpr encodedFloatSize = 8;
    static int32_t constexpr encodedMantissaBits = 54;
    static int32_t constexpr encodedExponentBits = 8;

    static_assert(wasmMinExponent < 0);

    static uint64_t constexpr maxEncodedMantissa = (1ull << (encodedMantissaBits + 1)) - 1;

    bool good_;

public:
    Number2(Slice const& data) : Number(), good_(false)
    {
        if (data.size() != encodedFloatSize)
            return;

        if (std::ranges::equal(floatNull, data))
        {
            good_ = true;
            return;
        }

        uint64_t const v = SerialIter(data).get64();
        if (!(v & STAmount::cIssuedCurrency))
            return;

        int32_t const e = static_cast<int32_t>((v >> encodedMantissaBits) & 0xFFull);
        int32_t const decodedExponent = e + wasmMinExponent - 1;  // e - 97
        if (decodedExponent < wasmMinExponent || decodedExponent > wasmMaxExponent)
            return;

        int64_t const neg = (v & STAmount::cPositive) ? 1 : -1;
        int64_t const m = neg * static_cast<int64_t>(v & ((1ull << encodedMantissaBits) - 1));
        if (!m)
            return;

        Number x(makeNumber(m, decodedExponent));
        if (m != x.mantissa() || decodedExponent != x.exponent())
            return;  // not canonical
        *static_cast<Number*>(this) = x;

        good_ = true;
    }

    template <class T>
    Number2(T mantissa = 0, int32_t exponent = 0) : Number(), good_(false)
    {
        if (!mantissa)
        {
            good_ = true;
            return;
        }

        auto const n = makeNumber(mantissa, exponent);
        auto const e = n.exponent();
        if (e < wasmMinExponent)
        {
            good_ = true;  // value is zero(as in Numbers behavior)
            return;
        }

        if (e > wasmMaxExponent)
            return;

        *static_cast<Number*>(this) = n;
        good_ = true;
    }

    Number2(Number const& n) : Number2(n.mantissa(), n.exponent())  // ensure Number canonized
    {
    }

    static Number
    makeNumber(int64_t mantissa, int32_t exponent)
    {
        if (mantissa < 0)
            return Number(true, -static_cast<uint64_t>(mantissa), exponent, Number::normalized());
        return Number(false, mantissa, exponent, Number::normalized());
    }

    static Number
    makeNumber(uint64_t mantissa, int32_t exponent)
    {
        return Number(false, mantissa, exponent, Number::normalized());
    }

    operator bool() const
    {
        return good_;
    }

    Expected<Bytes, HostFunctionError>
    toBytes() const
    {
        if (!good_)
            return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);

        auto const m = mantissa();
        auto const e = exponent();

        uint64_t v = m >= 0 ? STAmount::cPositive : 0;
        v |= STAmount::cIssuedCurrency;

        uint64_t const absM = std::abs(m);
        if (!absM)
        {
            return floatNull;
        }
        else if (absM > maxEncodedMantissa)
        {
            return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);  // LCOV_EXCL_LINE
        }
        v |= absM;

        if (e > wasmMaxExponent)
            return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
        else if (e < wasmMinExponent)
            return floatNull;
        uint64_t const normExp = e - wasmMinExponent + 1;  //+97
        v |= normExp << encodedMantissaBits;

        Serializer msg;
        msg.add64(v);
        auto const data = msg.getData();

#ifdef DEBUG_OUTPUT
        std::cout << "m: " << std::setw(20) << mantissa() << ", e: " << std::setw(12) << exponent()
                  << ", hex: ";
        std::cout << std::hex << std::uppercase << std::setfill('0');
        for (auto const& c : data)
            std::cout << std::setw(2) << (unsigned)c << " ";
        std::cout << std::dec << std::setfill(' ') << std::endl;
#endif

        return data;
    }
};

Bytes const Number2::floatNull = {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

struct FloatState
{
    Number::rounding_mode oldMode_;
    MantissaRange::mantissa_scale oldScale_;
    bool good_;

    FloatState(int32_t mode)
        : oldMode_(Number::getround()), oldScale_(Number::getMantissaScale()), good_(false)
    {
        if (mode < Number::rounding_mode::to_nearest || mode > Number::rounding_mode::upward)
            return;

        Number::setround(static_cast<Number::rounding_mode>(mode));
        Number::setMantissaScale(MantissaRange::mantissa_scale::small);
        good_ = true;
    }

    ~FloatState()
    {
        Number::setround(oldMode_);
        Number::setMantissaScale(oldScale_);
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
    detail::FloatState rm(Number::rounding_mode::to_nearest);
    detail::Number2 const num(data);
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
        detail::FloatState rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        detail::Number2 num(x);
        return num.toBytes();
    }
    // LCOV_EXCL_START
    catch (...)
    {
    }
    return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
    // LCOV_EXCL_STOP
}

Expected<Bytes, HostFunctionError>
floatFromUintImpl(uint64_t x, int32_t mode)
{
    try
    {
        detail::FloatState rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        detail::Number2 num(x);
        auto r = num.toBytes();
        return r;
    }
    // LCOV_EXCL_START
    catch (...)
    {
    }
    return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
    // LCOV_EXCL_STOP
}

Expected<Bytes, HostFunctionError>
floatSetImpl(int64_t mantissa, int32_t exponent, int32_t mode)
{
    try
    {
        detail::FloatState rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        detail::Number2 num(mantissa, exponent);
        if (!num)
            return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
        return num.toBytes();
    }
    catch (...)
    {
    }
    return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
}

Expected<int32_t, HostFunctionError>
floatCompareImpl(Slice const& x, Slice const& y)
{
    try
    {
        // set default mode as we don't expect it will be used here
        detail::FloatState rm(Number::rounding_mode::to_nearest);
        detail::Number2 xx(x);
        if (!xx)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        detail::Number2 yy(y);
        if (!yy)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        return xx < yy ? 2 : (xx == yy ? 0 : 1);
    }
    // LCOV_EXCL_START
    catch (...)
    {
    }
    return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
    // LCOV_EXCL_STOP
}

Expected<Bytes, HostFunctionError>
floatAddImpl(Slice const& x, Slice const& y, int32_t mode)
{
    try
    {
        detail::FloatState rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        detail::Number2 xx(x);
        if (!xx)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        detail::Number2 yy(y);
        if (!yy)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        detail::Number2 res = xx + yy;

        return res.toBytes();
    }
    // LCOV_EXCL_START
    catch (...)
    {
    }
    return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
    // LCOV_EXCL_STOP
}

Expected<Bytes, HostFunctionError>
floatSubtractImpl(Slice const& x, Slice const& y, int32_t mode)
{
    try
    {
        detail::FloatState rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        detail::Number2 xx(x);
        if (!xx)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        detail::Number2 yy(y);
        if (!yy)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        detail::Number2 res = xx - yy;

        return res.toBytes();
    }
    // LCOV_EXCL_START
    catch (...)
    {
    }
    return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
    // LCOV_EXCL_STOP
}

Expected<Bytes, HostFunctionError>
floatMultiplyImpl(Slice const& x, Slice const& y, int32_t mode)
{
    try
    {
        detail::FloatState rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        detail::Number2 xx(x);
        if (!xx)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        detail::Number2 yy(y);
        if (!yy)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        detail::Number2 res = xx * yy;

        return res.toBytes();
    }
    // LCOV_EXCL_START
    catch (...)
    {
    }
    return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
    // LCOV_EXCL_STOP
}

Expected<Bytes, HostFunctionError>
floatDivideImpl(Slice const& x, Slice const& y, int32_t mode)
{
    try
    {
        detail::FloatState rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        detail::Number2 xx(x);
        if (!xx)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        detail::Number2 yy(y);
        if (!yy)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        detail::Number2 res = xx / yy;

        return res.toBytes();
    }
    catch (...)
    {
    }
    return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
}

Expected<Bytes, HostFunctionError>
floatRootImpl(Slice const& x, int32_t n, int32_t mode)
{
    try
    {
        if (n < 1)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        detail::FloatState rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        detail::Number2 xx(x);
        if (!xx)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        detail::Number2 res(root(xx, n));

        return res.toBytes();
    }
    // LCOV_EXCL_START
    catch (...)
    {
    }
    return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
    // LCOV_EXCL_STOP
}

Expected<Bytes, HostFunctionError>
floatPowerImpl(Slice const& x, int32_t n, int32_t mode)
{
    try
    {
        if ((n < 0) || (n > wasmMaxExponent))
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        detail::FloatState rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        detail::Number2 xx(x);
        if (!xx)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        if (xx == Number() && !n)
            return Unexpected(HostFunctionError::INVALID_PARAMS);

        detail::Number2 res(power(xx, n, 1));

        return res.toBytes();
    }
    // LCOV_EXCL_START
    catch (...)
    {
    }
    return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
    // LCOV_EXCL_STOP
}

Expected<Bytes, HostFunctionError>
floatLogImpl(Slice const& x, int32_t mode)
{
    try
    {
        detail::FloatState rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        detail::Number2 xx(x);
        if (!xx)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        detail::Number2 res(log10(xx));

        return res.toBytes();
    }
    // LCOV_EXCL_START
    catch (...)
    {
    }
    return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
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
WasmHostFunctionsImpl::floatSet(int64_t mantissa, int32_t exponent, int32_t mode) const
{
    return wasm_float::floatSetImpl(mantissa, exponent, mode);
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

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatLog(Slice const& x, int32_t mode) const
{
    return wasm_float::floatLogImpl(x, mode);
}

}  // namespace xrpl
