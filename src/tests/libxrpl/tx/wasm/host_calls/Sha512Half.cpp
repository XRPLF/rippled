#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/MockHostFunctions.h>
#include <tx/wasm/WasmFixture.h>

#include <expected>
#include <string>

namespace xrpl::test {

using testing::Return;

// sha512_half — bytes in and bytes out, the shape that needs the engine's output buffer.
struct Sha512HalfCall : HostCallTest
{
    [[nodiscard]] std::string
    wat() const override
    {
        return std::string{R"wat(
(module
  (import "host_lib" "sha512_half" (func $sha512_half (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 64) "abc")

  ;; Hashes the three bytes at 64 into the 32 at 0, then returns the first four bytes of the
  ;; digest so the answer is shown to have arrived, not just been counted.
  (func (export "escrow_finish") (result i32)
    (local $n i32)
    (local.set $n (call $sha512_half (i32.const 64) (i32.const 3) (i32.const 0) (i32.const 32)))
    (select (local.get $n) (i32.load (i32.const 0)) (i32.lt_s (local.get $n) (i32.const 0))))

  ;; Reports the length the host gave, for the cases where the digest itself is not the point.
  (func (export "digest_length") (result i32)
    (call $sha512_half (i32.const 64) (i32.const 3) (i32.const 0) (i32.const 32))))
)wat"};
    }

    // A digest whose first four bytes are distinctive, so the load below cannot pass by
    // accident.
    static Hash
    digest()
    {
        Hash value;
        value.begin()[0] = 0x0d;
        value.begin()[1] = 0x0c;
        value.begin()[2] = 0x0b;
        value.begin()[3] = 0x0a;
        return value;
    }
};

// Both directions in one call: the guest's bytes reach the host borrowed from its memory, and
// the answer comes back into the same memory through the engine's buffer.
TEST_F(Sha512HalfCall, GuestBytesReachHostAndDigestComesBack)
{
    EXPECT_CALL(host, computeSha512HalfHash(BytesAre("abc"))).WillOnce(Return(digest()));

    EXPECT_EQ(hostAnswer(), 0x0a0b0c0d) << "the digest's first four bytes, little-endian";
}

TEST_F(Sha512HalfCall, DigestIsThirtyTwoBytes)
{
    EXPECT_CALL(host, computeSha512HalfHash).WillOnce(Return(digest()));

    EXPECT_EQ(hostAnswer("digest_length"), 32);
}

TEST_F(Sha512HalfCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, computeSha512HalfHash)
        .WillOnce(Return(std::unexpected(HostFunctionError::InvalidParams)));

    EXPECT_EQ(hostAnswer(), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
