#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <string>

namespace xrpl::test {

using testing::Return;

// parent_ldgr_time — no input, one scalar written out.
struct ParentLedgerTimeGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kOutAt = 0;
    static constexpr std::int32_t kTimeLen = 4;

    static constexpr Arg kOut = Arg::outRegion(kOutAt, kTimeLen);

    static constexpr std::uint32_t kCloseTime = 0x5a6b7c8d;

    [[nodiscard]] static std::string
    watFor(Arg outArg, Answer answer = Answer::WrittenBytes)
    {
        return hostCallWat("parent_ldgr_time", {outArg}, {}, answer);
    }
};

TEST_F(ParentLedgerTimeGuest, close_time_reaches_guest_as_four_little_endian_bytes)
{
    EXPECT_CALL(host, getParentLedgerTime()).WillOnce(Return(kCloseTime));

    auto const wat = watFor(kOut);
    EXPECT_EQ(hostAnswer(wat), static_cast<std::int32_t>(kCloseTime));
}

}  // namespace xrpl::test
