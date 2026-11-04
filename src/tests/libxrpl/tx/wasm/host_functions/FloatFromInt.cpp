#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/FloatFixture.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

namespace xrpl::test {

struct FloatFromIntImpl : FloatTest
{
};

TEST_F(FloatFromIntImpl, bad_mode_is_malformed)
{
    auto h = makeHost();
    expectError(h->floatFromInt(kMin64, -1), HostFunctionError::FloatInputMalformed);
    expectError(h->floatFromInt(kMin64, 4), HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatFromIntImpl, min_int)
{
    expectValue(makeHost()->floatFromInt(kMin64, 0), FloatTest::kIntMin);
}

TEST_F(FloatFromIntImpl, zero)
{
    expectValue(makeHost()->floatFromInt(0, 0), FloatTest::kIntZero);
}

TEST_F(FloatFromIntImpl, max_int)
{
    expectValue(makeHost()->floatFromInt(kMax64, 0), FloatTest::kIntMax);
}

}  // namespace xrpl::test
