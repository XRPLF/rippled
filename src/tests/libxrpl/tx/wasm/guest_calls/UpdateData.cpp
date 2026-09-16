#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostCallFixture.h>
#include <tx/wasm/fixtures/MockHostFunctions.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>

namespace xrpl::test {

using testing::Return;

// set_data — one region in, the byte count returned directly.
//
// The only mutating call with no out region, so the guest learns nothing but the count; what
// this layer has to show is that the bytes it wrote are the bytes the host is handed.
struct UpdateDataGuest : HostCallTest
{
    static constexpr std::int32_t kDataAt = 0;
    static constexpr std::int32_t kDataLen = 5;

    static constexpr Arg kData = Arg::region(kDataAt, kDataLen);

    Bytes const data{'h', 'e', 'l', 'l', 'o'};

    [[nodiscard]] std::string
    watFor(Arg dataArg) const
    {
        return hostCallWat("set_data", {dataArg}, {{.at = kDataAt, .bytes = data}});
    }
};

TEST_F(UpdateDataGuest, GuestBytesReachHostAndTheStoredCountIsTheAnswer)
{
    EXPECT_CALL(host, updateData(BytesAre("hello"))).WillOnce(Return(kDataLen));

    auto const wat = watFor(kData);
    EXPECT_EQ(hostAnswer(wat), kDataLen);
}

TEST_F(UpdateDataGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, updateData(BytesAre("hello")))
        .WillOnce(Return(std::unexpected(HostFunctionError::DataFieldTooLarge)));

    auto const wat = watFor(kData);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::DataFieldTooLarge));
}

// `guarded` turns the throw into `InternalFatal`, which the engine treats as fatal rather
// than passing back: the run ends, and the guest never resumes to read it.
TEST_F(UpdateDataGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, updateData(BytesAre("hello")))
        .WillOnce(testing::Throw(std::runtime_error{"update data came apart"}));

    auto const outcome = callHost(watFor(kData));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("updateData"));
}

TEST_F(UpdateDataGuest, InputRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, updateData).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kDataLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(UpdateDataGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, updateData).Times(0);

    auto const wat = watFor(Arg::region(-1, kDataLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
