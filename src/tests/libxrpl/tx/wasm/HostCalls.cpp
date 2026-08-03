#include <tx/wasm/WasmFixture.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <expected>
#include <limits>
#include <string>

namespace xrpl::test {

namespace {

using testing::Return;

// The code a host error crosses as. The guest sees it as the host function's return value,
// so a soft failure is the contract's to interpret rather than the engine's to trap on.
std::int32_t
code(HostFunctionError error)
{
    return hfErrorToInt(error);
}

}  // namespace

// ---------------------------------------------------------------------------------------
// ldgr_index — no input, one scalar output
// ---------------------------------------------------------------------------------------

class LedgerSqnCall : public HostCallTest
{
protected:
    [[nodiscard]] std::string
    wat() const override
    {
        return std::string{R"wat(
(module
  (import "host_lib" "ldgr_index" (func $ldgr_index (param i32 i32) (result i32)))
  (memory (export "memory") 1)

  ;; Four bytes is what the value needs. Returns what the host wrote, or its error code.
  (func (export "escrow_finish") (result i32)
    (local $n i32)
    (local.set $n (call $ldgr_index (i32.const 0) (i32.const 4)))
    (select (local.get $n) (i32.load (i32.const 0)) (i32.lt_s (local.get $n) (i32.const 0))))

  ;; Two bytes is not enough for the value. Returns the host's code when memory is still
  ;; zero, or 1 if anything was written into it - so a refused write is visibly a refusal
  ;; and not a truncation.
  (func (export "into_two_bytes") (result i32)
    (local $n i32)
    (local.set $n (call $ldgr_index (i32.const 0) (i32.const 2)))
    (select (local.get $n) (i32.const 1) (i32.eqz (i32.load (i32.const 0))))))
)wat"};
    }
};

TEST_F(LedgerSqnCall, SequenceReachesGuestAsFourLittleEndianBytes)
{
    EXPECT_CALL(host_, getLedgerSqn()).WillOnce(Return(0x01020304u));

    // Read back with `i32.load`, which is little-endian by the wasm spec — so the value
    // arriving intact is the byte order being right.
    EXPECT_EQ(hostAnswer(), 0x01020304);
}

TEST_F(LedgerSqnCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host_, getLedgerSqn())
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    EXPECT_EQ(hostAnswer(), code(HostFunctionError::LedgerObjNotFound));
}

// The engine decides the fit, not the host: the host is never told the guest's capacity, it
// reports the value's true length and the engine turns a length past the buffer into
// `BufferTooSmall` — with nothing written.
TEST_F(LedgerSqnCall, BufferTooSmallIsRefusedWholeNotTruncated)
{
    EXPECT_CALL(host_, getLedgerSqn()).WillOnce(Return(0x01020304u));

    EXPECT_EQ(hostAnswer("into_two_bytes"), code(HostFunctionError::BufferTooSmall));
}

// ---------------------------------------------------------------------------------------
// home_le_field — a scalar field code in, bytes out
// ---------------------------------------------------------------------------------------

class CurrentLedgerObjFieldCall : public HostCallTest
{
protected:
    // The field code the guest asks for. A real one, so the shim's `SField` lookup has
    // something to find.
    std::int32_t fieldCode_ = sfBalance.getCode();

