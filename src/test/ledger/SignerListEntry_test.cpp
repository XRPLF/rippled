#include <test/jtx/Account.h>
#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/helpers/SignerListEntry.h>
#include <xrpl/protocol/Indexes.h>

namespace xrpl::test {

class SignerListEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        expectKeylet<SignerListEntry>(
            *this, e, keylet::signerList(e.alice.id()), "signerList(account)", e.alice.id());
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(SignerListEntry, ledger, xrpl);

}  // namespace xrpl::test
