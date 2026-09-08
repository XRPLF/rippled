#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/FloatFixture.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

#include <cstdint>
#include <limits>

namespace xrpl::test {

struct FloatFromUintImpl : FloatTest
{
    static constexpr std::uint64_t kMaxU64 = std::numeric_limits<std::uint64_t>::max();
};

TEST_F(FloatFromUintImpl, BadModeIsMalformed)
{
    auto h = makeHost();
    expectError(h->floatFromUint(0, -1), HostFunctionError::FloatInputMalformed);
    expectError(h->floatFromUint(0, 4), HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatFromUintImpl, Zero)
{
    expectValue(makeHost()->floatFromUint(0, 0), FloatTest::kIntZero);
}

TEST_F(FloatFromUintImpl, MaxUint)
{
    expectValue(makeHost()->floatFromUint(kMaxU64, 0), FloatTest::kUintMax);
}

}  // namespace xrpl::test
