#include <test/jtx/Account.h>
#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/helpers/RippleStateEntry.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/UintTypes.h>

namespace xrpl::test {

class RippleStateEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        auto const usd = e.alice["USD"];
        Currency const currency = usd.currency;

        expectKeylet<RippleStateEntry>(
            *this,
            e,
            keylet::trustLine(e.alice.id(), e.bob.id(), currency),
            "trustLine(id0, id1, currency)",
            e.alice.id(),
            e.bob.id(),
            currency);

        expectKeylet<RippleStateEntry>(
            *this,
            e,
            keylet::trustLine(e.bob.id(), usd.issue()),
            "trustLine(id, issue)",
            e.bob.id(),
            usd.issue());

        // Trust lines are deliberately symmetric in their two accounts -- the
        // keylet canonicalizes them -- so unlike the other two-account entries
        // there is no transposition to catch here.
        BEAST_EXPECT(
            keylet::trustLine(e.alice.id(), e.bob.id(), currency).key ==
            keylet::trustLine(e.bob.id(), e.alice.id(), currency).key);
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(RippleStateEntry, ledger, xrpl);

}  // namespace xrpl::test
