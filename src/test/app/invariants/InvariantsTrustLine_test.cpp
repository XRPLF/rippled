#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/flags.h>
#include <test/jtx/pay.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/applySteps.h>

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace xrpl::test {

class InvariantsTrustLine_test : public InvariantsBase
{
    void
    testNoXRPTrustLine()
    {
        using namespace test::jtx;
        testcase << "trust lines with XRP not allowed";
        doInvariantCheck(
            {{"an XRP trust line was created"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                // create simple trust SLE with xrp currency
                auto const sleNew =
                    std::make_shared<SLE>(keylet::trustLine(a1, a2, xrpIssue().currency));
                ac.view().insert(sleNew);
                return true;
            });
    }

    void
    testNoDeepFreezeTrustLinesWithoutFreeze()
    {
        using namespace test::jtx;
        testcase << "trust lines with deep freeze flag without freeze "
                    "not allowed";
        doInvariantCheck(
            {{"a trust line with deep freeze flag without normal freeze was "
              "created"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sleNew =
                    std::make_shared<SLE>(keylet::trustLine(a1, a2, a1["USD"].currency));
                sleNew->setFieldAmount(sfLowLimit, a1["USD"](0));
                sleNew->setFieldAmount(sfHighLimit, a1["USD"](0));

                std::uint32_t uFlags = 0u;
                uFlags |= lsfLowDeepFreeze;
                sleNew->setFieldU32(sfFlags, uFlags);
                ac.view().insert(sleNew);
                return true;
            });

        doInvariantCheck(
            {{"a trust line with deep freeze flag without normal freeze was "
              "created"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sleNew =
                    std::make_shared<SLE>(keylet::trustLine(a1, a2, a1["USD"].currency));
                sleNew->setFieldAmount(sfLowLimit, a1["USD"](0));
                sleNew->setFieldAmount(sfHighLimit, a1["USD"](0));
                std::uint32_t uFlags = 0u;
                uFlags |= lsfHighDeepFreeze;
                sleNew->setFieldU32(sfFlags, uFlags);
                ac.view().insert(sleNew);
                return true;
            });

        doInvariantCheck(
            {{"a trust line with deep freeze flag without normal freeze was "
              "created"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sleNew =
                    std::make_shared<SLE>(keylet::trustLine(a1, a2, a1["USD"].currency));
                sleNew->setFieldAmount(sfLowLimit, a1["USD"](0));
                sleNew->setFieldAmount(sfHighLimit, a1["USD"](0));
                std::uint32_t uFlags = 0u;
                uFlags |= lsfLowDeepFreeze | lsfHighDeepFreeze;
                sleNew->setFieldU32(sfFlags, uFlags);
                ac.view().insert(sleNew);
                return true;
            });

        doInvariantCheck(
            {{"a trust line with deep freeze flag without normal freeze was "
              "created"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sleNew =
                    std::make_shared<SLE>(keylet::trustLine(a1, a2, a1["USD"].currency));
                sleNew->setFieldAmount(sfLowLimit, a1["USD"](0));
                sleNew->setFieldAmount(sfHighLimit, a1["USD"](0));
                std::uint32_t uFlags = 0u;
                uFlags |= lsfLowDeepFreeze | lsfHighFreeze;
                sleNew->setFieldU32(sfFlags, uFlags);
                ac.view().insert(sleNew);
                return true;
            });

        doInvariantCheck(
            {{"a trust line with deep freeze flag without normal freeze was "
              "created"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sleNew =
                    std::make_shared<SLE>(keylet::trustLine(a1, a2, a1["USD"].currency));
                sleNew->setFieldAmount(sfLowLimit, a1["USD"](0));
                sleNew->setFieldAmount(sfHighLimit, a1["USD"](0));
                std::uint32_t uFlags = 0u;
                uFlags |= lsfLowFreeze | lsfHighDeepFreeze;
                sleNew->setFieldU32(sfFlags, uFlags);
                ac.view().insert(sleNew);
                return true;
            });
    }

    void
    testTransfersNotFrozen()
    {
        using namespace test::jtx;
        testcase << "transfers when frozen";

        Account const g1{"G1"};
        // Helper function to establish the trustlines
        auto const createTrustlines = [&](Account const& a1, Account const& a2, Env& env) {
            // Preclose callback to establish trust lines with gateway
            env.fund(XRP(1000), g1);

            env.trust(g1["USD"](10000), a1);
            env.trust(g1["USD"](10000), a2);
            env.close();

            env(pay(g1, a1, g1["USD"](1000)));
            env(pay(g1, a2, g1["USD"](1000)));
            env.close();

            return true;
        };

        auto const a1FrozenByIssuer = [&](Account const& a1, Account const& a2, Env& env) {
            createTrustlines(a1, a2, env);
            env(trust(g1, a1["USD"](10000), tfSetFreeze));
            env.close();

            return true;
        };

        auto const a1DeepFrozenByIssuer = [&](Account const& a1, Account const& a2, Env& env) {
            a1FrozenByIssuer(a1, a2, env);
            env(trust(g1, a1["USD"](10000), tfSetDeepFreeze));
            env.close();

            return true;
        };

        auto const changeBalances = [&](Account const& a1,
                                        Account const& a2,
                                        ApplyContext& ac,
                                        int a1Balance,
                                        int a2Balance) {
            auto const sleA1 = ac.view().peek(keylet::trustLine(a1, g1["USD"]));
            auto const sleA2 = ac.view().peek(keylet::trustLine(a2, g1["USD"]));

            sleA1->setFieldAmount(sfBalance, g1["USD"](a1Balance));
            sleA2->setFieldAmount(sfBalance, g1["USD"](a2Balance));

            ac.view().update(sleA1);
            ac.view().update(sleA2);
        };

        // test: imitating frozen A1 making a payment to A2.
        doInvariantCheck(
            {{"Attempting to move frozen funds"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                changeBalances(a1, a2, ac, -900, -1100);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            a1FrozenByIssuer);

        // test: imitating deep frozen A1 making a payment to A2.
        doInvariantCheck(
            {{"Attempting to move frozen funds"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                changeBalances(a1, a2, ac, -900, -1100);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            a1DeepFrozenByIssuer);

        // test: imitating A2 making a payment to deep frozen A1.
        doInvariantCheck(
            {{"Attempting to move frozen funds"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                changeBalances(a1, a2, ac, -1100, -900);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            a1DeepFrozenByIssuer);
    }

    // Regression cases for the pre-fixCleanup3_5_0 issuer-grouping bug in
    // TransfersNotFrozen. A same-currency cross-issuer trade -- USD.g1 <->
    // USD.g2 via a shared holder a1 -- caused the invariant to spuriously
    // treat a1 as an issuer and reject the trade whenever a1 carried
    // asfGlobalFreeze or a self-freeze on its own side, even though the
    // payment/offer engine never consults those flags for a non-issuer.
    // With the fix, the invariant no longer records changes under
    // endpoints that are pure holders, so these scenarios pass.
    void
    testTransfersNotFrozenCrossIssuer(FeatureBitset features)
    {
        using namespace test::jtx;
        bool const fixEnabled = features[fixCleanup3_5_0];
        testcase << "transfers when frozen: same-currency cross-issuer"
                 << (fixEnabled ? " fix" : "");

        Account const g1{"CIG1"};
        Account const g2{"CIG2"};

        // Preclose: two gateways g1, g2 both issue USD. Both a1 and a2
        // hold each gateway's USD. This is the ordinary two-gateway book
        // that the exploit poisons with a permanent offer.
        auto const createTrustlines = [&](Account const& a1, Account const& a2, Env& env) {
            env.fund(XRP(1000), g1, g2);
            env.close();

            env.trust(g1["USD"](10000), a1);
            env.trust(g1["USD"](10000), a2);
            env.trust(g2["USD"](10000), a1);
            env.trust(g2["USD"](10000), a2);
            env.close();

            env(pay(g1, a1, g1["USD"](1000)));
            env(pay(g1, a2, g1["USD"](1000)));
            env(pay(g2, a1, g2["USD"](1000)));
            env(pay(g2, a2, g2["USD"](1000)));
            env.close();

            return true;
        };

        // Preclose variants that add the different attacker-flag setups.
        auto const a1GlobalFrozen = [&](Account const& a1, Account const& a2, Env& env) {
            if (!createTrustlines(a1, a2, env))
                return false;
            // asfGlobalFreeze is legal for a non-issuer -- the payment
            // engine treats it as a no-op for a1 -- but pre-fix the
            // invariant erroneously enforced it.
            env(fset(a1, asfGlobalFreeze));
            env.close();
            return true;
        };

        auto const a1SelfFrozenOwnSide = [&](Account const& a1, Account const& a2, Env& env) {
            if (!createTrustlines(a1, a2, env))
                return false;
            // a1 sets its OWN side of the trust line to gw. The payment
            // engine only reads the issuer's freeze bit for that line
            // (RippleStateHelpers.cpp:isFrozen), so a1's own side is
            // engine-irrelevant.
            env(trust(a1, g1["USD"](10000), tfSetFreeze));
            env(trust(a1, g2["USD"](10000), tfSetFreeze));
            env.close();
            return true;
        };

        auto const a1SelfDeepFrozenOwnSide = [&](Account const& a1, Account const& a2, Env& env) {
            if (!createTrustlines(a1, a2, env))
                return false;
            env(trust(a1, g1["USD"](10000), tfSetFreeze));
            env(trust(a1, g1["USD"](10000), tfSetDeepFreeze));
            env(trust(a1, g2["USD"](10000), tfSetFreeze));
            env(trust(a1, g2["USD"](10000), tfSetDeepFreeze));
            env.close();
            return true;
        };

        // Simulate the trust-line balance movement of a same-currency
        // cross-issuer swap:
        //   a1 sends 100 USD.g1 to a2, a1 receives 100 USD.g2 from a2.
        // This is what a taker crossing a1's OfferCreate{TakerPays: 100
        // USD.g2, TakerGets: 100 USD.g1} would produce.
        auto const swapBalances = [&](Account const& a1, Account const& a2, ApplyContext& ac) {
            auto const adjust = [&ac](
                                    Account const& holder, Account const& gw, int deltaFromHolder) {
                auto const sle = ac.view().peek(keylet::trustLine(holder, gw["USD"]));
                // Trust-line balance is stored from the Low account's
                // perspective; flip the delta's sign if the holder is High.
                // (sle->at() on a mutable SLE returns a ValueProxy, so read
                // the amount fields via getFieldAmount to reach getIssuer().)
                bool const holderIsLow = sle->getFieldAmount(sfLowLimit).getIssuer() == holder.id();
                int const deltaFromLow = holderIsLow ? deltaFromHolder : -deltaFromHolder;
                STAmount const stored = sle->getFieldAmount(sfBalance);
                sle->setFieldAmount(sfBalance, stored + gw["USD"](deltaFromLow));
                ac.view().update(sle);
            };
            // a1 sends 100 USD.g1 -> a2 receives 100 USD.g1
            adjust(a1, g1, -100);
            adjust(a2, g1, +100);
            // a1 receives 100 USD.g2 <- a2 sends 100 USD.g2
            adjust(a1, g2, +100);
            adjust(a2, g2, -100);
            return true;
        };

        // Pre-fix, the same-currency cross-issuer swap fatal-logs and
        // fires tecINVARIANT_FAILED because {USD, a1} contains one sender
        // (from the (a1, g2) line, sign inverted under a1's group) and one
        // receiver (from the (a1, g1) line, sign inverted), defeating the
        // one-sided short-circuit and letting a1's asfGlobalFreeze /
        // self-freeze bits leak into the check. Post-fix, a1 is not
        // recorded as issuer (perspective balance stays >= 0 on both
        // lines) and the swap goes through cleanly.
        auto const expectLogs = fixEnabled ? std::vector<std::string>{}
                                           : std::vector<std::string>{
                                                 "Attempting to move "
                                                 "frozen funds"};
        auto const expectTers = fixEnabled
            ? std::initializer_list<TER>{tesSUCCESS, tesSUCCESS}
            : std::initializer_list<TER>{tecINVARIANT_FAILED, tefINVARIANT_FAILED};

        // ttOFFER_CREATE mirrors the real attack vector (a1's poison
        // offer at the top of the USD.g1/USD.g2 book) and, unlike ttCLAWBACK
        // etc., has no OverrideFreeze privilege that would mask the check.
        doInvariantCheck(
            makeEnv(features),
            expectLogs,
            swapBalances,
            XRPAmount{},
            STTx{ttOFFER_CREATE, [](STObject&) {}},
            expectTers,
            a1GlobalFrozen);

        doInvariantCheck(
            makeEnv(features),
            expectLogs,
            swapBalances,
            XRPAmount{},
            STTx{ttOFFER_CREATE, [](STObject&) {}},
            expectTers,
            a1SelfFrozenOwnSide);

        doInvariantCheck(
            makeEnv(features),
            expectLogs,
            swapBalances,
            XRPAmount{},
            STTx{ttOFFER_CREATE, [](STObject&) {}},
            expectTers,
            a1SelfDeepFrozenOwnSide);
    }

    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{testableAmendments()};

        testNoXRPTrustLine();
        testNoDeepFreezeTrustLinesWithoutFreeze();
        testTransfersNotFrozen();
        testTransfersNotFrozenCrossIssuer(all - fixCleanup3_5_0);
        testTransfersNotFrozenCrossIssuer(all);
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsTrustLine, app, xrpl);

}  // namespace xrpl::test
