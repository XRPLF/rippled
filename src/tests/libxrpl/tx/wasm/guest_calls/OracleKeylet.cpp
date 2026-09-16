#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostCallFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>

namespace xrpl::test {

using testing::Return;

// oracle_id — an account region and a four-byte `doc_id` region in, a keylet region out.
struct OracleKeyletGuest : HostCallTest
{
    static constexpr std::int32_t kAccountAt = 0;
    static constexpr std::int32_t kDocIdAt = 32;
    static constexpr std::int32_t kOutAt = 64;
    static constexpr std::int32_t kAccountLen = static_cast<std::int32_t>(AccountID::size());
    static constexpr std::int32_t kDocIdLen = 4;
    static constexpr std::int32_t kKeyletLen = 32;
    static constexpr std::uint32_t kDocIdValue = 0x74859607;

    static constexpr Arg kAccount = Arg::region(kAccountAt, kAccountLen);
    static constexpr Arg kDocId = Arg::region(kDocIdAt, kDocIdLen);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kKeyletLen);

    Bytes const accountBytes = Bytes(AccountID::size(), 0x0c);
    AccountID const account = AccountID::fromVoid(accountBytes.data());
    Bytes const docIdBytes{
        static_cast<std::uint8_t>(kDocIdValue),
        static_cast<std::uint8_t>(kDocIdValue >> 8),
        static_cast<std::uint8_t>(kDocIdValue >> 16),
        static_cast<std::uint8_t>(kDocIdValue >> 24)};

    Bytes const keylet = [] {
        Bytes bytes(kKeyletLen, 0xab);
        bytes[0] = 0x0d;
        bytes[1] = 0x0c;
        bytes[2] = 0x0b;
        bytes[3] = 0x0a;
        return bytes;
    }();

    [[nodiscard]] std::string
    watFor(Arg accountArg, Arg docIdArg, Arg outArg, Answer answer = Answer::WrittenBytes) const
    {
        return hostCallWat(
            "oracle_id",
            {accountArg, docIdArg, outArg},
            {{.at = kAccountAt, .bytes = accountBytes}, {.at = kDocIdAt, .bytes = docIdBytes}},
            answer);
    }
};

TEST_F(OracleKeyletGuest, AccountAndDocIdReachHostAndKeyletComesBack)
{
    EXPECT_CALL(host, oracleKeylet(account, kDocIdValue)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount, kDocId, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the keylet's first four bytes, little-endian";
}

TEST_F(OracleKeyletGuest, StatusIsTheKeyletsLength)
{
    EXPECT_CALL(host, oracleKeylet(account, kDocIdValue)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount, kDocId, kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kKeyletLen);
}

TEST_F(OracleKeyletGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, oracleKeylet(account, kDocIdValue))
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    auto const wat = watFor(kAccount, kDocId, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

TEST_F(OracleKeyletGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, oracleKeylet(account, kDocIdValue))
        .WillOnce(testing::Throw(std::runtime_error{"oracle keylet came apart"}));

    auto const outcome = callHost(watFor(kAccount, kDocId, kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("oracleKeylet"));
}

TEST_F(OracleKeyletGuest, AccountOfTheWrongLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, oracleKeylet).Times(0);

    auto const wat = watFor(Arg::region(kAccountAt, kAccountLen - 1), kDocId, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(OracleKeyletGuest, DocIdRegionOfAnyWidthButFourIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, oracleKeylet).Times(0);

    for (std::int32_t const len : {kDocIdLen - 1, kDocIdLen + 1})
    {
        auto const wat = watFor(kAccount, Arg::region(kDocIdAt, len), kOut);
        EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams))
            << len << " bytes";
    }
}

TEST_F(OracleKeyletGuest, InputRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, oracleKeylet).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kAccountLen), kDocId, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(OracleKeyletGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, oracleKeylet).Times(0);

    auto const wat = watFor(Arg::region(-1, kAccountLen), kDocId, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(OracleKeyletGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, oracleKeylet(account, kDocIdValue)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount, kDocId, Arg::outRegion(kOutAt, kKeyletLen - 1));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(OracleKeyletGuest, OutRegionPastMemoryIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, oracleKeylet(account, kDocIdValue)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount, kDocId, Arg::outRegion(kOnePage, kKeyletLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(OracleKeyletGuest, NegativeOutPointerIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, oracleKeylet(account, kDocIdValue)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount, kDocId, Arg::outRegion(-1, kKeyletLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
