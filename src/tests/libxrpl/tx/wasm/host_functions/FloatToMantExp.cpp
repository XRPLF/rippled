#include <xrpl/basics/Number.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/FloatFixture.h>
#include <tx/wasm/RealHostFixture.h>

#include <cstdint>
#include <limits>

namespace xrpl::test {

struct FloatToMantExpImpl : FloatTest
{
    static constexpr std::int32_t kExpMin = std::numeric_limits<std::int32_t>::min();

    static FloatPair
    pair(std::int64_t mantissa, std::int32_t exponent)
    {
        return FloatPair{mantissa, exponent};
    }
};

TEST_F(FloatToMantExpImpl, MalformedInput)
{
    expectError(makeHost()->floatToMantExp(Slice{}), HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatToMantExpImpl, Zero)
{
    expectValue(makeHost()->floatToMantExp(slice(floats::kIntZero)), pair(0, kExpMin));
}

TEST_F(FloatToMantExpImpl, One)
{
    expectValue(
        makeHost()->floatToMantExp(slice(floats::kOne)),
        pair(1'000'000'000'000'000'000, -floats::kNormalExp));
}

TEST_F(FloatToMantExpImpl, MinusOne)
{
    expectValue(
        makeHost()->floatToMantExp(slice(floats::kMinusOne)),
        pair(-1'000'000'000'000'000'000, -floats::kNormalExp));
}

TEST_F(FloatToMantExpImpl, Ten)
{
    expectValue(
        makeHost()->floatToMantExp(slice(floats::kTen)),
        pair(1'000'000'000'000'000'000, -floats::kNormalExp + 1));
}

TEST_F(FloatToMantExpImpl, Pi)
{
    expectValue(
        makeHost()->floatToMantExp(slice(floats::kPi)),
        pair(3'141'592'653'589'793'000, -floats::kNormalExp));
}

TEST_F(FloatToMantExpImpl, IntMax)
{
    expectValue(makeHost()->floatToMantExp(slice(floats::kIntMax)), pair(kMax64, 0));
}

TEST_F(FloatToMantExpImpl, IntMin)
{
    expectValue(makeHost()->floatToMantExp(slice(floats::kIntMin)), pair(-kMax64, 0));
}

TEST_F(FloatToMantExpImpl, Max)
{
    expectValue(
        makeHost()->floatToMantExp(slice(floats::kMax)),
        pair(Number::kMaxRep, Number::kMaxExponent));
}

}  // namespace xrpl::test
