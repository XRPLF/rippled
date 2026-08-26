#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/RealVmTest.h>

#include <cstdint>
#include <string>

namespace xrpl::test {

// The error channel, driven by a real failure rather than a mock's canned one.
//
// Every other e2e case here proves a success path. But a contract spends most of its life
// reacting to codes, and the path a *real* error takes is different from the one a mock
// error takes: the impl returns a `HostFunctionError`, `HostContext` turns it into a wire
// code, and the engine hands that back to the guest as a negative i32 without disturbing the
// run. `host_calls` proves the middle step against a mock that was *told* to fail; nothing
// until now has proved that a real impl's real failure comes out the far end intact.
//
// The distinction matters because the two halves are separately enumerated: a code the impl
// can return but the bridge does not map, or maps to a different number, is invisible to
// both of the other layers.
struct HostErrorE2e : RealVmTest
{
};

TEST_F(HostErrorE2e, ARealHostErrorReachesTheGuestAsItsWireCode)
{
    // The contract runs against an account root, then asks it for `sfMemoData` — a field
    // that object does not carry. The impl genuinely fails to find it, so the code the
    // guest reads was produced by the real lookup rather than staged.
    auto const owner = fund("owner");

    auto const wat = std::string{R"wat(
(module
  (import "host_lib" "home_le_field" (func $home_le_field (param i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (func (export "escrow_finish") (result i32)
    (call $home_le_field (i32.const )wat"} +
        std::to_string(sfMemoData.getCode()) + R"wat() (i32.const 0) (i32.const 32))))
)wat";

    auto const outcome = run(wat, keylet::account(owner.id()));

    // The run itself succeeds: a soft host error is an answer to the contract, not a fault
    // in it. Reporting it as a failed run would be the interesting bug here.
    ASSERT_TRUE(outcome.has_value()) << transToken(outcome.error().ter);
    EXPECT_EQ(outcome->result, static_cast<std::int32_t>(HostFunctionError::FieldNotFound));
}

}  // namespace xrpl::test
