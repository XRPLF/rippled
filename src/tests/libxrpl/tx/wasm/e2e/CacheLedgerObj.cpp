#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/RealHostFixture.h>
#include <tx/wasm/RealVmTest.h>
#include <tx/wasm/WasmRun.h>

#include <cstdint>
#include <string>

namespace xrpl::test {

// The keylet -> cache -> read round trip: the only place a contract's host calls depend on
// each other. Every other e2e case here is one call in isolation. This one is three, and each
// consumes what the last produced: `accountroot_id` computes a key into guest memory, `cache_le`
// hands those same bytes back to the host and answers with a slot number, and `le_field`
// uses that slot to read the object. The slot table is the one piece of host state that
// outlives a single call, so this is the only test at any layer that can catch the two ends
// of that state disagreeing — `host_calls` mocks the host, so its slot numbers are whatever
// the mock was told to return, and `host_functions` calls the impl directly, so its slots
// never cross the guest boundary at all.
struct CacheLedgerObjE2e : RealVmTest
{
};

TEST_F(CacheLedgerObjE2e, ContractComputesAKeyCachesTheObjectAndReadsItsField)
{
    auto const owner = fund("owner");
    auto const wat = std::string{R"wat(
(module
  (import "host_lib" "accountroot_id" (func $accountroot_id (param i32 i32 i32 i32) (result i32)))
  (import "host_lib" "cache_le" (func $cache_le (param i32 i32 i32) (result i32)))
  (import "host_lib" "le_field" (func $le_field (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) ")wat"} +
        watEscaped(RealHostFixture::toBytes(owner.id())) + R"wat(")
  (func (export "escrow_finish") (result i32)
    (local $slot i32)
    (local $r i32)
    ;; The account's AccountRoot keylet, computed by the host into offset 64.
    (local.set $r (call $accountroot_id (i32.const 0) (i32.const 20) (i32.const 64) (i32.const 32)))
    (if (i32.lt_s (local.get $r) (i32.const 0)) (then (return (local.get $r))))
    ;; Those same 32 bytes handed straight back: cache the object they name.
    (local.set $slot (call $cache_le (i32.const 64) (i32.const 32) (i32.const 0)))
    (if (i32.lt_s (local.get $slot) (i32.const 0)) (then (return (local.get $slot))))
    ;; And read a field of it through the slot the host just assigned.
    (call $le_field (local.get $slot) (i32.const )wat" +
        std::to_string(sfAccount.getCode()) + R"wat() (i32.const 128) (i32.const 32))))
)wat";

    auto const outcome = run(wat);
    ASSERT_TRUE(outcome.has_value()) << transToken(outcome.error().ter);
    // 20 bytes: the `sfAccount` the contract read back is the account it started from, so
    // the key it computed found the right object.
    EXPECT_EQ(
        outcome->result, static_cast<std::int32_t>(RealHostFixture::toBytes(owner.id()).size()));
}

}  // namespace xrpl::test
