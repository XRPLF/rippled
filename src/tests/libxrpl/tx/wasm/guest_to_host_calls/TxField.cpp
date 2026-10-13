#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <string>

namespace xrpl::test {

using testing::Return;

// tx_field — a scalar field code in, bytes out.
struct TxFieldGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kOutAt = 64;
    static constexpr std::int32_t kOutLen = 8;

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

TEST_F(TxFieldGuest, field_bytes_reach_the_guests_out_region)
{
    EXPECT_CALL(host, getTxField(testing::Ref(sfBalance))).WillOnce(Return(value));

    auto const wat = watFor(field(), kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the first four bytes, little-endian";
}

}  // namespace xrpl::test
