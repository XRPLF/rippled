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
struct OracleKeyletCall : HostContextTest
{
    Bytes const accountBytes{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                             0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14};
    AccountID const account = AccountID::fromVoid(accountBytes.data());
    std::uint32_t const docId = 12345;
};

TEST_F(OracleKeyletCall, account_and_doc_id_are_forwarded_keylet_is_written)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, oracleKeylet(account, docId)).WillOnce(testing::Return(keylet));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.oracleKeylet(bytesOf(accountBytes), docId, out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(OracleKeyletCall, host_error_becomes_contract_return_value)
{
    EXPECT_CALL(host, oracleKeylet(account, docId))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.oracleKeylet(bytesOf(accountBytes), docId, out.slice()),
        hfErrorToInt(HostFunctionError::LedgerObjNotFound));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(OracleKeyletCall, short_account_is_refused_without_asking_host)
{
    Bytes const shortAccount(AccountID::size() - 1, 0x01);
    EXPECT_CALL(host, oracleKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.oracleKeylet(bytesOf(shortAccount), docId, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(OracleKeyletCall, long_account_is_refused_without_asking_host)
{
    Bytes const longAccount(AccountID::size() + 1, 0x01);
    EXPECT_CALL(host, oracleKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.oracleKeylet(bytesOf(longAccount), docId, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(OracleKeyletCall, empty_account_is_refused_without_asking_host)
{
    EXPECT_CALL(host, oracleKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.oracleKeylet(bytesOf(Bytes{}), docId, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(OracleKeyletCall, host_exception_becomes_internal_fatal_and_is_logged)
{
    EXPECT_CALL(host, oracleKeylet(account, docId))
        .WillOnce(testing::Throw(std::runtime_error{"oracle keylet came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.oracleKeylet(bytesOf(accountBytes), docId, out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("oracle keylet came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("oracleKeylet"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(OracleKeyletCall, short_out_region_writes_nothing_and_returns_true_length)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, oracleKeylet(account, docId)).WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size() - 1};
    EXPECT_EQ(
        hostContext.oracleKeylet(bytesOf(accountBytes), docId, out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(OracleKeyletCall, out_region_of_exact_size_is_written)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, oracleKeylet(account, docId)).WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size()};
    EXPECT_EQ(
        hostContext.oracleKeylet(bytesOf(accountBytes), docId, out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(OracleKeyletCall, empty_result_answers_zero_and_writes_nothing)
{
    EXPECT_CALL(host, oracleKeylet(account, docId)).WillOnce(testing::Return(Bytes{}));

    OutRegion out{32};
    EXPECT_EQ(hostContext.oracleKeylet(bytesOf(accountBytes), docId, out.slice()), 0);
    EXPECT_FALSE(out.wasWritten());
}

}  // namespace xrpl::test
