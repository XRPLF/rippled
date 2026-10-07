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

// loan_id — a 32-byte loan broker id and a four-byte sequence region in, a keylet out.
struct LoanKeyletGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kLoanBrokerIdAt = 0;
    static constexpr std::int32_t kSeqAt = 32;
    static constexpr std::int32_t kOutAt = 64;
    static constexpr std::int32_t kLoanBrokerIdLen = static_cast<std::int32_t>(uint256::size());
    static constexpr std::int32_t kSeqLen = 4;
    static constexpr std::int32_t kKeyletLen = 32;
    static constexpr std::uint32_t kSeqValue = 0x73849506;

    static constexpr Arg kLoanBrokerId = Arg::region(kLoanBrokerIdAt, kLoanBrokerIdLen);
    static constexpr Arg kSeq = Arg::region(kSeqAt, kSeqLen);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kKeyletLen);

    Bytes const loanBrokerIdBytes = Bytes(uint256::size(), 0x1b);
    uint256 const loanBrokerId = uint256::fromVoid(loanBrokerIdBytes.data());
    Bytes const seqBytes{
        static_cast<std::uint8_t>(kSeqValue),
        static_cast<std::uint8_t>(kSeqValue >> 8),
        static_cast<std::uint8_t>(kSeqValue >> 16),
        static_cast<std::uint8_t>(kSeqValue >> 24)};

    Bytes const keylet = [] {
        Bytes bytes(kKeyletLen, 0xab);
        bytes[0] = 0x0d;
        bytes[1] = 0x0c;
        bytes[2] = 0x0b;
        bytes[3] = 0x0a;
        return bytes;
    }();

    [[nodiscard]] std::string
    watFor(Arg loanBrokerIdArg, Arg seqArg, Arg outArg, Answer answer = Answer::WrittenBytes) const
    {
        return hostCallWat(
            "loan_id",
            {loanBrokerIdArg, seqArg, outArg},
            {{.at = kLoanBrokerIdAt, .bytes = loanBrokerIdBytes},
             {.at = kSeqAt, .bytes = seqBytes}},
            answer);
    }
};

TEST_F(LoanKeyletGuest, loan_broker_id_and_seq_reach_host_and_keylet_comes_back)
{
    EXPECT_CALL(host, loanKeylet(Eq(loanBrokerId), kSeqValue)).WillOnce(Return(keylet));

    auto const wat = watFor(kLoanBrokerId, kSeq, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the keylet's first four bytes, little-endian";
}

}  // namespace xrpl::test
