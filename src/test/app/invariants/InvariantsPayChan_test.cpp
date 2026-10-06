#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/Account.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/ApplyContext.h>

#include <cstdint>
#include <memory>

namespace xrpl::test {

class InvariantsPayChan_test : public InvariantsBase
{
    void
    testInexactRemainder()
    {
        using namespace test::jtx;
        testcase << "token channel remainder must be exact";

        // Amount 1e20 and Balance 1 satisfy every other channel check, but
        // 1e20 - 1 needs 20 digits and rounds back to 1e20.
        doInvariantCheck(
            {{"Invariant failed: payment channel amount/balance inconsistent"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;
                auto chan = std::make_shared<SLE>(keylet::payChannel(
                    a1.id(), a2.id(), SeqProxy::rawSequence((*sle)[sfSequence])));
                Issue const usd{Currency(0x5553440000000000), a2.id()};
                chan->setAccountID(sfAccount, a1.id());
                chan->setAccountID(sfDestination, a2.id());
                chan->setFieldAmount(sfAmount, STAmount{usd, std::uint64_t{1000000000000000}, 5});
                chan->setFieldAmount(sfBalance, STAmount{usd, 1});
                ac.view().insert(chan);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYCHAN_CLAIM, [](STObject&) {}});
    }

    void
    run() override
    {
        testInexactRemainder();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsPayChan, app, xrpl);

}  // namespace xrpl::test
