#include <xrpl/protocol/AccountID.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostContextFixture.h>

#include <cstdint>
#include <expected>
#include <limits>
#include <stdexcept>

namespace xrpl::test {

// The engine's own rules - buffer-fit, guest memory - are tested on the Rust side, not here.
//
// The first file over the account-in, keylet-out shape `invokeWithAccount` gives eleven other
// methods, so `account` is a distinctive 20 bytes rather than all-zero: a forwarding mistake
// (a swapped byte, a truncated copy) would still pass against an all-zero id.
struct EscrowKeyletCall : HostContextTest
{
    Bytes const accountBytes{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                             0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14};
    AccountID const account = AccountID::fromVoid(accountBytes.data());
    std::int32_t const seq = 12345;
};

TEST_F(EscrowKeyletCall, AccountAndSeqAreForwardedKeyletIsWritten)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, escrowKeylet(account, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Return(keylet));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.escrowKeylet(bytesOf(accountBytes), seq, out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(EscrowKeyletCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, escrowKeylet(account, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.escrowKeylet(bytesOf(accountBytes), seq, out.slice()),
        hfErrorToInt(HostFunctionError::LedgerObjNotFound));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(EscrowKeyletCall, ShortAccountIsRefusedWithoutAskingHost)
{
    Bytes const shortAccount(AccountID::size() - 1, 0x01);
    EXPECT_CALL(host, escrowKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.escrowKeylet(bytesOf(shortAccount), seq, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(EscrowKeyletCall, LongAccountIsRefusedWithoutAskingHost)
{
    Bytes const longAccount(AccountID::size() + 1, 0x01);
    EXPECT_CALL(host, escrowKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.escrowKeylet(bytesOf(longAccount), seq, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(EscrowKeyletCall, EmptyAccountIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, escrowKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.escrowKeylet(bytesOf(Bytes{}), seq, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(EscrowKeyletCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, escrowKeylet(account, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Throw(std::runtime_error{"escrow keylet came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.escrowKeylet(bytesOf(accountBytes), seq, out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("escrow keylet came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("escrowKeylet"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(EscrowKeyletCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, escrowKeylet(account, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size() - 1};
    EXPECT_EQ(
        hostContext.escrowKeylet(bytesOf(accountBytes), seq, out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(EscrowKeyletCall, OutRegionOfExactSizeIsWritten)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, escrowKeylet(account, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size()};
    EXPECT_EQ(
        hostContext.escrowKeylet(bytesOf(accountBytes), seq, out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(EscrowKeyletCall, EmptyResultAnswersZeroAndWritesNothing)
{
    EXPECT_CALL(host, escrowKeylet(account, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Return(Bytes{}));

    OutRegion out{32};
    EXPECT_EQ(hostContext.escrowKeylet(bytesOf(accountBytes), seq, out.slice()), 0);
    EXPECT_FALSE(out.wasWritten());
}

// `seq` crosses the ABI as an `i32` bit pattern, not a signed count: the guest's
// `4294967295u` is this `-1`, and `escrowKeylet` must hand the host back `4294967295u`, not a
// sign-extended or clamped value.
TEST_F(EscrowKeyletCall, NegativeSeqArrivesAtHostAsUnsignedBitPattern)
{
    std::int32_t const negativeSeq = -1;
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, escrowKeylet(account, std::numeric_limits<std::uint32_t>::max()))
        .WillOnce(testing::Return(keylet));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.escrowKeylet(bytesOf(accountBytes), negativeSeq, out.slice()),
        static_cast<std::int32_t>(keylet.size()));
}

}  // namespace xrpl::test
