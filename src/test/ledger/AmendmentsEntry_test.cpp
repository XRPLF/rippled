#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/helpers/AmendmentsEntry.h>
#include <xrpl/protocol/Indexes.h>

namespace xrpl::test {

class AmendmentsEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        expectKeylet<AmendmentsEntry>(*this, e, keylet::amendments(), "amendments()");
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(AmendmentsEntry, ledger, xrpl);

}  // namespace xrpl::test
