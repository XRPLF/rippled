#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestCallFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>

namespace xrpl::test {

using testing::Return;

// home_le_arr_len — a scalar field code in, the count answered directly.
//
// No out region and no guest memory read at all, so the only argument axis is the field code
// itself.
struct CurrentLedgerObjArrayLenGuest : GuestCallTest
{
    static constexpr std::int32_t kCount = 5;

    // A real field code, so the shim's `SField` lookup has something to find.
    static Arg
    field()
    {
        return Arg::scalar(sfBalance.getCode());
    }

    [[nodiscard]] static std::string
    watFor(Arg fieldArg)
    {
        return hostCallWat("home_le_arr_len", {fieldArg});
    }
};

TEST_F(CurrentLedgerObjArrayLenGuest, FieldCodeBecomesSFieldHostIsAskedFor)
{
    EXPECT_CALL(host, getCurrentLedgerObjArrayLen(testing::Ref(sfBalance)))
        .WillOnce(Return(kCount));

    EXPECT_EQ(hostAnswer(watFor(field())), kCount);
}

TEST_F(CurrentLedgerObjArrayLenGuest, UnknownFieldCodeIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getCurrentLedgerObjArrayLen).Times(0);

    auto const wat = watFor(Arg::scalar(0x7fff'0000));  // a type nothing is registered under
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidField));
}

// `NoArray` is what a field that is not an array actually answers, so it stands for the host
// error axis here rather than an arbitrary code.
TEST_F(CurrentLedgerObjArrayLenGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getCurrentLedgerObjArrayLen(testing::Ref(sfBalance)))
        .WillOnce(Return(std::unexpected(HostFunctionError::NoArray)));

    EXPECT_EQ(hostAnswer(watFor(field())), hfErrorToInt(HostFunctionError::NoArray));
}

TEST_F(CurrentLedgerObjArrayLenGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, getCurrentLedgerObjArrayLen(testing::Ref(sfBalance)))
        .WillOnce(testing::Throw(std::runtime_error{"current ledger obj array len came apart"}));

    auto const outcome = run(watFor(field()));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("getCurrentLedgerObjArrayLen"));
}

}  // namespace xrpl::test
