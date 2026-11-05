#include <xrpl/protocol/SField.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <string>

namespace xrpl::test {

using testing::Return;

// le_arr_len — a cache slot and a field code in, the count answered directly.
//
// Both are bare `i32`s on the wire, so the slot and the field code are values that cannot be
// confused for one another: a swap in `CxxHost`'s forward would otherwise be invisible.
struct LedgerObjArrayLenGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kCount = 5;
    static constexpr std::int32_t kSlot = 7;

    static constexpr Arg kCacheIdx = Arg::scalar(kSlot);

    // A real field code, so the shim's `SField` lookup has something to find.
    static Arg
    field()
    {
        return Arg::scalar(sfBalance.getCode());
    }

    [[nodiscard]] static std::string
    watFor(Arg cacheIdxArg, Arg fieldArg)
    {
        return hostCallWat("le_arr_len", {cacheIdxArg, fieldArg});
    }
};

TEST_F(LedgerObjArrayLenGuest, slot_and_field_code_reach_host_in_order)
{
    EXPECT_CALL(host, getLedgerObjArrayLen(kSlot, testing::Ref(sfBalance)))
        .WillOnce(Return(kCount));

    EXPECT_EQ(hostAnswer(watFor(kCacheIdx, field())), kCount);
}

}  // namespace xrpl::test
