#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/DirectoryNodeEntry.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>

#include <cstdint>

namespace xrpl {
namespace test {

class DirectoryNodeEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        expectKeylet<DirectoryNodeEntry>(
            *this, e, keylet::ownerDir(e.alice.id()), "ownerDir(id)", e.alice.id());

        expectKeylet<DirectoryNodeEntry>(
            *this,
            e,
            keylet::page(e.someID(), 3u),
            "page(root, index)",
            e.someID(),
            std::uint64_t{3});

        // The two overloads reach different keylet:: functions; a copy-paste
        // slip between them would be invisible otherwise.
        BEAST_EXPECT(keylet::ownerDir(e.alice.id()).key != keylet::page(e.someID(), 3u).key);
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(DirectoryNodeEntry, ledger, xrpl);

}  // namespace test
}  // namespace xrpl
