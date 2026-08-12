#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/MPTokenIssuanceEntry.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>

#include <cstdint>

namespace xrpl {
namespace test {

class MPTokenIssuanceEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        MPTID const issuanceID = makeMptID(1, e.alice.id());

        expectKeylet<MPTokenIssuanceEntry>(
            *this,
            e,
            keylet::mptokenIssuance(makeMptID(1, e.alice.id())),
            "mptokenIssuance(seq, issuer)",
            std::uint32_t{1},
            e.alice.id());

        expectKeylet<MPTokenIssuanceEntry>(
            *this, e, keylet::mptokenIssuance(issuanceID), "mptokenIssuance(MPTID)", issuanceID);

        expectKeylet<MPTokenIssuanceEntry>(
            *this, e, keylet::mptokenIssuance(e.someID()), "mptokenIssuance(uint256)", e.someID());
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(MPTokenIssuanceEntry, ledger, xrpl);

}  // namespace test
}  // namespace xrpl
