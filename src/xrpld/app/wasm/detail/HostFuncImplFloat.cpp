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
    static Bytes const FLOAT_NULL;

    bool good_;

public:
    Number2(Slice const& data) : Number(), good_(false)
    {
        if (data.size() != 8)
            return;

        if (std::ranges::equal(FLOAT_NULL, data))
        {
            good_ = true;
            return;
        }

        uint64_t const v = SerialIter(data).get64();
        if (!(v & STAmount::cIssuedCurrency))
            return;

        int64_t const neg = (v & STAmount::cPositive) ? 1 : -1;
        int32_t const e = static_cast<uint8_t>((v >> (64 - 10)) & 0xFFull);
        if (e < 1 || e > 177)
            return;

        int64_t const m = neg * static_cast<int64_t>(v & ((1ull << 54) - 1));
        if (!m)
            return;

        Number x(m, e + IOUAmount::minExponent - 1);
        *static_cast<Number*>(this) = x;
        good_ = true;
    }

    Number2() : Number(), good_(true)
    {
    }

    Number2(int64_t x) : Number(x), good_(true)
    {
    }

    Number2(uint64_t x) : Number(0), good_(false)
    {
        using mtype = std::invoke_result_t<decltype(&Number::mantissa), Number>;
        if (x <= std::numeric_limits<mtype>::max())
            *this = Number(x);
        else
            *this = Number(x / 10, 1) + Number(x % 10);

        good_ = true;
    }

    Number2(int64_t mantissa, int32_t exponent)
        : Number(mantissa, exponent), good_(true)
    {
    }

    Number2(Number const& n) : Number(n), good_(true)
    {
    }

    operator bool() const
    {
        return good_;
    }

    Expected<Bytes, HostFunctionError>
    toBytes() const
    {
        uint64_t v = mantissa() >= 0 ? STAmount::cPositive : 0;
        v |= STAmount::cIssuedCurrency;

        uint64_t const absM = mantissa() >= 0 ? mantissa() : -mantissa();
        if (!absM)
        {
            using etype =
                std::invoke_result_t<decltype(&Number::exponent), Number>;
            if (exponent() != std::numeric_limits<etype>::lowest())
            {
                return Unexpected(
                    HostFunctionError::
                        FLOAT_COMPUTATION_ERROR);  // LCOV_EXCL_LINE
            }
            return FLOAT_NULL;
        }
        else if (absM > ((1ull << 54) - 1))
        {
            return Unexpected(
                HostFunctionError::FLOAT_COMPUTATION_ERROR);  // LCOV_EXCL_LINE
        }
        else if (exponent() > IOUAmount::maxExponent)
            return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
        else if (exponent() < IOUAmount::minExponent)
            return FLOAT_NULL;

        int const e = exponent() - IOUAmount::minExponent + 1;  //+97
        v |= absM;
        v |= ((uint64_t)e) << 54;

        Serializer msg;
        msg.add64(v);
        auto const data = msg.getData();

#ifdef DEBUG_OUTPUT
        std::cout << "m: " << std::setw(20) << mantissa()
                  << ", e: " << std::setw(12) << exponent() << ", hex: ";
        std::cout << std::hex << std::uppercase << std::setfill('0');
        for (auto const& c : data)
            std::cout << std::setw(2) << (unsigned)c << " ";
        std::cout << std::dec << std::setfill(' ') << std::endl;
#endif

        return data;
    }
};

Bytes const Number2::FLOAT_NULL =
    {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

struct SetRound
{
    Number::rounding_mode oldMode_;
    bool good_;

    SetRound(int32_t mode) : oldMode_(Number::getround()), good_(false)
    {
        if (mode < Number::rounding_mode::to_nearest ||
            mode > Number::rounding_mode::upward)
            return;

        Number::setround(static_cast<Number::rounding_mode>(mode));
        good_ = true;
    }

    ~SetRound()
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
    detail::Number2 const num(data);
    if (!num)
    {
        std::string hex;
        hex.reserve(data.size() * 2);
        boost::algorithm::hex(
            data.begin(), data.end(), std::back_inserter(hex));
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
        detail::SetRound rm(mode);
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
        detail::SetRound rm(mode);
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
floatSetImpl(int64_t mantissa, int32_t exponent, int32_t mode)
{
    try
    {
        detail::SetRound rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        detail::Number2 num(mantissa, exponent);
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
        detail::SetRound rm(mode);
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
        detail::SetRound rm(mode);
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
        detail::SetRound rm(mode);
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
        detail::SetRound rm(mode);
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

        detail::SetRound rm(mode);
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
        if ((n < 0) || (n > IOUAmount::maxExponent))
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        detail::SetRound rm(mode);
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
        detail::SetRound rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        detail::Number2 xx(x);
        if (!xx)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        detail::Number2 res(lg(xx));

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
WasmHostFunctionsImpl::floatFromInt(int64_t x, int32_t mode)
{
    return wasm_float::floatFromIntImpl(x, mode);
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatFromUint(uint64_t x, int32_t mode)
{
    return wasm_float::floatFromUintImpl(x, mode);
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatSet(
    int64_t mantissa,
    int32_t exponent,
    int32_t mode)
{
    return wasm_float::floatSetImpl(mantissa, exponent, mode);
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::floatCompare(Slice const& x, Slice const& y)
{
    return wasm_float::floatCompareImpl(x, y);
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatAdd(Slice const& x, Slice const& y, int32_t mode)
{
    return wasm_float::floatAddImpl(x, y, mode);
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatSubtract(
    Slice const& x,
    Slice const& y,
    int32_t mode)
{
    return wasm_float::floatSubtractImpl(x, y, mode);
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatMultiply(
    Slice const& x,
    Slice const& y,
    int32_t mode)
{
    return wasm_float::floatMultiplyImpl(x, y, mode);
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatDivide(Slice const& x, Slice const& y, int32_t mode)
{
    return wasm_float::floatDivideImpl(x, y, mode);
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatRoot(Slice const& x, int32_t n, int32_t mode)
{
    return wasm_float::floatRootImpl(x, n, mode);
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatPower(Slice const& x, int32_t n, int32_t mode)
{
    return wasm_float::floatPowerImpl(x, n, mode);
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatLog(Slice const& x, int32_t mode)
{
    return wasm_float::floatLogImpl(x, mode);
}

}  // namespace xrpl
