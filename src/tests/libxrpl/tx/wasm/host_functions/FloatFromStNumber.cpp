#include <xrpl/basics/Number.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/FloatFixture.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

#include <cstdint>
#include <limits>

namespace xrpl::test {

struct FloatFromStNumberImpl : FloatTest
{
};

TEST_F(FloatFromStNumberImpl, bad_mode_is_malformed)
{
    auto h = makeHost();
    auto const n = STNumber{sfNumber, Number(123, 0)};
    expectError(h->floatFromSTNumber(n, -1), HostFunctionError::FloatInputMalformed);
    expectError(h->floatFromSTNumber(n, 4), HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatFromStNumberImpl, max_uint)
{
    auto const n = STNumber{
        sfNumber, Number(std::numeric_limits<std::uint64_t>::max(), 0, Number::Normalized{})};
    expectValue(makeHost()->floatFromSTNumber(n, 0), FloatTest::kUintMax);
}

TEST_F(FloatFromStNumberImpl, minus_max_exponent)
{
    auto const n = STNumber{sfNumber, Number(-1, Number::kMaxExponent + FloatTest::kNormalExp)};
    expectValue(makeHost()->floatFromSTNumber(n, 0), FloatTest::kMinusMaxExp);
}

}  // namespace xrpl::test
