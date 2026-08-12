#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/ledger/EntryTestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/TicketEntry.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SeqProxy.h>

namespace xrpl {
namespace test {

class TicketEntry_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        using namespace jtx;
        EntryTestEnv e(*this);

        SeqProxy const ticketSeq = SeqProxy::rawTicket(2);

        expectKeylet<TicketEntry>(
            *this,
            e,
            keylet::ticket(e.alice.id(), ticketSeq),
            "ticket(id, ticketSeq)",
            e.alice.id(),
            ticketSeq);

        expectKeylet<TicketEntry>(
            *this, e, keylet::ticket(e.someID()), "ticket(uint256)", e.someID());
    }

public:
    void
    run() override
    {
        testConstructors();
    }
};

BEAST_DEFINE_TESTSUITE(TicketEntry, ledger, xrpl);

}  // namespace test
}  // namespace xrpl
