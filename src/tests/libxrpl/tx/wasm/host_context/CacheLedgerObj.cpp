#include <xrpl/basics/base_uint.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostContextFixture.h>

#include <expected>
#include <stdexcept>

namespace xrpl::test {

// `cacheLedgerObj` mutates the host's slot table, so it is non-`const`; it answers the slot
// used directly, with no out region.
struct CacheLedgerObjCall : HostContextTest
{
    Bytes const objIdBytes = Bytes(uint256::size(), 0x33);
    uint256 const objId = uint256::fromVoid(objIdBytes.data());
};

TEST_F(CacheLedgerObjCall, ObjIdAndCacheIdxForwardedSlotIsReturned)
{
    EXPECT_CALL(host, cacheLedgerObj(testing::Eq(objId), 5)).WillOnce(testing::Return(7));

    EXPECT_EQ(hostContext.cacheLedgerObj(bytesOf(objIdBytes), 5), 7);
}

TEST_F(CacheLedgerObjCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, cacheLedgerObj(testing::Eq(objId), 5))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::SlotsFull)));

    EXPECT_EQ(
        hostContext.cacheLedgerObj(bytesOf(objIdBytes), 5),
        hfErrorToInt(HostFunctionError::SlotsFull));
}

TEST_F(CacheLedgerObjCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, cacheLedgerObj(testing::Eq(objId), 5))
        .WillOnce(testing::Throw(std::runtime_error{"cache slot came apart"}));

    EXPECT_EQ(
        hostContext.cacheLedgerObj(bytesOf(objIdBytes), 5),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("cache slot came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("cacheLedgerObj"));
}

TEST_F(CacheLedgerObjCall, MalformedObjIdIsRefusedWithoutAskingHost)
{
    Bytes const malformed(uint256::size() - 1, 0x33);
    EXPECT_CALL(host, cacheLedgerObj).Times(0);

    EXPECT_EQ(
        hostContext.cacheLedgerObj(bytesOf(malformed), 5),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

// 0 selects a free slot at the host - a meaningful argument here, not an absent one - and must
// still cross unchanged.
TEST_F(CacheLedgerObjCall, ZeroCacheIdxIsForwardedVerbatim)
{
    EXPECT_CALL(host, cacheLedgerObj(testing::Eq(objId), 0)).WillOnce(testing::Return(0));

    EXPECT_EQ(hostContext.cacheLedgerObj(bytesOf(objIdBytes), 0), 0);
}

// Unlike `seq` elsewhere in this file's shape family, `cacheIdx` is not reinterpreted as
// unsigned: a negative value reaches the host as itself.
TEST_F(CacheLedgerObjCall, NegativeCacheIdxIsForwardedVerbatim)
{
    EXPECT_CALL(host, cacheLedgerObj(testing::Eq(objId), -1))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::SlotOutRange)));

    EXPECT_EQ(
        hostContext.cacheLedgerObj(bytesOf(objIdBytes), -1),
        hfErrorToInt(HostFunctionError::SlotOutRange));
}

}  // namespace xrpl::test
