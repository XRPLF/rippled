#include <xrpl/basics/base_uint.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostContextFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

// The engine's own rules - buffer-fit, guest memory - are tested on the Rust side, not here.
struct LoanKeyletCall : HostContextTest
{
    Bytes const loanBrokerIdBytes{0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b,
                                  0x5c, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
                                  0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70};
    uint256 const loanBrokerId = uint256::fromVoid(loanBrokerIdBytes.data());
    std::uint32_t const loanSeq = 12345;
};

TEST_F(LoanKeyletCall, LoanBrokerIdAndSeqAreForwardedKeyletIsWritten)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, loanKeylet(testing::Eq(loanBrokerId), loanSeq))
        .WillOnce(testing::Return(keylet));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.loanKeylet(bytesOf(loanBrokerIdBytes), loanSeq, out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(LoanKeyletCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, loanKeylet(testing::Eq(loanBrokerId), loanSeq))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.loanKeylet(bytesOf(loanBrokerIdBytes), loanSeq, out.slice()),
        hfErrorToInt(HostFunctionError::LedgerObjNotFound));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(LoanKeyletCall, MalformedLoanBrokerIdIsRefusedWithoutAskingHost)
{
    Bytes const malformedLoanBrokerId(uint256::size() - 1, 0x51);
    EXPECT_CALL(host, loanKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.loanKeylet(bytesOf(malformedLoanBrokerId), loanSeq, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(LoanKeyletCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, loanKeylet(testing::Eq(loanBrokerId), loanSeq))
        .WillOnce(testing::Throw(std::runtime_error{"loan keylet came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.loanKeylet(bytesOf(loanBrokerIdBytes), loanSeq, out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("loan keylet came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("loanKeylet"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(LoanKeyletCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, loanKeylet(testing::Eq(loanBrokerId), loanSeq))
        .WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size() - 1};
    EXPECT_EQ(
        hostContext.loanKeylet(bytesOf(loanBrokerIdBytes), loanSeq, out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(LoanKeyletCall, OutRegionOfExactSizeIsWritten)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, loanKeylet(testing::Eq(loanBrokerId), loanSeq))
        .WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size()};
    EXPECT_EQ(
        hostContext.loanKeylet(bytesOf(loanBrokerIdBytes), loanSeq, out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(LoanKeyletCall, EmptyResultAnswersZeroAndWritesNothing)
{
    EXPECT_CALL(host, loanKeylet(testing::Eq(loanBrokerId), loanSeq))
        .WillOnce(testing::Return(Bytes{}));

    OutRegion out{32};
    EXPECT_EQ(hostContext.loanKeylet(bytesOf(loanBrokerIdBytes), loanSeq, out.slice()), 0);
    EXPECT_FALSE(out.wasWritten());
}

}  // namespace xrpl::test
