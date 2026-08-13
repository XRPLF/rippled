#include <test/jtx/Account.h>
#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/helpers/NFTokenPageEntry.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>

namespace xrpl::test {

class NFTokenPageEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        Keylet const pageMin = keylet::nftokenPageMin(e.alice.id());

        expectKeylet<NFTokenPageEntry>(
            *this,
            e,
            keylet::nftokenPage(pageMin, e.someID()),
            "nftokenPage(page, token)",
            pageMin,
            e.someID());
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(NFTokenPageEntry, ledger, xrpl);

}  // namespace xrpl::test
