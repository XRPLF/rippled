#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/HostContextFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

namespace {

Bytes
concatBytes(Bytes const& first, Bytes const& second)
{
    Bytes bytes = first;
    bytes.insert(bytes.end(), second.begin(), second.end());
    return bytes;
}

}  // namespace

// The engine's own rules - buffer-fit, guest memory - are tested on the Rust side, not
// here. This is `parseAsset`'s only coverage, so every branch of its length-based dispatch
// is pinned below.
struct AmmKeyletCall : HostContextTest
{
    Bytes const mptWire = Bytes(24, 0x7a);
    Bytes const xrpWire = Bytes(20, 0x00);
    Bytes const currencyWire = Bytes(20, 0x42);
    Bytes const accountWire = Bytes(20, 0x99);
    Bytes const issueWire = concatBytes(currencyWire, accountWire);

    Asset const mptAsset{MPTID::fromVoid(mptWire.data())};
    Asset const xrpAsset{xrpIssue()};
    Asset const issueAsset{
        Issue{Currency::fromVoid(currencyWire.data()), AccountID::fromVoid(accountWire.data())}};

    Bytes const keylet = Bytes(32, 0xab);
};

TEST_F(AmmKeyletCall, MptAndIssueAssetsForwardedAndKeyletWritten)
{
    EXPECT_CALL(host, ammKeylet(testing::Eq(mptAsset), testing::Eq(issueAsset)))
        .WillOnce(testing::Return(keylet));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.ammKeylet(bytesOf(mptWire), bytesOf(issueWire), out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(AmmKeyletCall, BareXrpCurrencyBytesBecomeNativeAssetHostIsAskedFor)
{
    EXPECT_CALL(host, ammKeylet(testing::Eq(xrpAsset), testing::Eq(mptAsset)))
        .WillOnce(testing::Return(keylet));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.ammKeylet(bytesOf(xrpWire), bytesOf(mptWire), out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(AmmKeyletCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, ammKeylet(testing::Eq(mptAsset), testing::Eq(issueAsset)))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.ammKeylet(bytesOf(mptWire), bytesOf(issueWire), out.slice()),
        hfErrorToInt(HostFunctionError::LedgerObjNotFound));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(AmmKeyletCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, ammKeylet(testing::Eq(mptAsset), testing::Eq(issueAsset)))
        .WillOnce(testing::Throw(std::runtime_error{"amm keylet came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.ammKeylet(bytesOf(mptWire), bytesOf(issueWire), out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("amm keylet came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("ammKeylet"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(AmmKeyletCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    EXPECT_CALL(host, ammKeylet(testing::Eq(mptAsset), testing::Eq(issueAsset)))
        .WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size() - 1};
    EXPECT_EQ(
        hostContext.ammKeylet(bytesOf(mptWire), bytesOf(issueWire), out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(AmmKeyletCall, BareNonXrpCurrencyIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, ammKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.ammKeylet(bytesOf(currencyWire), bytesOf(mptWire), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(AmmKeyletCall, IssueWithNativeCurrencyIsRefusedWithoutAskingHost)
{
    Bytes const nativeIssueWire = concatBytes(xrpWire, accountWire);
    EXPECT_CALL(host, ammKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.ammKeylet(bytesOf(nativeIssueWire), bytesOf(mptWire), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(AmmKeyletCall, EmptyAssetIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, ammKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.ammKeylet(bytesOf(Bytes{}), bytesOf(mptWire), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

// asset1 is parsed before asset2, but `parseAsset` answers the same `InvalidParams` for every
// malformed shape, so which one was rejected is not observable here. The two are malformed for
// different reasons so the case is at least not a duplicate of the single-asset ones above.
TEST_F(AmmKeyletCall, BothAssetsMalformedIsRefusedWithoutAskingHost)
{
    Bytes const wrongLength{1, 2, 3, 4, 5};
    EXPECT_CALL(host, ammKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.ammKeylet(bytesOf(wrongLength), bytesOf(currencyWire), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