    [[nodiscard]] std::string
    wat() const override
    {
        return std::string{R"wat(
(module
  (import "host_lib" "home_le_field" (func $home_le_field (param i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (func (export "escrow_finish") (result i32)
    (call $home_le_field (i32.const )wat"} +
            std::to_string(fieldCode_) + R"wat() (i32.const 0) (i32.const 32))))
)wat";
    }
};

// The shim turns the guest's `i32` into the `SField` the C++ interface takes; asserting on
// the argument is what pins that translation rather than assuming it.
TEST_F(CurrentLedgerObjFieldCall, FieldCodeBecomesSFieldHostIsAskedFor)
{
    EXPECT_CALL(host_, getCurrentLedgerObjField(testing::Ref(sfBalance)))
        .WillOnce(Return(Bytes{1, 2, 3}));

    EXPECT_EQ(hostAnswer(), 3) << "the length the host reported";
}

TEST_F(CurrentLedgerObjFieldCall, UnknownFieldCodeIsRefusedWithoutAskingHost)
{
    fieldCode_ = 0x7fff'0000;  // a type nothing is registered under
    EXPECT_CALL(host_, getCurrentLedgerObjField).Times(0);

    EXPECT_EQ(hostAnswer(), code(HostFunctionError::InvalidField));
}

TEST_F(CurrentLedgerObjFieldCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host_, getCurrentLedgerObjField)
        .WillOnce(Return(std::unexpected(HostFunctionError::FieldNotFound)));

    EXPECT_EQ(hostAnswer(), code(HostFunctionError::FieldNotFound));
}

// The field cap bounds the status, not just the bytes: a host reporting a length past
// `kMaxWasmDataLength` is too large whatever the guest's buffer was.
TEST_F(CurrentLedgerObjFieldCall, FieldPastProtocolCapIsTooLarge)
{
    EXPECT_CALL(host_, getCurrentLedgerObjField)
        .WillOnce(Return(Bytes(kMaxWasmDataLength + 1, 0xab)));

    EXPECT_EQ(hostAnswer(), code(HostFunctionError::DataFieldTooLarge));
}

// ---------------------------------------------------------------------------------------
// sha512_half — bytes in and bytes out, the shape that needs the engine's output buffer
// ---------------------------------------------------------------------------------------

class Sha512HalfCall : public HostCallTest
{
protected:
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
    EXPECT_CALL(host_, computeSha512HalfHash(BytesAre("abc"))).WillOnce(Return(digest()));

    EXPECT_EQ(hostAnswer(), 0x0a0b0c0d) << "the digest's first four bytes, little-endian";
}

TEST_F(Sha512HalfCall, DigestIsThirtyTwoBytes)
{
    EXPECT_CALL(host_, computeSha512HalfHash).WillOnce(Return(digest()));

    EXPECT_EQ(hostAnswer("digest_length"), 32);
}

TEST_F(Sha512HalfCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host_, computeSha512HalfHash)
        .WillOnce(Return(std::unexpected(HostFunctionError::InvalidParams)));

    EXPECT_EQ(hostAnswer(), code(HostFunctionError::InvalidParams));
}

// ---------------------------------------------------------------------------------------
// trace — two byte inputs and a flag, no output
// ---------------------------------------------------------------------------------------

class TraceCall : public HostCallTest
{
protected:
    [[nodiscard]] std::string
    wat() const override
    {
        return std::string{R"wat(
(module
  (import "host_lib" "trace" (func $trace (param i32 i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "note")
  (data (i32.const 16) "\07\08")

  (func (export "escrow_finish") (result i32)
    (call $trace (i32.const 0) (i32.const 4) (i32.const 16) (i32.const 2) (i32.const 1)))

  (func (export "not_as_hex") (result i32)
    (call $trace (i32.const 0) (i32.const 4) (i32.const 16) (i32.const 2) (i32.const 0))))
)wat"};
    }
};

// Two borrowed regions in one call, which is the shape a single-input helper could not
// express — so this pins that both arrive intact, and the flag with them.
TEST_F(TraceCall, MessageDataAndFlagAllArrive)
{
    EXPECT_CALL(host_, trace(std::string_view("note"), BytesAre("\x07\x08"), true))
        .WillOnce(Return(0));

    EXPECT_EQ(hostAnswer(), 0) << "a call with nothing to report answers 0";
}

TEST_F(TraceCall, HexFlagIsGuestsToChoose)
{
    EXPECT_CALL(host_, trace(testing::_, testing::_, false)).WillOnce(Return(0));

    EXPECT_EQ(hostAnswer("not_as_hex"), 0);
}

TEST_F(TraceCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host_, trace).WillOnce(Return(std::unexpected(HostFunctionError::InvalidParams)));

    EXPECT_EQ(hostAnswer(), code(HostFunctionError::InvalidParams));
}

// ---------------------------------------------------------------------------------------
// trace_num — a string and an i64, the ABI's only 64-bit parameter
// ---------------------------------------------------------------------------------------

class TraceNumCall : public HostCallTest
{
protected:
    [[nodiscard]] std::string
    wat() const override
    {
        return std::string{R"wat(
(module
  (import "host_lib" "trace_num" (func $trace_num (param i32 i32 i64) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "count")

  (func (export "escrow_finish") (result i32)
    (call $trace_num (i32.const 0) (i32.const 5) (i64.const -9223372036854775808))))
)wat"};
    }
};

// The extreme value on purpose: an `i64` that a truncating or sign-losing conversion anywhere
// on the wire would visibly mangle.
TEST_F(TraceNumCall, I64ArrivesWholeIncludingMostNegativeValue)
{
    EXPECT_CALL(
        host_,
        traceNum(std::string_view("count"), std::numeric_limits<std::int64_t>::min()))
        .WillOnce(Return(0));

    EXPECT_EQ(hostAnswer(), 0);
}

TEST_F(TraceNumCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host_, traceNum)
        .WillOnce(Return(std::unexpected(HostFunctionError::IndexOutOfBounds)));

    EXPECT_EQ(hostAnswer(), code(HostFunctionError::IndexOutOfBounds));
}

}  // namespace xrpl::test
