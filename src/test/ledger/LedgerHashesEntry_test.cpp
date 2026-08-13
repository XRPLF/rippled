#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/helpers/LedgerHashesEntry.h>
#include <xrpl/protocol/Indexes.h>

namespace xrpl::test {

class LedgerHashesEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        expectKeylet<LedgerHashesEntry>(*this, e, keylet::skip(), "skip()");
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerHashesEntry, ledger, xrpl);

}  // namespace xrpl::test
