#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
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

TEST_F(CurrentLedgerObjFieldGuest, field_bytes_reach_the_guests_out_region)
{
    EXPECT_CALL(host, getCurrentLedgerObjField(testing::Ref(sfBalance)))
        .WillOnce(Return(Bytes{1, 2, 3, 4}));

    auto const wat = watFor(field(), kOut);
    EXPECT_EQ(hostAnswer(wat), 0x04030201) << "the four bytes, little-endian";
}

}  // namespace xrpl::test
