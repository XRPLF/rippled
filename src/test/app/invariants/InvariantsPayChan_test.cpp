#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/flags.h>
#include <test/jtx/pay.h>
#include <test/jtx/paychan.h>
#include <test/jtx/trust.h>

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
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/ApplyContext.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace xrpl::test {

class InvariantsPayChan_test : public InvariantsBase
{
    jtx::Account const gw_{"gw"};
    std::uint32_t seq_ = 0;

    // a1 opens a channel to a2 holding 10 USD issued by gw_ or 10 XRP; the
    // source then claims paidOut of it, leaving the channel open.
    Preclose
    openChannel(bool token, std::int64_t paidOut)
    {
        return
            [this, token, paidOut](jtx::Account const& a1, jtx::Account const& a2, jtx::Env& env) {
                using namespace test::jtx;
                using namespace std::chrono_literals;
                IOU const usd = gw_["USD"];
                if (token)
                {
                    env.fund(XRP(1000), gw_);
                    env(fset(gw_, asfAllowTrustLineLocking));
                    env(trust(a1, usd(1000)));
                    env(trust(a2, usd(1000)));
                    env(pay(gw_, a1, usd(100)));
                }
                STAmount const amount = token ? STAmount{usd(10)} : STAmount{XRP(10)};
                seq_ = env.seq(a1);
                env(paychan::create(a1, a2, amount, 100s, a1.pk()));
                if (paidOut != 0)
                {
                    STAmount const paid = token ? STAmount{usd(paidOut)} : STAmount{XRP(paidOut)};
                    env(paychan::claim(a1, paychan::channel(a1, a2, seq_), paid, paid));
                }
                return true;
            };
    }

    // Rewrites the channel's Amount and Balance in place.
    Precheck
    setChannel(STAmount const& amount, STAmount const& balance)
    {
        return [this, amount, balance](
                   jtx::Account const& a1, jtx::Account const& a2, ApplyContext& ac) {
            auto const chan =
                ac.view().peek(keylet::payChannel(a1.id(), a2.id(), SeqProxy::rawSequence(seq_)));
            if (!chan)
                return false;
            chan->setFieldAmount(sfAmount, amount);
            chan->setFieldAmount(sfBalance, balance);
            ac.view().update(chan);
            return true;
        };
    }

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
                auto const chan = std::make_shared<SLE>(keylet::payChannel(
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
    testAssetChange()
    {
        using namespace test::jtx;
        using namespace std::chrono_literals;
        testcase << "payment channel asset never changes";

        Account const gw2{"gw2"};
        IOU const usd = gw_["USD"];

        std::vector<std::string> const changed{"Invariant failed: payment channel asset changed"};
        STTx const claimTx{ttPAYCHAN_CLAIM, [](STObject&) {}};

        // One token for another.
        doInvariantCheck(
            changed,
            setChannel(gw_["EUR"](10), gw_["EUR"](0)),
            XRPAmount{},
            claimTx,
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            openChannel(true, 0));

        // Same currency, another issuer.
        doInvariantCheck(
            changed,
            setChannel(gw2["USD"](10), gw2["USD"](0)),
            XRPAmount{},
            claimTx,
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            openChannel(true, 0));

        // A drained XRP channel turned into a token channel.
        doInvariantCheck(
            changed,
            setChannel(usd(10), usd(10)),
            XRPAmount{},
            claimTx,
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            openChannel(false, 10));

        // A drained token channel turned into an XRP channel.
        doInvariantCheck(
            changed,
            setChannel(XRP(10), XRP(10)),
            XRPAmount{},
            claimTx,
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            openChannel(true, 10));

        // A same-asset balance change is not an asset change.
        doInvariantCheck(
            {},
            setChannel(usd(10), usd(5)),
            XRPAmount{},
            claimTx,
            {tesSUCCESS, tesSUCCESS},
            openChannel(true, 0));
    }

    void
    testAmountDecrease()
    {
        using namespace test::jtx;
        testcase << "only a clawback lowers a channel amount";

        IOU const usd = gw_["USD"];
        std::vector<std::string> const decreased{
            "Invariant failed: payment channel amount decreased",
        };
        STTx const claimTx{ttPAYCHAN_CLAIM, [](STObject&) {}};
        STTx const clawbackTx{ttPAYCHAN_CLAWBACK, [](STObject&) {}};

        // A claim may not lower Amount.
        doInvariantCheck(
            decreased,
            setChannel(usd(8), usd(0)),
            XRPAmount{},
            claimTx,
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            openChannel(true, 0));

        // A clawback that lowers Amount may not change Balance.
        doInvariantCheck(
            decreased,
            setChannel(usd(8), usd(1)),
            XRPAmount{},
            clawbackTx,
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            openChannel(true, 0));

        // A clawback lowers Amount and keeps Balance.
        doInvariantCheck(
            {},
            setChannel(usd(8), usd(0)),
            XRPAmount{},
            clawbackTx,
            {tesSUCCESS, tesSUCCESS},
            openChannel(true, 0));
    }

    void
    run() override
    {
        testInexactRemainder();
        testAssetChange();
        testAmountDecrease();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsPayChan, app, xrpl);

}  // namespace xrpl::test
