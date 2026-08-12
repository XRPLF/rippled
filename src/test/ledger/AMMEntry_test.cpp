#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AMMEntry.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>

namespace xrpl {
namespace test {

class AMMEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        Asset const xrp{xrpIssue()};
        Asset const usd{e.alice["USD"].issue()};

        expectKeylet<AMMEntry>(*this, e, keylet::amm(xrp, usd), "amm(asset, asset)", xrp, usd);

        expectKeylet<AMMEntry>(*this, e, keylet::amm(e.someID()), "amm(uint256)", e.someID());
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(AMMEntry, ledger, xrpl);

}  // namespace test
}  // namespace xrpl
