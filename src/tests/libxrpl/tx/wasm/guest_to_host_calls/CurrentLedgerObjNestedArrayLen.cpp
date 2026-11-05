#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/BytesHelpers.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <string>
#include <vector>

namespace xrpl::test {

using testing::Return;

// home_le_inner_arr_len — a locator region in, the count answered directly. No out region, so
// no buffer-fit axis.
struct CurrentLedgerObjNestedArrayLenGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kLocatorAt = 16;
    static constexpr std::int32_t kLocatorLen = 12;
    static constexpr std::int32_t kCount = 5;

    static constexpr Arg kLocator = Arg::region(kLocatorAt, kLocatorLen);

    // A negative step, so a sign or byte-order mistake in the wire form shows up.
    std::vector<std::int32_t> const steps{5, -12, 130};
    Bytes const locatorBytes = bytesOfSteps(steps);

    [[nodiscard]] std::string
    watFor(Arg locatorArg) const
    {
        return hostCallWat(
            "home_le_inner_arr_len", {locatorArg}, {{.at = kLocatorAt, .bytes = locatorBytes}});
    }
};

TEST_F(CurrentLedgerObjNestedArrayLenGuest, locator_steps_reach_host_and_count_comes_back)
{
    EXPECT_CALL(host, getCurrentLedgerObjNestedArrayLen(LocatorEquals(steps)))
        .WillOnce(Return(kCount));

    EXPECT_EQ(hostAnswer(watFor(kLocator)), kCount);
}

}  // namespace xrpl::test
