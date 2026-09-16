#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestCallFixture.h>

#include <cstdint>
#include <expected>
#include <string>

namespace xrpl::test {

using testing::Return;

// home_le_field — a scalar field code in, bytes out.
struct CurrentLedgerObjFieldGuest : GuestCallTest
{
    static constexpr std::int32_t kOutAt = 0;
    static constexpr std::int32_t kOutLen = 32;

    static constexpr Arg kOut = Arg::outRegion(kOutAt, kOutLen);

    // A real field code, so the shim's `SField` lookup has something to find.
    static Arg
    field()
    {
        return Arg::scalar(sfBalance.getCode());
    }

    [[nodiscard]] static std::string
    watFor(Arg fieldArg, Arg outArg, Answer answer = Answer::WrittenBytes)
    {
        return hostCallWat("home_le_field", {fieldArg, outArg}, {}, answer);
    }
};

TEST_F(CurrentLedgerObjFieldGuest, FieldCodeBecomesSFieldHostIsAskedFor)
{
    EXPECT_CALL(host, getCurrentLedgerObjField(testing::Ref(sfBalance)))
        .WillOnce(Return(Bytes{1, 2, 3}));

    auto const wat = watFor(field(), kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), 3) << "the length the host reported";
}

TEST_F(CurrentLedgerObjFieldGuest, FieldBytesReachTheGuestsOutRegion)
{
    EXPECT_CALL(host, getCurrentLedgerObjField(testing::Ref(sfBalance)))
        .WillOnce(Return(Bytes{1, 2, 3, 4}));

    auto const wat = watFor(field(), kOut);
    EXPECT_EQ(hostAnswer(wat), 0x04030201) << "the four bytes, little-endian";
}

TEST_F(CurrentLedgerObjFieldGuest, UnknownFieldCodeIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getCurrentLedgerObjField).Times(0);

    auto const wat = watFor(Arg::scalar(0x7fff'0000), kOut);  // a type nothing is registered under
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidField));
}

TEST_F(CurrentLedgerObjFieldGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getCurrentLedgerObjField)
        .WillOnce(Return(std::unexpected(HostFunctionError::FieldNotFound)));

    auto const wat = watFor(field(), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::FieldNotFound));
}

// The field cap bounds the status, not just the bytes: a host reporting a length past
// `kMaxWasmDataLength` is too large whatever the guest's buffer was.
TEST_F(CurrentLedgerObjFieldGuest, FieldPastProtocolCapIsTooLarge)
{
    EXPECT_CALL(host, getCurrentLedgerObjField)
        .WillOnce(Return(Bytes(kMaxWasmDataLength + 1, 0xab)));

    auto const wat = watFor(field(), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::DataFieldTooLarge));
}

TEST_F(CurrentLedgerObjFieldGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, getCurrentLedgerObjField(testing::Ref(sfBalance)))
        .WillOnce(testing::Throw(std::runtime_error{"ledger object field came apart"}));

    auto const outcome = run(watFor(field(), kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("getCurrentLedgerObjField"));
}

TEST_F(CurrentLedgerObjFieldGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, getCurrentLedgerObjField(testing::Ref(sfBalance)))
        .WillOnce(Return(Bytes{1, 2, 3, 4}));

    auto const wat = watFor(field(), Arg::outRegion(kOutAt, 3));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(CurrentLedgerObjFieldGuest, OutRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getCurrentLedgerObjField).Times(0);

    auto const wat = watFor(field(), Arg::outRegion(kOnePage, kOutLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(CurrentLedgerObjFieldGuest, NegativeOutPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getCurrentLedgerObjField).Times(0);

    auto const wat = watFor(field(), Arg::outRegion(-1, kOutLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}
}  // namespace xrpl::test
