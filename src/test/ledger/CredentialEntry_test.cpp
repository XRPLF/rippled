#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/CredentialEntry.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>

#include <string>

namespace xrpl {
namespace test {

class CredentialEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        std::string const credTypeStr = "termsandconditions";
        Slice const credType = makeSlice(credTypeStr);

        expectKeylet<CredentialEntry>(
            *this,
            e,
            keylet::credential(e.alice.id(), e.bob.id(), credType),
            "credential(subject, issuer, credType)",
            e.alice.id(),
            e.bob.id(),
            credType);

        expectKeylet<CredentialEntry>(
            *this, e, keylet::credential(e.someID()), "credential(uint256)", e.someID());

        // Subject and issuer are both AccountIDs, so the assertion above only
        // has teeth if their order matters.
        BEAST_EXPECT(
            keylet::credential(e.alice.id(), e.bob.id(), credType).key !=
            keylet::credential(e.bob.id(), e.alice.id(), credType).key);
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(CredentialEntry, ledger, xrpl);

}  // namespace test
}  // namespace xrpl
