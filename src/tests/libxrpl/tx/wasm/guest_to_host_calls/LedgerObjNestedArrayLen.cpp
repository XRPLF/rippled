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

// le_inner_arr_len — a cache slot and a locator region in, the count answered directly. No out
// region, so no buffer-fit axis.
//
// The slot and the locator's pointer and length are three bare `i32`s on the wire, so each
// carries a value none of the others could be mistaken for.
struct LedgerObjNestedArrayLenGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kLocatorAt = 16;
    static constexpr std::int32_t kLocatorLen = 12;
    static constexpr std::int32_t kCount = 5;
    static constexpr std::int32_t kSlot = 7;

    static constexpr Arg kCacheIdx = Arg::scalar(kSlot);
    static constexpr Arg kLocator = Arg::region(kLocatorAt, kLocatorLen);

    // A negative step, so a sign or byte-order mistake in the wire form shows up.
    std::vector<std::int32_t> const steps{5, -12, 130};
    Bytes const locatorBytes = bytesOfSteps(steps);

    [[nodiscard]] std::string
    watFor(Arg cacheIdxArg, Arg locatorArg) const
    {
        return hostCallWat(
            "le_inner_arr_len",
            {cacheIdxArg, locatorArg},
            {{.at = kLocatorAt, .bytes = locatorBytes}});
    }
};

TEST_F(LedgerObjNestedArrayLenGuest, slot_and_locator_reach_host_in_order_and_count_comes_back)
{
    EXPECT_CALL(host, getLedgerObjNestedArrayLen(kSlot, LocatorEquals(steps)))
        .WillOnce(Return(kCount));

    EXPECT_EQ(hostAnswer(watFor(kCacheIdx, kLocator)), kCount);
}

}  // namespace xrpl::test
