#include <xrpl/basics/Slice.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/FloatFixture.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

#include <cstdint>

namespace xrpl::test {

struct FloatToIntImpl : FloatTest
{
};

TEST_F(FloatToIntImpl, bad_mode_is_malformed)
{
    auto h = makeHost();
    expectError(h->floatToInt(slice(FloatTest::kOne), -1), HostFunctionError::FloatInputMalformed);
    expectError(h->floatToInt(slice(FloatTest::kOne), 4), HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatToIntImpl, malformed_inputs)
{
    expectError(makeHost()->floatToInt(Slice{}, 0), HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatToIntImpl, zero)
{
    expectValue(makeHost()->floatToInt(slice(FloatTest::kIntZero), 0), std::int64_t{0});
}

TEST_F(FloatToIntImpl, one)
{
    expectValue(makeHost()->floatToInt(slice(FloatTest::kOne), 0), std::int64_t{1});
}

TEST_F(FloatToIntImpl, minus_one)
{
    expectValue(makeHost()->floatToInt(slice(FloatTest::kMinusOne), 0), std::int64_t{-1});
}

TEST_F(FloatToIntImpl, max)
{
    expectValue(makeHost()->floatToInt(slice(FloatTest::kIntMax), 0), kMax64);
}

TEST_F(FloatToIntImpl, min)
{
    // floatIntMin rounds to -(2^63-1), i.e. -kMax64.
    expectValue(makeHost()->floatToInt(slice(FloatTest::kIntMin), 0), -kMax64);
}

TEST_F(FloatToIntImpl, overflows_int64_is_computation_error)
{
    expectError(
        makeHost()->floatToInt(slice(FloatTest::kUintMax), 0),
        HostFunctionError::FloatComputationError);
}

TEST_F(FloatToIntImpl, pi_rounds_by_mode)
{
    auto h = makeHost();
    expectValue(h->floatToInt(slice(FloatTest::kPi), 0), std::int64_t{3});  // ToNearest
    expectValue(h->floatToInt(slice(FloatTest::kPi), 1), std::int64_t{3});  // TowardsZero
    expectValue(h->floatToInt(slice(FloatTest::kPi), 2), std::int64_t{3});  // Downward
    expectValue(h->floatToInt(slice(FloatTest::kPi), 3), std::int64_t{4});  // Upward
}

}  // namespace xrpl::test
