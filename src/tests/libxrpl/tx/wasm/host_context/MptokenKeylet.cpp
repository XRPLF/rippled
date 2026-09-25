#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostContextFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

// The engine's own rules - buffer-fit, guest memory - are tested on the Rust side, not here.
// `mptid` and `holder` are checked together in one condition rather than through
// `invokeWithAccount`, so which one fired is not observable when both are malformed.
struct MptokenKeyletCall : HostContextTest
{
    Bytes const mptidBytes = Bytes(24, 0x7a);
    Bytes const holderBytes{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                            0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14};

    MPTID const mptid = MPTID::fromVoid(mptidBytes.data());
    AccountID const holder = AccountID::fromVoid(holderBytes.data());

    Bytes const keylet = Bytes(32, 0xab);
};

TEST_F(MptokenKeyletCall, MptidAndHolderForwardedAndKeyletWritten)
{
    EXPECT_CALL(host, mptokenKeylet(testing::Eq(mptid), testing::Eq(holder)))
        .WillOnce(testing::Return(keylet));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.mptokenKeylet(bytesOf(mptidBytes), bytesOf(holderBytes), out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(MptokenKeyletCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, mptokenKeylet(testing::Eq(mptid), testing::Eq(holder)))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.mptokenKeylet(bytesOf(mptidBytes), bytesOf(holderBytes), out.slice()),
        hfErrorToInt(HostFunctionError::LedgerObjNotFound));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(MptokenKeyletCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, mptokenKeylet(testing::Eq(mptid), testing::Eq(holder)))
        .WillOnce(testing::Throw(std::runtime_error{"mptoken keylet came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.mptokenKeylet(bytesOf(mptidBytes), bytesOf(holderBytes), out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("mptoken keylet came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("mptokenKeylet"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(MptokenKeyletCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    EXPECT_CALL(host, mptokenKeylet(testing::Eq(mptid), testing::Eq(holder)))
        .WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size() - 1};
    EXPECT_EQ(
        hostContext.mptokenKeylet(bytesOf(mptidBytes), bytesOf(holderBytes), out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(MptokenKeyletCall, MalformedMptidIsRefusedWithoutAskingHost)
{
    Bytes const malformedMptid(MPTID::size() - 1, 0x7a);
    EXPECT_CALL(host, mptokenKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.mptokenKeylet(bytesOf(malformedMptid), bytesOf(holderBytes), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

// Distinct from a malformed mptid: the mptid is well-formed here, so this exercises the
// holder's own check rather than the mptid's.
TEST_F(MptokenKeyletCall, MalformedHolderIsRefusedWithoutAskingHost)
{
    Bytes const malformedHolder(AccountID::size() - 1, 0x01);
    EXPECT_CALL(host, mptokenKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.mptokenKeylet(bytesOf(mptidBytes), bytesOf(malformedHolder), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

// Both lengths are checked in one condition and both answer the same `InvalidParams`, so
// which one fired is not observable here. What is: neither argument reaches the host.
TEST_F(MptokenKeyletCall, BothArgumentsMalformedIsRefusedWithoutAskingHost)
{
    Bytes const malformedMptid(MPTID::size() - 1, 0x7a);
    Bytes const malformedHolder(AccountID::size() - 1, 0x01);
    EXPECT_CALL(host, mptokenKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.mptokenKeylet(bytesOf(malformedMptid), bytesOf(malformedHolder), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
