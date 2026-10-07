#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/BytesHelpers.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <string>
#include <vector>

namespace xrpl::test {

using testing::Return;

// tx_inner — a locator region in, bytes out.
struct TxNestedFieldGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kLocatorAt = 16;
    static constexpr std::int32_t kLocatorLen = 12;
    static constexpr std::int32_t kOutAt = 64;
    static constexpr std::int32_t kOutLen = 32;

    static constexpr Arg kLocator = Arg::region(kLocatorAt, kLocatorLen);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kOutLen);

    // A negative step, so a sign or byte-order mistake in the wire form shows up.
    std::vector<std::int32_t> const steps{5, -12, 130};
    Bytes const locatorBytes = bytesOfSteps(steps);

    // Six bytes, the first four distinctive so the guest's `i32.load` of them cannot pass by
    // accident and the reported length cannot be mistaken for that load's width.
    Bytes const value{0x0d, 0x0c, 0x0b, 0x0a, 0xee, 0xff};

    [[nodiscard]] std::string
    watFor(Arg locatorArg, Arg outArg, Answer answer = Answer::WrittenBytes) const
    {
        return hostCallWat(
            "tx_inner", {locatorArg, outArg}, {{.at = kLocatorAt, .bytes = locatorBytes}}, answer);
    }
};

TEST_F(TxNestedFieldGuest, locator_steps_reach_host_and_bytes_come_back)
{
    EXPECT_CALL(host, getTxNestedField(LocatorEquals(steps))).WillOnce(Return(value));

    auto const wat = watFor(kLocator, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the first four bytes, little-endian";
}

}  // namespace xrpl::test
