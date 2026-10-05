#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <expected>
#include <string>

namespace xrpl::test {

using testing::Return;

// ldgr_index — no input, one scalar output.
struct LedgerSqnGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kOutAt = 0;
    static constexpr std::int32_t kSeqLen = 4;

    static constexpr Arg kOut = Arg::outRegion(kOutAt, kSeqLen);

    [[nodiscard]] static std::string
    watFor(Arg outArg, Answer answer = Answer::WrittenBytes)
    {
        return hostCallWat("ldgr_index", {outArg}, {}, answer);
    }
};

TEST_F(LedgerSqnGuest, sequence_reaches_guest_as_four_little_endian_bytes)
{
    EXPECT_CALL(host, getLedgerSqn()).WillOnce(Return(0x01020304u));

    // Read back with `i32.load`, which is little-endian by the wasm spec — so the value
    // arriving intact is the byte order being right.
    auto const wat = watFor(kOut);
    EXPECT_EQ(hostAnswer(wat), 0x01020304);
}

TEST_F(LedgerSqnGuest, host_error_becomes_contract_return_value)
{
    EXPECT_CALL(host, getLedgerSqn())
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    auto const wat = watFor(kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

TEST_F(LedgerSqnGuest, status_is_the_scalar_width)
{
    EXPECT_CALL(host, getLedgerSqn()).WillOnce(Return(0x01020304u));

    auto const wat = watFor(kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kSeqLen);
}

TEST_F(LedgerSqnGuest, host_exception_stops_the_run_and_is_logged)
{
    EXPECT_CALL(host, getLedgerSqn())
        .WillOnce(testing::Throw(std::runtime_error{"ledger sequence came apart"}));

    auto const outcome = run(watFor(kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("getLedgerSqn"));
}

}  // namespace xrpl::test
