#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>

namespace xrpl::test {

using testing::Return;

// parent_ldgr_hash — no input, 32 bytes written out.
struct ParentLedgerHashGuest : GuestToHostCallTest
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

TEST_F(ParentLedgerHashGuest, hash_reaches_the_guests_out_region)
{
    EXPECT_CALL(host, getParentLedgerHash()).WillOnce(Return(hash));

    auto const wat = watFor(kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the hash's first four bytes, little-endian";
}

TEST_F(ParentLedgerHashGuest, status_is_the_hash_length)
{
    EXPECT_CALL(host, getParentLedgerHash()).WillOnce(Return(hash));

    auto const wat = watFor(kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kHashLen);
}

TEST_F(ParentLedgerHashGuest, host_error_becomes_contract_return_value)
{
    EXPECT_CALL(host, getParentLedgerHash())
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    auto const wat = watFor(kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

TEST_F(ParentLedgerHashGuest, host_exception_stops_the_run_and_is_logged)
{
    EXPECT_CALL(host, getParentLedgerHash())
        .WillOnce(testing::Throw(std::runtime_error{"parent ledger hash came apart"}));

    auto const outcome = run(watFor(kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("getParentLedgerHash"));
}

TEST_F(ParentLedgerHashGuest, out_region_one_byte_short_is_refused_after_asking_host)
{
    EXPECT_CALL(host, getParentLedgerHash()).WillOnce(Return(hash));

    auto const wat = watFor(Arg::outRegion(kOutAt, kHashLen - 1));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(ParentLedgerHashGuest, out_region_past_memory_is_refused_without_asking_host)
{
    EXPECT_CALL(host, getParentLedgerHash).Times(0);

    auto const wat = watFor(Arg::outRegion(kOnePage, kHashLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(ParentLedgerHashGuest, negative_out_pointer_is_refused_without_asking_host)
{
    EXPECT_CALL(host, getParentLedgerHash).Times(0);

    auto const wat = watFor(Arg::outRegion(-1, kHashLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
