#include <test/jtx/Account.h>
#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/helpers/OracleEntry.h>
#include <xrpl/protocol/Indexes.h>

#include <cstdint>

namespace xrpl::test {

class OracleEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        expectKeylet<OracleEntry>(
            *this,
            e,
            keylet::oracle(e.alice.id(), 7u),
            "oracle(account, documentID)",
            e.alice.id(),
            std::uint32_t{7});
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(OracleEntry, ledger, xrpl);

}  // namespace xrpl::test
