#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <expected>
#include <string>

namespace xrpl::test {

using testing::Return;

// home_le_field — a scalar field code in, bytes out.
struct CurrentLedgerObjFieldGuest : GuestToHostCallTest
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

TEST_F(CurrentLedgerObjFieldGuest, field_code_becomes_sfield_host_is_asked_for)
{
    EXPECT_CALL(host, getCurrentLedgerObjField(testing::Ref(sfBalance)))
        .WillOnce(Return(Bytes{1, 2, 3}));

    auto const wat = watFor(field(), kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), 3) << "the length the host reported";
}

TEST_F(CurrentLedgerObjFieldGuest, field_bytes_reach_the_guests_out_region)
{
    EXPECT_CALL(host, getCurrentLedgerObjField(testing::Ref(sfBalance)))
        .WillOnce(Return(Bytes{1, 2, 3, 4}));

    auto const wat = watFor(field(), kOut);
    EXPECT_EQ(hostAnswer(wat), 0x04030201) << "the four bytes, little-endian";
}

TEST_F(CurrentLedgerObjFieldGuest, unknown_field_code_is_refused_without_asking_host)
{
    EXPECT_CALL(host, getCurrentLedgerObjField).Times(0);

    auto const wat = watFor(Arg::scalar(0x7fff'0000), kOut);  // a type nothing is registered under
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidField));
}

TEST_F(CurrentLedgerObjFieldGuest, host_error_becomes_contract_return_value)
{
    EXPECT_CALL(host, getCurrentLedgerObjField)
        .WillOnce(Return(std::unexpected(HostFunctionError::FieldNotFound)));

    auto const wat = watFor(field(), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::FieldNotFound));
}

TEST_F(CurrentLedgerObjFieldGuest, host_exception_stops_the_run_and_is_logged)
{
    EXPECT_CALL(host, getCurrentLedgerObjField(testing::Ref(sfBalance)))
        .WillOnce(testing::Throw(std::runtime_error{"ledger object field came apart"}));

    auto const outcome = run(watFor(field(), kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("getCurrentLedgerObjField"));
}

}  // namespace xrpl::test
