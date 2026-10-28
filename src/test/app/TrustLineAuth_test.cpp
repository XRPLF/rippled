#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/flags.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/token.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace xrpl::test {

// The payment engine's authorization gate used to fire only on an empty trust
// line. An unauthorized line that already carried a balance could keep
// receiving, and a payment crossing zero could create such a balance
// (XRPLF/rippled issue #5450). Post fixCleanup3_5_0 an unauthorized line can
// be redeemed but never credited. These tests pin both eras.
class TrustLineAuth_test : public beast::unit_test::Suite
{
    static jtx::Env
    makeEnv(beast::unit_test::Suite& suite, bool withCleanup)
    {
        using namespace jtx;
        return withCleanup ? Env{suite} : Env{suite, testableAmendments() - fixCleanup3_5_0};
    }

    void
    testEmptyLineReceive()
    {
        using namespace jtx;
        testcase("unauthorized empty line cannot receive");

        Account const g1{"G1"};
        Account const a1{"A1"};

        for (bool const withCleanup : {true, false})
        {
            Env env = makeEnv(*this, withCleanup);
            env.fund(XRP(1000), a1, g1);
            env(fset(g1, asfRequireAuth));
            env.close();
            // a1 opens the line; g1 never authorizes it.
            env.trust(g1["USD"](10000), a1);
            env.close();

            // tecNO_AUTH is final post amendment; terNO_AUTH left a dry path before.
            env(pay(g1, a1, g1["USD"](5)), Ter(withCleanup ? TER{tecNO_AUTH} : TER{tecPATH_DRY}));
            env.close();
            BEAST_EXPECT(env.balance(a1, g1["USD"]) == g1["USD"](0));

            // Authorization repairs it either way.
            env(trust(g1, g1["USD"](0), a1, tfSetfAuth));
            env.close();
            env(pay(g1, a1, g1["USD"](5)));
            env.close();
            BEAST_EXPECT(env.balance(a1, g1["USD"]) == g1["USD"](5));
        }
    }

    void
    testZeroCrossing()
    {
        using namespace jtx;
        testcase("payment crossing zero cannot create an unauthorized balance");

        Account const g1{"G1"};
        Account const a1{"A1"};

        for (bool const withCleanup : {true, false})
        {
            Env env = makeEnv(*this, withCleanup);
            env.fund(XRP(1000), a1, g1);
            env(fset(g1, asfRequireAuth));
            env.close();
            // Both sides extend trust for USD on the one shared line; g1
            // never authorizes a1.
            env(trust(g1, a1["USD"](1000)));
            env(trust(a1, g1["USD"](1000)));
            env.close();
            // a1 issues 5 USD to g1. a1 requires no authorization, so the
            // shared line now carries a balance in g1's favor.
            env(pay(a1, g1, a1["USD"](5)));
            env.close();
            // g1 pays 10 USD: 5 redeem what g1 holds, the other 5 would make
            // the unauthorized a1 a holder of g1's IOU.
            if (withCleanup)
            {
                // Only the 5 redeemable units flow, so the payment comes up
                // short and fails without touching the balance.
                env(pay(g1, a1, g1["USD"](10)), Ter(tecPATH_PARTIAL));
                env.close();
                BEAST_EXPECT(env.balance(a1, g1["USD"]) == g1["USD"](-5));

                // Redeeming exactly what g1 holds drains the line to zero.
                env(pay(g1, a1, g1["USD"](5)));
                env.close();
                BEAST_EXPECT(env.balance(a1, g1["USD"]) == g1["USD"](0));

                // Nothing is left to redeem, so this is a plain receive.
                env(pay(g1, a1, g1["USD"](1)), Ter(tecNO_AUTH));
                env.close();
                BEAST_EXPECT(env.balance(a1, g1["USD"]) == g1["USD"](0));
            }
            else
            {
                env(pay(g1, a1, g1["USD"](10)));
                env.close();
                BEAST_EXPECT(env.balance(a1, g1["USD"]) == g1["USD"](5));
            }
        }
    }

    void
    testLegacyEndToEnd()
    {
        using namespace jtx;
        testcase("legacy unauthorized balance end to end");

        Account const g1{"G1"};
        Account const a1{"A1"};
        Account const a2{"A2"};

        // Start pre-amendment with the NFT authorization gap open, the flow
        // behind the unauthorized balances of XRPLF/rippled issue #5450.
        Env env{*this, testableAmendments() - fixEnforceNFTokenTrustlineV2 - fixCleanup3_5_0};
        env.fund(XRP(10000), g1, a1, a2);
        env(fset(g1, asfRequireAuth));
        env.close();
        env(trust(a1, g1["USD"](10000)));
        env(trust(g1, g1["USD"](0), a1, tfSetfAuth));
        env(pay(g1, a1, g1["USD"](1000)));
        env(trust(a2, g1["USD"](10000)));
        env.close();

        // a2 sells an NFT to the authorized a1 for USD and ends up holding
        // 10 USD without authorization.
        uint256 const nftID = token::getNextID(env, a2, 0u, tfTransferable);
        env(token::mint(a2, 0), Txflags(tfTransferable));
        env.close();
        uint256 const sellIdx = keylet::nftokenOffer(a2, SeqProxy::rawSequence(env.seq(a2))).key;
        env(token::createOffer(a2, nftID, g1["USD"](10)), Txflags(tfSellNFToken));
        env.close();
        env(token::acceptSellOffer(a1, sellIdx));
        env.close();
        BEAST_EXPECT(env.balance(a2, g1["USD"]) == g1["USD"](10));

        // The amendment activates with the legacy balance in place.
        env.enableFeature(fixCleanup3_5_0);
        env.close();
        BEAST_EXPECT(env.balance(a2, g1["USD"]) == g1["USD"](10));

        // The line cannot grow: a nonzero balance no longer exempts it.
        env(pay(a1, a2, g1["USD"](5)), Ter(tecNO_AUTH));
        env.close();
        BEAST_EXPECT(env.balance(a2, g1["USD"]) == g1["USD"](10));

        // RequireAuth governs holding, not sending: the legacy balance stays
        // spendable to an authorized holder, and freeze is the issuer's tool.
        env(pay(a2, a1, g1["USD"](4)));
        env.close();
        BEAST_EXPECT(env.balance(a2, g1["USD"]) == g1["USD"](6));

        // Returning the rest to the issuer drains the line for good.
        env(pay(a2, g1, g1["USD"](6)));
        env.close();
        BEAST_EXPECT(env.balance(a2, g1["USD"]) == g1["USD"](0));

        // At zero it is an ordinary unauthorized line: nothing comes back.
        env(pay(g1, a2, g1["USD"](1)), Ter(tecNO_AUTH));
        env.close();
        BEAST_EXPECT(env.balance(a2, g1["USD"]) == g1["USD"](0));
    }

    void
    run() override
    {
        testEmptyLineReceive();
        testZeroCrossing();
        testLegacyEndToEnd();
    }
};

BEAST_DEFINE_TESTSUITE(TrustLineAuth, app, xrpl);

}  // namespace xrpl::test
