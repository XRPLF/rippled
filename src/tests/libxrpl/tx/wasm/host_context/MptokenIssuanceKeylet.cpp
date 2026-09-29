#include <xrpl/protocol/AccountID.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostContextFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

// The engine's own rules - buffer-fit, guest memory - are tested on the Rust side, not here.
struct MptokenIssuanceKeyletCall : HostContextTest
{
    Bytes const issuerBytes{0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a,
                            0x5b, 0x5c, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62, 0x63, 0x64};
    AccountID const issuer = AccountID::fromVoid(issuerBytes.data());
    std::uint32_t const seq = 98765;
};

TEST_F(MptokenIssuanceKeyletCall, issuer_and_seq_are_forwarded_keylet_is_written)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, mptokenIssuanceKeylet(issuer, seq)).WillOnce(testing::Return(keylet));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.mptokenIssuanceKeylet(bytesOf(issuerBytes), seq, out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(MptokenIssuanceKeyletCall, host_error_becomes_contract_return_value)
{
    EXPECT_CALL(host, mptokenIssuanceKeylet(issuer, seq))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.mptokenIssuanceKeylet(bytesOf(issuerBytes), seq, out.slice()),
        hfErrorToInt(HostFunctionError::LedgerObjNotFound));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(MptokenIssuanceKeyletCall, short_issuer_is_refused_without_asking_host)
{
    Bytes const shortIssuer(AccountID::size() - 1, 0x01);
    EXPECT_CALL(host, mptokenIssuanceKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.mptokenIssuanceKeylet(bytesOf(shortIssuer), seq, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(MptokenIssuanceKeyletCall, long_issuer_is_refused_without_asking_host)
{
    Bytes const longIssuer(AccountID::size() + 1, 0x01);
    EXPECT_CALL(host, mptokenIssuanceKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.mptokenIssuanceKeylet(bytesOf(longIssuer), seq, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(MptokenIssuanceKeyletCall, empty_issuer_is_refused_without_asking_host)
{
    EXPECT_CALL(host, mptokenIssuanceKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.mptokenIssuanceKeylet(bytesOf(Bytes{}), seq, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(MptokenIssuanceKeyletCall, host_exception_becomes_internal_fatal_and_is_logged)
{
    EXPECT_CALL(host, mptokenIssuanceKeylet(issuer, seq))
        .WillOnce(testing::Throw(std::runtime_error{"mptoken issuance keylet came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.mptokenIssuanceKeylet(bytesOf(issuerBytes), seq, out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("mptoken issuance keylet came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("mptokenIssuanceKeylet"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(MptokenIssuanceKeyletCall, short_out_region_writes_nothing_and_returns_true_length)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, mptokenIssuanceKeylet(issuer, seq)).WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size() - 1};
    EXPECT_EQ(
        hostContext.mptokenIssuanceKeylet(bytesOf(issuerBytes), seq, out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(MptokenIssuanceKeyletCall, out_region_of_exact_size_is_written)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, mptokenIssuanceKeylet(issuer, seq)).WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size()};
    EXPECT_EQ(
        hostContext.mptokenIssuanceKeylet(bytesOf(issuerBytes), seq, out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(MptokenIssuanceKeyletCall, empty_result_answers_zero_and_writes_nothing)
{
    EXPECT_CALL(host, mptokenIssuanceKeylet(issuer, seq)).WillOnce(testing::Return(Bytes{}));

    OutRegion out{32};
    EXPECT_EQ(hostContext.mptokenIssuanceKeylet(bytesOf(issuerBytes), seq, out.slice()), 0);
    EXPECT_FALSE(out.wasWritten());
}

}  // namespace xrpl::test
