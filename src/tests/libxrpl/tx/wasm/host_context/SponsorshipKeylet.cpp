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
//
// `sponsor` and `sponsee` are distinct byte patterns: a happy path built from two copies of the
// same account would still pass if the two were swapped.
struct SponsorshipKeyletCall : HostContextTest
{
    Bytes const sponsorBytes{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                             0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14};
    Bytes const sponseeBytes{0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
                             0xeb, 0xec, 0xed, 0xee, 0xef, 0xf0, 0xf1, 0xf2, 0xf3, 0xf4};
    AccountID const sponsor = AccountID::fromVoid(sponsorBytes.data());
    AccountID const sponsee = AccountID::fromVoid(sponseeBytes.data());
};

TEST_F(SponsorshipKeyletCall, sponsor_and_sponsee_are_forwarded_in_order_keylet_is_written)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, sponsorshipKeylet(sponsor, sponsee)).WillOnce(testing::Return(keylet));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.sponsorshipKeylet(bytesOf(sponsorBytes), bytesOf(sponseeBytes), out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(SponsorshipKeyletCall, host_error_becomes_contract_return_value)
{
    EXPECT_CALL(host, sponsorshipKeylet(sponsor, sponsee))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.sponsorshipKeylet(bytesOf(sponsorBytes), bytesOf(sponseeBytes), out.slice()),
        hfErrorToInt(HostFunctionError::LedgerObjNotFound));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(SponsorshipKeyletCall, malformed_sponsor_is_refused_without_asking_host)
{
    Bytes const malformedSponsor(AccountID::size() - 1, 0x01);
    EXPECT_CALL(host, sponsorshipKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.sponsorshipKeylet(
            bytesOf(malformedSponsor), bytesOf(sponseeBytes), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(SponsorshipKeyletCall, malformed_sponsee_is_refused_without_asking_host)
{
    Bytes const malformedSponsee(AccountID::size() + 1, 0xe1);
    EXPECT_CALL(host, sponsorshipKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.sponsorshipKeylet(
            bytesOf(sponsorBytes), bytesOf(malformedSponsee), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

// Both ids fail one combined length check, so a call malformed in both places answers the same
// `InvalidParams` as either alone; what's observable is that the host is never asked.
TEST_F(SponsorshipKeyletCall, both_accounts_malformed_is_refused_without_asking_host)
{
    Bytes const malformedSponsor(AccountID::size() - 1, 0x01);
    Bytes const malformedSponsee(AccountID::size() - 1, 0xe1);
    EXPECT_CALL(host, sponsorshipKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.sponsorshipKeylet(
            bytesOf(malformedSponsor), bytesOf(malformedSponsee), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(SponsorshipKeyletCall, host_exception_becomes_internal_fatal_and_is_logged)
{
    EXPECT_CALL(host, sponsorshipKeylet(sponsor, sponsee))
        .WillOnce(testing::Throw(std::runtime_error{"sponsorship keylet came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.sponsorshipKeylet(bytesOf(sponsorBytes), bytesOf(sponseeBytes), out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("sponsorship keylet came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("sponsorshipKeylet"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(SponsorshipKeyletCall, short_out_region_writes_nothing_and_returns_true_length)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, sponsorshipKeylet(sponsor, sponsee)).WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size() - 1};
    EXPECT_EQ(
        hostContext.sponsorshipKeylet(bytesOf(sponsorBytes), bytesOf(sponseeBytes), out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(SponsorshipKeyletCall, out_region_of_exact_size_is_written)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, sponsorshipKeylet(sponsor, sponsee)).WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size()};
    EXPECT_EQ(
        hostContext.sponsorshipKeylet(bytesOf(sponsorBytes), bytesOf(sponseeBytes), out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(SponsorshipKeyletCall, empty_result_answers_zero_and_writes_nothing)
{
    EXPECT_CALL(host, sponsorshipKeylet(sponsor, sponsee)).WillOnce(testing::Return(Bytes{}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.sponsorshipKeylet(bytesOf(sponsorBytes), bytesOf(sponseeBytes), out.slice()),
        0);
    EXPECT_FALSE(out.wasWritten());
}

}  // namespace xrpl::test
