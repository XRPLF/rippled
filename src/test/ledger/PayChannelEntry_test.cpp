#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/PayChannelEntry.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SeqProxy.h>

namespace xrpl {
namespace test {

class PayChannelEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        SeqProxy const seq = SeqProxy::rawSequence(4);

        expectKeylet<PayChannelEntry>(
            *this,
            e,
            keylet::payChannel(e.alice.id(), e.bob.id(), seq),
            "payChannel(src, dst, seq)",
            e.alice.id(),
            e.bob.id(),
            seq);

        // Source and destination are both AccountIDs, so the assertion above
        // only has teeth if their order matters.
        BEAST_EXPECT(
            keylet::payChannel(e.alice.id(), e.bob.id(), seq).key !=
            keylet::payChannel(e.bob.id(), e.alice.id(), seq).key);
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(PayChannelEntry, ledger, xrpl);

}  // namespace test
}  // namespace xrpl
