#include <test/jtx/Account.h>
#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/helpers/SponsorshipEntry.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>

namespace xrpl::test {

class SponsorshipEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        expectKeylet<SponsorshipEntry>(
            *this,
            e,
            keylet::sponsorship(e.alice.id(), e.bob.id()),
            "sponsorship(sponsor, sponsee)",
            e.alice.id(),
            e.bob.id());

        // Sponsor and sponsee are both AccountIDs, so the assertion above only
        // has teeth if their order matters.
        BEAST_EXPECT(
            keylet::sponsorship(e.alice.id(), e.bob.id()).key !=
            keylet::sponsorship(e.bob.id(), e.alice.id()).key);
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(SponsorshipEntry, ledger, xrpl);

}  // namespace xrpl::test
