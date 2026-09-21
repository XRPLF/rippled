#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestCallFixture.h>
#include <tx/wasm/fixtures/MockHostFunctions.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>

namespace xrpl::test {

using testing::Return;

// check_sig — three byte regions in, the verdict as the return value.
struct CheckSignatureGuest : GuestCallTest
{
    static constexpr std::int32_t kMessageAt = 0;
    static constexpr std::int32_t kSignatureAt = 32;
    static constexpr std::int32_t kPubkeyAt = 64;
    static constexpr std::int32_t kSliceLen = 3;

    static constexpr Arg kMessage = Arg::region(kMessageAt, kSliceLen);
    static constexpr Arg kSignature = Arg::region(kSignatureAt, kSliceLen);
    static constexpr Arg kPubkey = Arg::region(kPubkeyAt, kSliceLen);

    Bytes const messageBytes{'m', 's', 'g'};
    Bytes const signatureBytes{'s', 'i', 'g'};
    Bytes const pubkeyBytes{'k', 'e', 'y'};

    [[nodiscard]] std::string
    watFor(Arg messageArg, Arg signatureArg, Arg pubkeyArg) const
    {
        return hostCallWat(
            "check_sig",
            {messageArg, signatureArg, pubkeyArg},
            {{.at = kMessageAt, .bytes = messageBytes},
             {.at = kSignatureAt, .bytes = signatureBytes},
             {.at = kPubkeyAt, .bytes = pubkeyBytes}});
    }
};

TEST_F(CheckSignatureGuest, MessageSignatureAndPubkeyReachHostInOrder)
{
    EXPECT_CALL(host, checkSignature(BytesAre("msg"), BytesAre("sig"), BytesAre("key")))
        .WillOnce(Return(1));

    auto const wat = watFor(kMessage, kSignature, kPubkey);
    EXPECT_EQ(hostAnswer(wat), 1);
}

// Nothing on this path holds the three regions to a length, deliberately rather than by
// oversight: empty ones reach the host like any others.
TEST_F(CheckSignatureGuest, EmptyRegionsReachHostUnvalidated)
{
    auto const isEmpty = testing::Property(&Slice::empty, true);
    EXPECT_CALL(host, checkSignature(isEmpty, isEmpty, isEmpty)).WillOnce(Return(0));

    auto const wat =
        watFor(Arg::region(kMessageAt, 0), Arg::region(kSignatureAt, 0), Arg::region(kPubkeyAt, 0));
    EXPECT_EQ(hostAnswer(wat), 0);
}

TEST_F(CheckSignatureGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, checkSignature(BytesAre("msg"), BytesAre("sig"), BytesAre("key")))
        .WillOnce(Return(std::unexpected(HostFunctionError::InvalidParams)));

    auto const wat = watFor(kMessage, kSignature, kPubkey);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(CheckSignatureGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, checkSignature(BytesAre("msg"), BytesAre("sig"), BytesAre("key")))
        .WillOnce(testing::Throw(std::runtime_error{"signature check came apart"}));

    auto const outcome = run(watFor(kMessage, kSignature, kPubkey));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("checkSignature"));
}

TEST_F(CheckSignatureGuest, InputRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, checkSignature).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kSliceLen), kSignature, kPubkey);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(CheckSignatureGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, checkSignature).Times(0);

    auto const wat = watFor(Arg::region(-1, kSliceLen), kSignature, kPubkey);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
