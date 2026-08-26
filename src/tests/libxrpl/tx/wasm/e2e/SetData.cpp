#include <xrpl/protocol/TER.h>

#include <gtest/gtest.h>
#include <tx/wasm/RealVmTest.h>

#include <string_view>

namespace xrpl::test {

// A contract writes its data field end to end.
struct SetDataE2e : RealVmTest
{
};

TEST_F(SetDataE2e, ContractWritesItsData)
{
    // `set_data` over 8 bytes of (zero-initialized) memory returns the byte count it stored.
    static constexpr auto kWat = std::string_view{R"wat(
(module
  (import "host_lib" "set_data" (func $set_data (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (func (export "escrow_finish") (result i32)
    (call $set_data (i32.const 0) (i32.const 8))))
)wat"};

    auto const outcome = run(kWat);
    ASSERT_TRUE(outcome.has_value()) << transToken(outcome.error().ter);
    EXPECT_EQ(outcome->result, 8);
}

}  // namespace xrpl::test
