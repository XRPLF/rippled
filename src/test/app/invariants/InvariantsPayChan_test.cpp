#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/flags.h>
#include <test/jtx/pay.h>
#include <test/jtx/paychan.h>
#include <test/jtx/trust.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>

#include <chrono>
#include <memory>

namespace xrpl::test {

class InvariantsPayChan_test : public InvariantsBase
{
    void
    testValidPaymentChannel()
    {
        using namespace test::jtx;
        using namespace std::literals;
        testcase << "valid payment channel";

        Account const gw("gw");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        // Inserts a token channel from a1 to a2 with the given amount and
        // balance.
        auto const insertChannel = [&](STAmount const& amount, STAmount const& balance) {
            return [=](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sleNew =
                    std::make_shared<SLE>(keylet::payChannel(a1, a2, SeqProxy::rawSequence(1'000)));
                sleNew->setFieldAmount(sfAmount, amount);
                sleNew->setFieldAmount(sfBalance, balance);
                ac.view().insert(sleNew);
                return true;
            };
        };

        // Amount and Balance name different assets.
        doInvariantCheck(
            {{"payment channel amount/balance inconsistent"}}, insertChannel(usd(10), eur(5)));

        // A negative Amount.
        doInvariantCheck(
            {{"payment channel amount/balance inconsistent"}},
            insertChannel(-usd(1).value(), usd(0)));

        // A negative Balance.
        doInvariantCheck(
            {{"payment channel amount/balance inconsistent"}},
            insertChannel(usd(10), -usd(1).value()));

        // A Balance above the Amount.
        doInvariantCheck(
            {{"payment channel amount/balance inconsistent"}}, insertChannel(usd(5), usd(10)));

        // Without featureTokenPaychan the check does not apply.
        doInvariantCheck(
            makeEnv(testableAmendments() - featureTokenPaychan),
            {},
            insertChannel(usd(5), usd(10)),
            XRPAmount{},
            STTx{ttACCOUNT_SET, [](STObject&) {}},
            {tesSUCCESS, tesSUCCESS});

        // An open USD channel from a1 to a2 with 40 of its 100 paid out.
        UInt256 chan;
        Preclose const precloseChannel = [&](Account const& a1, Account const& a2, Env& env) {
            env.fund(XRP(1'000), gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(usd(10'000), a1, a2);
            env.close();
            env(pay(gw, a1, usd(1'000)));
            env.close();
            chan = paychan::channel(a1, a2, env.seq(a1));
            env(paychan::create(a1, a2, usd(100), 100s, a1.pk()));
            env.close();
            env(paychan::claim(a1, chan, usd(40), usd(40)));
            return true;
        };
        auto const updateChannel = [&](STAmount const& amount, STAmount const& balance) {
            return [&, amount, balance](Account const&, Account const&, ApplyContext& ac) {
                auto const sle = ac.view().peek(Keylet{ltPAYCHAN, chan});
                if (!sle)
                    return false;
                sle->setFieldAmount(sfAmount, amount);
                sle->setFieldAmount(sfBalance, balance);
                ac.view().update(sle);
                return true;
            };
        };
        STTx const claim{ttPAYCHAN_CLAIM, [](STObject&) {}};

        // The paid-out Balance decreases.
        doInvariantCheck(
            {{"payment channel amount/balance inconsistent"}},
            updateChannel(usd(100), usd(30)),
            XRPAmount{},
            claim,
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseChannel);

        // The channel's asset changes.
        doInvariantCheck(
            {{"payment channel amount/balance inconsistent"}},
            updateChannel(eur(100), eur(50)),
            XRPAmount{},
            claim,
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseChannel);

        // The paid-out Balance increases within the Amount.
        doInvariantCheck(
            {},
            updateChannel(usd(100), usd(60)),
            XRPAmount{},
            claim,
            {tesSUCCESS, tesSUCCESS},
            precloseChannel);
    }

public:
    void
    run() override
    {
        testValidPaymentChannel();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsPayChan, app, xrpl);

}  // namespace xrpl::test
