#include <xrpl/basics/base_uint.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <string>

namespace xrpl::test {

using testing::Eq;
using testing::Return;

// cache_le — a region and a scalar in, and the answer returned directly.
//
// The shape with no out region, which is why this file has no buffer-fit case to make: the
// slot number crosses as the call's own `i32`. It stands for the array-length family,
// `nft_flags`, `float_cmp`, `check_sig` and `set_data`, which answer the same way.
struct CacheLedgerObjGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kObjIdAt = 0;
    static constexpr std::int32_t kObjIdLen = static_cast<std::int32_t>(uint256::size());
    static constexpr std::int32_t kSlot = 5;

    static constexpr Arg kObjId = Arg::region(kObjIdAt, kObjIdLen);
    static constexpr Arg kCacheIdx = Arg::scalar(kSlot);

    Bytes const objIdBytes = Bytes(uint256::size(), 0x33);
    uint256 const objId = uint256::fromVoid(objIdBytes.data());

    [[nodiscard]] std::string
    watFor(Arg objIdArg, Arg cacheIdxArg) const
    {
        return hostCallWat(
            "cache_le", {objIdArg, cacheIdxArg}, {{.at = kObjIdAt, .bytes = objIdBytes}});
    }
};

TEST_F(CacheLedgerObjGuest, obj_id_and_cache_idx_reach_host_and_the_slot_is_the_answer)
{
    EXPECT_CALL(host, cacheLedgerObj(Eq(objId), kSlot)).WillOnce(Return(7));

    auto const wat = watFor(kObjId, kCacheIdx);
    EXPECT_EQ(hostAnswer(wat), 7);
}

}  // namespace xrpl::test
