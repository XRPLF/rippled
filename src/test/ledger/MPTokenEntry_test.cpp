#include <test/jtx/Account.h>
#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/helpers/MPTokenEntry.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/UintTypes.h>

namespace xrpl::test {

class MPTokenEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        MPTID const issuanceID = makeMptID(1, e.alice.id());

        expectKeylet<MPTokenEntry>(
            *this,
            e,
            keylet::mptoken(issuanceID, e.bob.id()),
            "mptoken(MPTID, holder)",
            issuanceID,
            e.bob.id());

        expectKeylet<MPTokenEntry>(
            *this,
            e,
            keylet::mptoken(e.someID(), e.bob.id()),
            "mptoken(issuanceKey, holder)",
            e.someID(),
            e.bob.id());

        expectKeylet<MPTokenEntry>(
            *this, e, keylet::mptoken(e.someID()), "mptoken(uint256)", e.someID());
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(MPTokenEntry, ledger, xrpl);

}  // namespace xrpl::test
