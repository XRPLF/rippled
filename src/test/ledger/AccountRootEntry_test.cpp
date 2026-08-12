#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootEntry.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>

namespace xrpl {
namespace test {

class AccountRootEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        expectKeylet<AccountRootEntry>(
            *this, e, keylet::account(e.alice.id()), "account(id)", e.alice.id());

        expectKeylet<AccountRootEntry>(
            *this,
            e,
            keylet::account(Account("nobody").id()),
            "account(id) absent",
            Account("nobody").id());
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(AccountRootEntry, ledger, xrpl);

}  // namespace test
}  // namespace xrpl
