#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>
#include <tx/wasm/fixtures/MockHostFunctions.h>

#include <cstdint>
#include <string>

namespace xrpl::test {

using testing::Return;

// set_data — one region in, the byte count returned directly.
//
// The only mutating call with no out region, so the guest learns nothing but the count; what
// this layer has to show is that the bytes it wrote are the bytes the host is handed.
struct UpdateDataGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kDataAt = 0;
    static constexpr std::int32_t kDataLen = 5;

    static constexpr Arg kData = Arg::region(kDataAt, kDataLen);

    Bytes const data{'h', 'e', 'l', 'l', 'o'};

    [[nodiscard]] std::string
    watFor(Arg dataArg) const
    {
        return hostCallWat("set_data", {dataArg}, {{.at = kDataAt, .bytes = data}});
    }
};

TEST_F(UpdateDataGuest, guest_bytes_reach_host_and_the_stored_count_is_the_answer)
{
    EXPECT_CALL(host, updateData(BytesAre("hello"))).WillOnce(Return(kDataLen));

    auto const wat = watFor(kData);
    EXPECT_EQ(hostAnswer(wat), kDataLen);
}

}  // namespace xrpl::test
