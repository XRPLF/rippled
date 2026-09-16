#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostCallFixture.h>

#include <cstdint>
#include <expected>
#include <string>

namespace xrpl::test {

using testing::Return;

// home_le_field — a scalar field code in, bytes out.
struct CurrentLedgerObjFieldCall : HostCallTest
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

// The shim turns the guest's `i32` into the `SField` the C++ interface takes; asserting on
// the argument is what pins that translation rather than assuming it.
TEST_F(CurrentLedgerObjFieldCall, FieldCodeBecomesSFieldHostIsAskedFor)
{
    EXPECT_CALL(host, getCurrentLedgerObjField(testing::Ref(sfBalance)))
        .WillOnce(Return(Bytes{1, 2, 3}));

    auto const wat = watFor(field(), kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), 3) << "the length the host reported";
}

TEST_F(CurrentLedgerObjFieldCall, FieldBytesReachTheGuestsOutRegion)
{
    EXPECT_CALL(host, getCurrentLedgerObjField(testing::Ref(sfBalance)))
        .WillOnce(Return(Bytes{1, 2, 3, 4}));

    auto const wat = watFor(field(), kOut);
    EXPECT_EQ(hostAnswer(wat), 0x04030201) << "the four bytes, little-endian";
}

TEST_F(CurrentLedgerObjFieldCall, UnknownFieldCodeIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getCurrentLedgerObjField).Times(0);

    auto const wat = watFor(Arg::scalar(0x7fff'0000), kOut);  // a type nothing is registered under
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidField));
}

TEST_F(CurrentLedgerObjFieldCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getCurrentLedgerObjField)
        .WillOnce(Return(std::unexpected(HostFunctionError::FieldNotFound)));

    auto const wat = watFor(field(), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::FieldNotFound));
}

// The field cap bounds the status, not just the bytes: a host reporting a length past
// `kMaxWasmDataLength` is too large whatever the guest's buffer was.
TEST_F(CurrentLedgerObjFieldCall, FieldPastProtocolCapIsTooLarge)
{
    EXPECT_CALL(host, getCurrentLedgerObjField)
        .WillOnce(Return(Bytes(kMaxWasmDataLength + 1, 0xab)));

    auto const wat = watFor(field(), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::DataFieldTooLarge));
}

}  // namespace xrpl::test
