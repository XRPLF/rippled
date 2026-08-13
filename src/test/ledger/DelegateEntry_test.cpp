#include <test/jtx/Account.h>
#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/helpers/DelegateEntry.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>

namespace xrpl::test {

class DelegateEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        expectKeylet<DelegateEntry>(
            *this,
            e,
            keylet::delegate(e.alice.id(), e.bob.id()),
            "delegate(account, authorizedAccount)",
            e.alice.id(),
            e.bob.id());

        // Both arguments are AccountIDs, so the assertion above only has teeth
        // if their order matters.
        BEAST_EXPECT(
            keylet::delegate(e.alice.id(), e.bob.id()).key !=
            keylet::delegate(e.bob.id(), e.alice.id()).key);
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(DelegateEntry, ledger, xrpl);

}  // namespace xrpl::test
