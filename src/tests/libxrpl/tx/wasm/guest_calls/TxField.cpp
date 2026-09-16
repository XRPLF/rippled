#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostCallFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>

namespace xrpl::test {

using testing::Return;

// tx_field — a scalar field code in, bytes out.
//
// `abi.rs`'s `write_into` serves it, so the out region is judged *before* the host is asked:
// an unreachable region never reaches the mock, while a too-short one does, the fit being
// decided against the length the host reports.
struct TxFieldGuest : HostCallTest
{
    static constexpr std::int32_t kOutAt = 64;
    static constexpr std::int32_t kOutLen = 8;
    static constexpr std::int32_t kValueLen = 6;

    static constexpr Arg kOut = Arg::outRegion(kOutAt, kOutLen);

    // Six bytes, the first four distinctive so the guest's `i32.load` of them cannot pass by
    // accident and the reported length cannot be mistaken for that load's width.
    Bytes const value{0x0d, 0x0c, 0x0b, 0x0a, 0xee, 0xff};

    // A real field code, so the shim's `SField` lookup has something to find.
    static Arg
    field()
    {
        return Arg::scalar(sfBalance.getCode());
    }

    [[nodiscard]] static std::string
    watFor(Arg fieldArg, Arg outArg, Answer answer = Answer::WrittenBytes)
    {
        return hostCallWat("tx_field", {fieldArg, outArg}, {}, answer);
    }
};

// The shim turns the guest's `i32` into the `SField` the C++ interface takes; asserting on
// the argument is what pins that translation rather than assuming it.
TEST_F(TxFieldGuest, FieldCodeBecomesSFieldHostIsAskedFor)
{
    EXPECT_CALL(host, getTxField(testing::Ref(sfBalance))).WillOnce(Return(value));

    auto const wat = watFor(field(), kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kValueLen) << "the length the host reported";
}

TEST_F(TxFieldGuest, FieldBytesReachTheGuestsOutRegion)
{
    EXPECT_CALL(host, getTxField(testing::Ref(sfBalance))).WillOnce(Return(value));

    auto const wat = watFor(field(), kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the first four bytes, little-endian";
}

TEST_F(TxFieldGuest, UnknownFieldCodeIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getTxField).Times(0);

    auto const wat = watFor(Arg::scalar(0x7fff'0000), kOut);  // a type nothing is registered under
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidField));
}

TEST_F(TxFieldGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getTxField(testing::Ref(sfBalance)))
        .WillOnce(Return(std::unexpected(HostFunctionError::FieldNotFound)));

    auto const wat = watFor(field(), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::FieldNotFound));
}

// `guarded` turns the throw into `InternalFatal`, which the engine treats as fatal rather
// than passing back: the run ends, and the guest never resumes to read it.
TEST_F(TxFieldGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, getTxField(testing::Ref(sfBalance)))
        .WillOnce(testing::Throw(std::runtime_error{"tx field came apart"}));

    auto const outcome = callHost(watFor(field(), kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("getTxField"));
}

TEST_F(TxFieldGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, getTxField(testing::Ref(sfBalance))).WillOnce(Return(value));

    auto const wat = watFor(field(), Arg::outRegion(kOutAt, kValueLen - 1));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(TxFieldGuest, OutRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getTxField).Times(0);

    auto const wat = watFor(field(), Arg::outRegion(kOnePage, kOutLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(TxFieldGuest, NegativeOutPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getTxField).Times(0);

    auto const wat = watFor(field(), Arg::outRegion(-1, kOutLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
