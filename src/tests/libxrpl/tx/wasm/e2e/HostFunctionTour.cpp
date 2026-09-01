#include <xrpl/protocol/TER.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/RealVmTest.h>

#include <string_view>

namespace xrpl::test {

// A single contract that tours several host functions end to end — a ledger-header read, the
// base fee, a hash, a keylet, and a data write — returning 1 only if every call succeeds.
struct HostFunctionTourE2e : RealVmTest
{
};

TEST_F(HostFunctionTourE2e, AContractTouringManyHostFunctionsSucceeds)
{
    // Each call must return >= 0 (a byte count, i.e. success); the guest returns the first
    // negative error code, or 1 if the whole tour succeeds. Output regions are disjoint so no
    // call clobbers another, and buffers are generous so exact value sizes don't matter.
    static constexpr auto kWat = std::string_view{R"wat(
(module
  (import "host_lib" "ldgr_index" (func $ldgr_index (param i32 i32) (result i32)))
  (import "host_lib" "base_fee" (func $base_fee (param i32 i32) (result i32)))
  (import "host_lib" "sha512_half" (func $sha512_half (param i32 i32 i32 i32) (result i32)))
  (import "host_lib" "accountroot_id" (func $accountroot_id (param i32 i32 i32 i32) (result i32)))
  (import "host_lib" "set_data" (func $set_data (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (func (export "escrow_finish") (result i32)
    (local $r i32)
    (local.set $r (call $ldgr_index (i32.const 0) (i32.const 32)))
    (if (i32.lt_s (local.get $r) (i32.const 0)) (then (return (local.get $r))))
    (local.set $r (call $base_fee (i32.const 32) (i32.const 32)))
    (if (i32.lt_s (local.get $r) (i32.const 0)) (then (return (local.get $r))))
    (local.set $r (call $sha512_half (i32.const 0) (i32.const 4) (i32.const 64) (i32.const 32)))
    (if (i32.lt_s (local.get $r) (i32.const 0)) (then (return (local.get $r))))
    (local.set $r (call $accountroot_id (i32.const 0) (i32.const 20) (i32.const 128) (i32.const 32)))
    (if (i32.lt_s (local.get $r) (i32.const 0)) (then (return (local.get $r))))
    (local.set $r (call $set_data (i32.const 0) (i32.const 8)))
    (if (i32.lt_s (local.get $r) (i32.const 0)) (then (return (local.get $r))))
    (i32.const 1)))
)wat"};

    auto const outcome = run(kWat);
    ASSERT_TRUE(outcome.has_value()) << transToken(outcome.error().ter);
    EXPECT_EQ(outcome->result, 1) << "every host call in the tour should have succeeded";
}

}  // namespace xrpl::test
