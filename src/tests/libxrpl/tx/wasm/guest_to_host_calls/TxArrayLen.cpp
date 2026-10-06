#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
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

}  // namespace xrpl::test
