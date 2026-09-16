#include <xrpl/basics/base_uint.h>
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

// parent_ldgr_hash — no input, 32 bytes written out.
//
// `abi.rs`'s `write_into` serves it, so the out region is judged *before* the host is asked:
// an unreachable region never reaches the mock, while a too-short one does, the fit being
// decided against the length the host reports.
struct ParentLedgerHashGuest : HostCallTest
{
    static constexpr std::int32_t kOutAt = 0;
    static constexpr std::int32_t kHashLen = static_cast<std::int32_t>(uint256::size());

    static constexpr Arg kOut = Arg::outRegion(kOutAt, kHashLen);

    // A hash whose first four bytes are distinctive, so the guest's `i32.load` of them
    // cannot pass by accident.
    Bytes const hashBytes = [] {
        Bytes bytes(uint256::size(), 0xab);
        bytes[0] = 0x0d;
        bytes[1] = 0x0c;
        bytes[2] = 0x0b;
        bytes[3] = 0x0a;
        return bytes;
    }();
    Hash const hash = uint256::fromVoid(hashBytes.data());

    [[nodiscard]] static std::string
    watFor(Arg outArg, Answer answer = Answer::WrittenBytes)
    {
        return hostCallWat("parent_ldgr_hash", {outArg}, {}, answer);
    }
};

TEST_F(ParentLedgerHashGuest, HashReachesTheGuestsOutRegion)
{
    EXPECT_CALL(host, getParentLedgerHash()).WillOnce(Return(hash));

    auto const wat = watFor(kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the hash's first four bytes, little-endian";
}

TEST_F(ParentLedgerHashGuest, StatusIsTheHashLength)
{
    EXPECT_CALL(host, getParentLedgerHash()).WillOnce(Return(hash));

    auto const wat = watFor(kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kHashLen);
}

TEST_F(ParentLedgerHashGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getParentLedgerHash())
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    auto const wat = watFor(kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

// `guarded` turns the throw into `InternalFatal`, which the engine treats as fatal rather
// than passing back: the run ends, and the guest never resumes to read it.
TEST_F(ParentLedgerHashGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, getParentLedgerHash())
        .WillOnce(testing::Throw(std::runtime_error{"parent ledger hash came apart"}));

    auto const outcome = callHost(watFor(kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("getParentLedgerHash"));
}

TEST_F(ParentLedgerHashGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, getParentLedgerHash()).WillOnce(Return(hash));

    auto const wat = watFor(Arg::outRegion(kOutAt, kHashLen - 1));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(ParentLedgerHashGuest, OutRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getParentLedgerHash).Times(0);

    auto const wat = watFor(Arg::outRegion(kOnePage, kHashLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(ParentLedgerHashGuest, NegativeOutPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getParentLedgerHash).Times(0);

    auto const wat = watFor(Arg::outRegion(-1, kHashLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
