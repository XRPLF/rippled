#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestCallFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>

namespace xrpl::test {

using testing::Eq;
using testing::Return;

// cache_le — a region and a scalar in, and the answer returned directly.
//
// The shape with no out region, which is why this file has no buffer-fit case to make: the
// slot number crosses as the call's own `i32`. It stands for the array-length family,
// `nft_flags`, `float_cmp`, `check_sig` and `set_data`, which answer the same way.
struct CacheLedgerObjGuest : GuestCallTest
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

TEST_F(CacheLedgerObjGuest, ObjIdAndCacheIdxReachHostAndTheSlotIsTheAnswer)
{
    EXPECT_CALL(host, cacheLedgerObj(Eq(objId), kSlot)).WillOnce(Return(7));

    auto const wat = watFor(kObjId, kCacheIdx);
    EXPECT_EQ(hostAnswer(wat), 7);
}

TEST_F(CacheLedgerObjGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, cacheLedgerObj(Eq(objId), kSlot))
        .WillOnce(Return(std::unexpected(HostFunctionError::SlotsFull)));

    auto const wat = watFor(kObjId, kCacheIdx);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::SlotsFull));
}

TEST_F(CacheLedgerObjGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, cacheLedgerObj(Eq(objId), kSlot))
        .WillOnce(testing::Throw(std::runtime_error{"cache slot came apart"}));

    auto const outcome = run(watFor(kObjId, kCacheIdx));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("cacheLedgerObj"));
}

TEST_F(CacheLedgerObjGuest, ObjIdOfTheWrongLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, cacheLedgerObj).Times(0);

    auto const wat = watFor(Arg::region(kObjIdAt, kObjIdLen - 1), kCacheIdx);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(CacheLedgerObjGuest, InputRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, cacheLedgerObj).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kObjIdLen), kCacheIdx);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(CacheLedgerObjGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, cacheLedgerObj).Times(0);

    auto const wat = watFor(Arg::region(-1, kObjIdLen), kCacheIdx);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

// 0 asks the host to pick a free slot, so it is a meaningful argument rather than an absent
// one and must cross as itself.
TEST_F(CacheLedgerObjGuest, ZeroCacheIdxCrossesVerbatim)
{
    EXPECT_CALL(host, cacheLedgerObj(Eq(objId), 0)).WillOnce(Return(3));

    auto const wat = watFor(kObjId, Arg::scalar(0));
    EXPECT_EQ(hostAnswer(wat), 3);
}

// `cacheIdx` is the one scalar the ABI carries as signed: unlike a `seq`, it is not
// reinterpreted as unsigned on its way across, so a negative value reaches the host as
// itself and is the host's to refuse.
TEST_F(CacheLedgerObjGuest, NegativeCacheIdxCrossesVerbatim)
{
    EXPECT_CALL(host, cacheLedgerObj(Eq(objId), -1))
        .WillOnce(Return(std::unexpected(HostFunctionError::SlotOutRange)));

    auto const wat = watFor(kObjId, Arg::scalar(-1));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::SlotOutRange));
}

}  // namespace xrpl::test
