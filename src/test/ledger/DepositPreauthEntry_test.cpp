#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/DepositPreauthEntry.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>

#include <set>
#include <string>
#include <utility>

namespace xrpl {
namespace test {

class DepositPreauthEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        std::string const credTypeStr = "termsandconditions";
        std::set<std::pair<AccountID, Slice>> const authCreds{{e.bob.id(), makeSlice(credTypeStr)}};

        expectKeylet<DepositPreauthEntry>(
            *this,
            e,
            keylet::depositPreauth(e.alice.id(), e.bob.id()),
            "depositPreauth(owner, preauthorized)",
            e.alice.id(),
            e.bob.id());

        expectKeylet<DepositPreauthEntry>(
            *this,
            e,
            keylet::depositPreauth(e.alice.id(), authCreds),
            "depositPreauth(owner, authCreds)",
            e.alice.id(),
            authCreds);

        expectKeylet<DepositPreauthEntry>(
            *this, e, keylet::depositPreauth(e.someID()), "depositPreauth(uint256)", e.someID());

        // Owner and preauthorized are both AccountIDs, so the assertion above
        // only has teeth if their order matters.
        BEAST_EXPECT(
            keylet::depositPreauth(e.alice.id(), e.bob.id()).key !=
            keylet::depositPreauth(e.bob.id(), e.alice.id()).key);

        // The credential-set overload must not collide with the single-account
        // one.
        BEAST_EXPECT(
            keylet::depositPreauth(e.alice.id(), authCreds).key !=
            keylet::depositPreauth(e.alice.id(), e.bob.id()).key);
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(DepositPreauthEntry, ledger, xrpl);

}  // namespace test
}  // namespace xrpl
