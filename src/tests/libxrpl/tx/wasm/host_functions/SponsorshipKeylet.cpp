#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

namespace xrpl::test {

struct SponsorshipKeyletImpl : RealHostFixture
{
};

TEST_F(SponsorshipKeyletImpl, matches_sponsorship_keylet_function)
{
    auto const sponsor = fund("sponsor");
    auto const sponsee = fund("sponsee");

    expectKeyletMatches(
        makeHost()->sponsorshipKeylet(sponsor.id(), sponsee.id()),
        keylet::sponsorship(sponsor.id(), sponsee.id()));
}

TEST_F(SponsorshipKeyletImpl, cant_sponsor_self)
{
    auto const sponsor = fund("sponsor");

    expectError(
        makeHost()->sponsorshipKeylet(sponsor.id(), sponsor.id()),
        HostFunctionError::InvalidParams);
}

TEST_F(SponsorshipKeyletImpl, invalid_account)
{
    auto const sponsor = fund("sponsor");

    auto h = makeHost();
    expectError(h->sponsorshipKeylet(AccountID{}, sponsor.id()), HostFunctionError::InvalidAccount);
    expectError(h->sponsorshipKeylet(sponsor.id(), AccountID{}), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
