#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>

namespace xrpl::test {

using testing::Return;

// tx_arr_len — a scalar field code in, the count answered directly.
//
// No out region and no guest memory read at all, so the only argument axis is the field code
// itself.
struct TxArrayLenGuest : GuestToHostCallTest
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
        return hostCallWat("tx_arr_len", {fieldArg});
    }
};

TEST_F(TxArrayLenGuest, field_code_becomes_sfield_host_is_asked_for)
{
    EXPECT_CALL(host, getTxArrayLen(testing::Ref(sfBalance))).WillOnce(Return(kCount));

    EXPECT_EQ(hostAnswer(watFor(field())), kCount);
}

TEST_F(TxArrayLenGuest, unknown_field_code_is_refused_without_asking_host)
{
    EXPECT_CALL(host, getTxArrayLen).Times(0);

    auto const wat = watFor(Arg::scalar(0x7fff'0000));  // a type nothing is registered under
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidField));
}

// `NoArray` is what a field that is not an array actually answers, so it stands for the host
// error axis here rather than an arbitrary code.
TEST_F(TxArrayLenGuest, host_error_becomes_contract_return_value)
{
    EXPECT_CALL(host, getTxArrayLen(testing::Ref(sfBalance)))
        .WillOnce(Return(std::unexpected(HostFunctionError::NoArray)));

    EXPECT_EQ(hostAnswer(watFor(field())), hfErrorToInt(HostFunctionError::NoArray));
}

TEST_F(TxArrayLenGuest, host_exception_stops_the_run_and_is_logged)
{
    EXPECT_CALL(host, getTxArrayLen(testing::Ref(sfBalance)))
        .WillOnce(testing::Throw(std::runtime_error{"tx array len came apart"}));

    auto const outcome = run(watFor(field()));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("getTxArrayLen"));
}

}  // namespace xrpl::test
