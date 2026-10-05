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
struct LoanBrokerKeyletCall : HostContextTest
{
    Bytes const ownerBytes{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                           0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14};
    AccountID const owner = AccountID::fromVoid(ownerBytes.data());
    std::uint32_t const seq = 12345;
};

TEST_F(LoanBrokerKeyletCall, owner_and_seq_are_forwarded_keylet_is_written)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, loanBrokerKeylet(owner, seq)).WillOnce(testing::Return(keylet));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.loanBrokerKeylet(bytesOf(ownerBytes), seq, out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(LoanBrokerKeyletCall, host_error_becomes_contract_return_value)
{
    EXPECT_CALL(host, loanBrokerKeylet(owner, seq))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.loanBrokerKeylet(bytesOf(ownerBytes), seq, out.slice()),
        hfErrorToInt(HostFunctionError::LedgerObjNotFound));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(LoanBrokerKeyletCall, short_owner_is_refused_without_asking_host)
{
    Bytes const shortOwner(AccountID::size() - 1, 0x01);
    EXPECT_CALL(host, loanBrokerKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.loanBrokerKeylet(bytesOf(shortOwner), seq, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(LoanBrokerKeyletCall, long_owner_is_refused_without_asking_host)
{
    Bytes const longOwner(AccountID::size() + 1, 0x01);
    EXPECT_CALL(host, loanBrokerKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.loanBrokerKeylet(bytesOf(longOwner), seq, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(LoanBrokerKeyletCall, empty_owner_is_refused_without_asking_host)
{
    EXPECT_CALL(host, loanBrokerKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.loanBrokerKeylet(bytesOf(Bytes{}), seq, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(LoanBrokerKeyletCall, host_exception_becomes_internal_fatal_and_is_logged)
{
    EXPECT_CALL(host, loanBrokerKeylet(owner, seq))
        .WillOnce(testing::Throw(std::runtime_error{"loan broker keylet came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.loanBrokerKeylet(bytesOf(ownerBytes), seq, out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("loan broker keylet came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("loanBrokerKeylet"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(LoanBrokerKeyletCall, short_out_region_writes_nothing_and_returns_true_length)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, loanBrokerKeylet(owner, seq)).WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size() - 1};
    EXPECT_EQ(
        hostContext.loanBrokerKeylet(bytesOf(ownerBytes), seq, out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(LoanBrokerKeyletCall, out_region_of_exact_size_is_written)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, loanBrokerKeylet(owner, seq)).WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size()};
    EXPECT_EQ(
        hostContext.loanBrokerKeylet(bytesOf(ownerBytes), seq, out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(LoanBrokerKeyletCall, empty_result_answers_zero_and_writes_nothing)
{
    EXPECT_CALL(host, loanBrokerKeylet(owner, seq)).WillOnce(testing::Return(Bytes{}));

    OutRegion out{32};
    EXPECT_EQ(hostContext.loanBrokerKeylet(bytesOf(ownerBytes), seq, out.slice()), 0);
    EXPECT_FALSE(out.wasWritten());
}

}  // namespace xrpl::test
