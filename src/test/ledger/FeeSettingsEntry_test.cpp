#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/helpers/FeeSettingsEntry.h>
#include <xrpl/protocol/Indexes.h>

namespace xrpl::test {

class FeeSettingsEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        expectKeylet<FeeSettingsEntry>(*this, e, keylet::feeSettings(), "feeSettings()");
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(FeeSettingsEntry, ledger, xrpl);

}  // namespace xrpl::test
