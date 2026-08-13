#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/helpers/NegativeUNLEntry.h>
#include <xrpl/protocol/Indexes.h>

namespace xrpl::test {

class NegativeUNLEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        expectKeylet<NegativeUNLEntry>(*this, e, keylet::negativeUNL(), "negativeUNL()");
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(NegativeUNLEntry, ledger, xrpl);

}  // namespace xrpl::test
