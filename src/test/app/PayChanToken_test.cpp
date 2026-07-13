#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/paychan.h>
#include <test/jtx/rate.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/Dir.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>

namespace xrpl::test {
struct PayChanToken_test : public beast::unit_test::Suite
{
    void
    testIOUEnablement(FeatureBitset features)
    {
        testcase("IOU Enablement");

        using namespace jtx;
        using namespace std::chrono;

        for (bool const withTokenPaychan : {false, true})
        {
            auto const amend = withTokenPaychan ? features : features - featureTokenPaychan;
            Env env{*this, amend};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const usd = gw["USD"];
            env.fund(XRP(5'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(usd(10'000), alice, bob);
            env.close();
            env(pay(gw, alice, usd(5'000)));
            env(pay(gw, bob, usd(5'000)));
            env.close();

            auto const openResult = withTokenPaychan ? Ter(tesSUCCESS) : Ter(temBAD_AMOUNT);
            auto const closeResult = withTokenPaychan ? Ter(tesSUCCESS) : Ter(tecNO_TARGET);

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, usd(1'000), settleDelay, pk), openResult);
            env.close();
            env(paychan::fund(alice, chan, usd(1'000)), openResult);
            env.close();
            env(paychan::claim(bob, chan), Txflags(tfClose), closeResult);
            env.close();
        }
    }

    void
    testIOUAllowLockingFlag(FeatureBitset features)
    {
        testcase("IOU Allow Locking Flag");

        using namespace jtx;
        using namespace std::chrono;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const usd = gw["USD"];
        env.fund(XRP(5000), alice, bob, gw);
        env(fset(gw, asfAllowTrustLineLocking));
        env.close();
        env.trust(usd(10'000), alice, bob);
        env.close();
        env(pay(gw, alice, usd(5'000)));
        env(pay(gw, bob, usd(5'000)));
        env.close();

        // Create PayChan
        auto const pk = alice.pk();
        auto const settleDelay = 100s;
        auto const chan = paychan::channel(alice, bob, env.seq(alice));
        env(paychan::create(alice, bob, usd(1'000), settleDelay, pk), Ter(tesSUCCESS));
        env.close();

        // Clear the asfAllowTrustLineLocking flag
        env(fclear(gw, asfAllowTrustLineLocking));
        env.close();
        env.require(Nflags(gw, asfAllowTrustLineLocking));

        // Cannot Create PayChan without asfAllowTrustLineLocking
        env(paychan::create(alice, bob, usd(1'000), settleDelay, pk), Ter(tecNO_PERMISSION));
        env.close();

        // Cannot Fund PayChan without asfAllowTrustLineLocking; funding is
        // subject to the same issuer controls as create
        env(paychan::fund(alice, chan, usd(1'000)), Ter(tecNO_PERMISSION));
        env.close();

        // Can claim the paychan created before the flag was cleared
        auto const sig = paychan::signClaimAuth(alice.pk(), alice.sk(), chan, usd(1'000));
        env(paychan::claim(bob, chan, usd(1'000), usd(1'000), Slice(sig), alice.pk()),
            Ter(tesSUCCESS));
        env.close();
    }

    void
    testIOUCreatePreflight(FeatureBitset features)
    {
        testcase("IOU Create Preflight");
        using namespace test::jtx;
        using namespace std::literals;

        // temBAD_FEE: Exercises invalid preflight1.
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const usd = gw["USD"];
            env.fund(XRP(5'000), alice, bob, gw);

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, usd(1), settleDelay, pk),
                Fee(XRP(-1)),
                Ter(temBAD_FEE));
            env.close();
        }

        // temBAD_AMOUNT: amount <= 0
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const usd = gw["USD"];
            env.fund(XRP(5'000), alice, bob, gw);

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, usd(-1), settleDelay, pk), Ter(temBAD_AMOUNT));
            env.close();
        }

        // temBAD_CURRENCY: badCurrency() == amount.getCurrency()
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const bad = IOU(gw, badCurrency());
            env.fund(XRP(5'000), alice, bob, gw);

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, bad(1), settleDelay, pk), Ter(temBAD_CURRENCY));
            env.close();
        }
    }

    void
    testIOUCreatePreclaim(FeatureBitset features)
    {
        testcase("IOU Create Preclaim");
        using namespace test::jtx;
        using namespace std::literals;

        // tecNO_PERMISSION: issuer is the same as the account
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const usd = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);

            env(paychan::create(gw, alice, usd(1), 100s, alice.pk()), Ter(tecNO_PERMISSION));
            env.close();
        }

        // tecNO_ISSUER: Issuer does not exist
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const usd = gw["USD"];
            env.fund(XRP(5000), alice, bob);
            env.close();
            env.memoize(gw);

            env(paychan::create(alice, bob, usd(1), 100s, alice.pk()), Ter(tecNO_ISSUER));
            env.close();
        }

        // tecNO_PERMISSION: asfAllowTrustLineLocking is not set
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const usd = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env.close();
            env.trust(usd(10'000), alice, bob);
            env.close();
            env(pay(gw, alice, usd(5000)));
            env(pay(gw, bob, usd(5000)));
            env.close();

            env(paychan::create(gw, alice, usd(1), 100s, alice.pk()), Ter(tecNO_PERMISSION));
            env.close();
        }

        // tecNO_LINE: account does not have a trustline to the issuer
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const usd = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();

            env(paychan::create(alice, bob, usd(1), 100s, alice.pk()), Ter(tecNO_LINE));
            env.close();
        }

        // tecNO_PERMISSION: Not testable
        // tecNO_PERMISSION: Not testable
        // tecNO_AUTH: requireAuth
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const usd = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env(fset(gw, asfRequireAuth));
            env.close();
            env.trust(usd(10'000), alice, bob);
            env.close();

            env(paychan::create(alice, bob, usd(1), 100s, alice.pk()), Ter(tecNO_AUTH));
            env.close();
        }

        // tecNO_AUTH: requireAuth
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const usd = gw["USD"];
            auto const aliceUSD = alice["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env(fset(gw, asfRequireAuth));
            env.close();
            env(trust(gw, aliceUSD(10'000)), Txflags(tfSetfAuth));
            env.trust(usd(10'000), alice, bob);
            env.close();

            env(paychan::create(alice, bob, usd(1), 100s, alice.pk()), Ter(tecNO_AUTH));
            env.close();
        }

        // tecFROZEN: account is frozen
        {
            // Env Setup
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const usd = gw["USD"];
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env(trust(alice, usd(100'000)));
            env(trust(bob, usd(100'000)));
            env.close();
            env(pay(gw, alice, usd(10'000)));
            env(pay(gw, bob, usd(10'000)));
            env.close();

            // set freeze on alice trustline
            env(trust(gw, usd(10'000), alice, tfSetFreeze));
            env.close();

            env(paychan::create(alice, bob, usd(1), 100s, alice.pk()), Ter(tecFROZEN));
            env.close();
        }

        // tecFROZEN: dest is frozen
        {
            // Env Setup
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const usd = gw["USD"];
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env(trust(alice, usd(100'000)));
            env(trust(bob, usd(100'000)));
            env.close();
            env(pay(gw, alice, usd(10'000)));
            env(pay(gw, bob, usd(10'000)));
            env.close();

            // set freeze on bob trustline
            env(trust(gw, usd(10'000), bob, tfSetFreeze));
            env.close();

            env(paychan::create(alice, bob, usd(1), 100s, alice.pk()), Ter(tecFROZEN));
            env.close();
        }

        // tecINSUFFICIENT_FUNDS
        {
            // Env Setup
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const usd = gw["USD"];
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env(trust(alice, usd(100'000)));
            env(trust(bob, usd(100'000)));
            env.close();

            env(paychan::create(alice, bob, usd(1), 100s, alice.pk()), Ter(tecINSUFFICIENT_FUNDS));
            env.close();
        }

        // tecINSUFFICIENT_FUNDS
        {
            // Env Setup
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const usd = gw["USD"];
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env(trust(alice, usd(100'000)));
            env(trust(bob, usd(100'000)));
            env.close();
            env(pay(gw, alice, usd(10'000)));
            env(pay(gw, bob, usd(10'000)));
            env.close();

            env(paychan::create(alice, bob, usd(10'001), 100s, alice.pk()),
                Ter(tecINSUFFICIENT_FUNDS));
            env.close();
        }

        // tecPRECISION_LOSS
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const usd = gw["USD"];
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(usd(100000000000000000), alice);
            env.trust(usd(100000000000000000), bob);
            env.close();
            env(pay(gw, alice, usd(10000000000000000)));
            env(pay(gw, bob, usd(1)));
            env.close();

            // With a larger mantissa the amounts remain addable
            bool const largeMantissa =
                features[featureSingleAssetVault] || features[featureLendingProtocol];

            // alice cannot create paychan for 1/10 iou - precision loss
            env(paychan::create(alice, bob, usd(1), 100s, alice.pk()),
                Ter(largeMantissa ? (TER)tesSUCCESS : (TER)tecPRECISION_LOSS));
            env.close();
        }
    }

    void
    testIOUClaimPreclaim(FeatureBitset features)
    {
        testcase("IOU Claim Preclaim");
        using namespace test::jtx;
        using namespace std::literals;

        // tecNO_AUTH: requireAuth set: dest not authorized
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const usd = gw["USD"];
            auto const aliceUSD = alice["USD"];
            auto const bobUSD = bob["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env(fset(gw, asfRequireAuth));
            env.close();
            env(trust(gw, aliceUSD(10'000)), Txflags(tfSetfAuth));
            env(trust(gw, bobUSD(10'000)), Txflags(tfSetfAuth));
            env.trust(usd(10'000), alice, bob);
            env.close();
            env(pay(gw, alice, usd(10'000)));
            env(pay(gw, bob, usd(10'000)));
            env.close();

            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, usd(1), 100s, alice.pk()), Ter(tesSUCCESS));
            env.close();

            env(pay(bob, gw, usd(10'000)));
            env(trust(gw, bobUSD(0)), Txflags(tfSetfAuth));
            env(trust(bob, usd(0)));
            env.close();

            env.trust(usd(10'000), bob);
            env.close();

            // bob cannot claim because he is not authorized
            auto const sig = paychan::signClaimAuth(alice.pk(), alice.sk(), chan, usd(1));
            env(paychan::claim(bob, chan, usd(1), usd(1), Slice(sig), alice.pk()), Ter(tecNO_AUTH));
            env.close();
        }

        // tecFROZEN: issuer has deep frozen the dest
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const usd = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(usd(10'000), alice, bob);
            env.close();
            env(pay(gw, alice, usd(10'000)));
            env(pay(gw, bob, usd(10'000)));
            env.close();

            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, usd(1), 100s, alice.pk()), Ter(tesSUCCESS));
            env.close();

            // set freeze on bob trustline
            env(trust(gw, usd(10'000), bob, tfSetFreeze | tfSetDeepFreeze));

            // bob cannot claim because of deep freeze
            auto const sig = paychan::signClaimAuth(alice.pk(), alice.sk(), chan, usd(1));
            env(paychan::claim(bob, chan, usd(1), usd(1), Slice(sig), alice.pk()), Ter(tecFROZEN));
            env.close();
        }
    }

    void
    testIOUClaimDoApply(FeatureBitset features)
    {
        testcase("IOU Claim Do Apply");
        using namespace test::jtx;
        using namespace std::literals;

        // tecNO_LINE_INSUF_RESERVE: insufficient reserve to create line
        {
            Env env{*this, features};
            auto const acctReserve = env.current()->fees().accountReserve(0, 1);
            auto const incReserve = env.current()->fees().increment;
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const usd = gw["USD"];
            env.fund(XRP(5000), alice, gw);
            env.fund(acctReserve + (incReserve - 1), bob);
            env.close();
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(usd(10'000), alice);
            env.close();
            env(pay(gw, alice, usd(10'000)));
            env.close();

            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, usd(1), 100s, alice.pk()), Ter(tesSUCCESS));
            env.close();

            // bob cannot claim because insufficient reserve to create line
            auto const sig = paychan::signClaimAuth(alice.pk(), alice.sk(), chan, usd(1));
            env(paychan::claim(bob, chan, usd(1), usd(1), Slice(sig), alice.pk()),
                Ter(tecNO_LINE_INSUF_RESERVE));
            env.close();
        }

        // tecNO_LINE: alice submits; claim IOU not created
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const usd = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env.close();
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(usd(10'000), alice);
            env.close();
            env(pay(gw, alice, usd(10'000)));
            env.close();

            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, usd(1), 100s, alice.pk()), Ter(tesSUCCESS));
            env.close();

            // alice cannot claim because bob does not have a trustline
            env(paychan::claim(alice, chan, usd(1), usd(1)), Ter(tecNO_LINE));
            env.close();
        }

        // tecLIMIT_EXCEEDED: alice submits; IOU Limit < balance + amount
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const usd = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env.close();
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(usd(1000), alice, bob);
            env.close();
            env(pay(gw, alice, usd(1000)));
            env.close();

            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, usd(5), 100s, alice.pk()), Ter(tesSUCCESS));
            env.close();

            env.trust(usd(1), bob);
            env.close();

            // alice cannot claim because bobs limit is too low
            env(paychan::claim(alice, chan, usd(5), usd(5)), Ter(tecLIMIT_EXCEEDED));
            env.close();
        }

        // tesSUCCESS: bob submits; IOU Limit < balance + amount
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const usd = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env.close();
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(usd(1000), alice, bob);
            env.close();
            env(pay(gw, alice, usd(1000)));
            env.close();

            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, usd(5), 100s, alice.pk()), Ter(tesSUCCESS));
            env.close();

            env.trust(usd(1), bob);
            env.close();

            auto const bobPreLimit = env.limit(bob, usd);

            // bob can claim even if bobs limit is too low; the limit only
            // protects against unsolicited holdings and bob consents by
            // submitting the claim himself
            auto const sig = paychan::signClaimAuth(alice.pk(), alice.sk(), chan, usd(5));
            env(paychan::claim(bob, chan, usd(5), usd(5), Slice(sig), alice.pk()), Ter(tesSUCCESS));
            env.close();

            // bob received the claimed amount; his limit is not changed
            BEAST_EXPECT(env.balance(bob, usd) == usd(5));
            BEAST_EXPECT(env.limit(bob, usd) == bobPreLimit);
        }
    }

    void
    testIOUBalances(FeatureBitset features)
    {
        testcase("IOU Balances");

        using namespace jtx;
        using namespace std::chrono;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const usd = gw["USD"];
        env.fund(XRP(5000), alice, bob, gw);
        env(fset(gw, asfAllowTrustLineLocking));
        env.close();
        env.trust(usd(10'000), alice, bob);
        env.close();
        env(pay(gw, alice, usd(5'000)));
        env(pay(gw, bob, usd(5'000)));
        env.close();

        auto const outstandingUSD = usd(10'000);

        // Create & Claim (Dest) PayChan
        auto const chan = paychan::channel(alice, bob, env.seq(alice));
        {
            auto const preAliceUSD = env.balance(alice, usd);
            auto const preBobUSD = env.balance(bob, usd);
            env(paychan::create(alice, bob, usd(1'000), 1s, alice.pk()), Ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, usd) == preAliceUSD - usd(1'000));
            BEAST_EXPECT(env.balance(bob, usd) == preBobUSD);
            BEAST_EXPECT(issuerBalance(env, gw, usd) == outstandingUSD - usd(1'000));
            BEAST_EXPECT(issuerEscrowed(env, gw, usd) == usd(1'000));
        }
        {
            auto const preAliceUSD = env.balance(alice, usd);
            auto const preBobUSD = env.balance(bob, usd);
            auto const sig = paychan::signClaimAuth(alice.pk(), alice.sk(), chan, usd(1'000));
            env(paychan::claim(bob, chan, usd(1'000), usd(1'000), Slice(sig), alice.pk()),
                Ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, usd) == preAliceUSD);
            BEAST_EXPECT(env.balance(bob, usd) == preBobUSD + usd(1'000));
            BEAST_EXPECT(issuerBalance(env, gw, usd) == outstandingUSD);
            BEAST_EXPECT(issuerEscrowed(env, gw, usd) == usd(0));
        }

        // Create & Claim (Account) PayChan
        auto const chan2 = paychan::channel(alice, bob, env.seq(alice));
        {
            auto const preAliceUSD = env.balance(alice, usd);
            auto const preBobUSD = env.balance(bob, usd);
            env(paychan::create(alice, bob, usd(1'000), 100s, alice.pk()), Ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, usd) == preAliceUSD - usd(1'000));
            BEAST_EXPECT(env.balance(bob, usd) == preBobUSD);
            BEAST_EXPECT(issuerBalance(env, gw, usd) == outstandingUSD - usd(1'000));
            BEAST_EXPECT(issuerEscrowed(env, gw, usd) == usd(1'000));
        }
        {
            auto const preAliceUSD = env.balance(alice, usd);
            auto const preBobUSD = env.balance(bob, usd);
            env(paychan::claim(alice, chan2, usd(1'000), usd(1'000)),
                Txflags(tfClose),
                Ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, usd) == preAliceUSD);
            BEAST_EXPECT(env.balance(bob, usd) == preBobUSD + usd(1'000));
            BEAST_EXPECT(issuerBalance(env, gw, usd) == outstandingUSD);
            BEAST_EXPECT(issuerEscrowed(env, gw, usd) == usd(0));
        }
    }

    void
    testIOUMetaAndOwnership(FeatureBitset features)
    {
        using namespace jtx;
        using namespace std::chrono;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const usd = gw["USD"];
        {
            testcase("IOU Metadata to other");

            Env env{*this, features};
            env.fund(XRP(5000), alice, bob, carol, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(usd(10'000), alice, bob, carol);
            env.close();
            env(pay(gw, alice, usd(5000)));
            env(pay(gw, bob, usd(5000)));
            env(pay(gw, carol, usd(5000)));
            env.close();
            auto const aseq = env.seq(alice);
            auto const bseq = env.seq(bob);

            auto const pk = alice.pk();
            auto const pk2 = bob.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, usd(1'000), settleDelay, pk));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] == static_cast<std::uint8_t>(tesSUCCESS));
            env.close();
            env(paychan::create(bob, carol, usd(1'000), settleDelay, pk2));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] == static_cast<std::uint8_t>(tesSUCCESS));
            env.close();

            auto const ab = env.le(keylet::payChannel(alice.id(), bob.id(), aseq));
            BEAST_EXPECT(ab);

            auto const bc = env.le(keylet::payChannel(bob.id(), carol.id(), bseq));
            BEAST_EXPECT(bc);

            {
                xrpl::Dir const aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 2);
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(aod.begin(), aod.end(), ab) != aod.end());

                xrpl::Dir const bod(*env.current(), keylet::ownerDir(bob.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 3);
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(bod.begin(), bod.end(), ab) != bod.end());
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(bod.begin(), bod.end(), bc) != bod.end());

                xrpl::Dir const cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 2);
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(cod.begin(), cod.end(), bc) != cod.end());

                xrpl::Dir const iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 5);
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(iod.begin(), iod.end(), ab) != iod.end());
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(iod.begin(), iod.end(), bc) != iod.end());
            }

            auto const chanAb = paychan::channel(alice, bob, aseq);
            env(paychan::claim(alice, chanAb, usd(1'000), usd(1'000)), Txflags(tfClose));
            {
                BEAST_EXPECT(!env.le(keylet::payChannel(alice.id(), bob.id(), aseq)));
                BEAST_EXPECT(env.le(keylet::payChannel(bob.id(), carol.id(), bseq)));

                xrpl::Dir const aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 1);
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(aod.begin(), aod.end(), ab) == aod.end());

                xrpl::Dir const bod(*env.current(), keylet::ownerDir(bob.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 2);
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(bod.begin(), bod.end(), ab) == bod.end());
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(bod.begin(), bod.end(), bc) != bod.end());

                xrpl::Dir const cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 2);

                xrpl::Dir const iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 4);
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(iod.begin(), iod.end(), ab) == iod.end());
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(iod.begin(), iod.end(), bc) != iod.end());
            }

            env.close();
            auto const chanBc = paychan::channel(bob, carol, bseq);
            env(paychan::claim(bob, chanBc, usd(1'000), usd(1'000)), Txflags(tfClose));
            {
                BEAST_EXPECT(!env.le(keylet::payChannel(alice.id(), bob.id(), aseq)));
                BEAST_EXPECT(!env.le(keylet::payChannel(bob.id(), carol.id(), bseq)));

                xrpl::Dir const aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 1);
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(aod.begin(), aod.end(), ab) == aod.end());

                xrpl::Dir const bod(*env.current(), keylet::ownerDir(bob.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 1);
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(bod.begin(), bod.end(), ab) == bod.end());
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(bod.begin(), bod.end(), bc) == bod.end());

                xrpl::Dir const cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 1);

                xrpl::Dir const iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 3);
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(iod.begin(), iod.end(), ab) == iod.end());
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(iod.begin(), iod.end(), bc) == iod.end());
            }
        }

        {
            testcase("IOU Metadata to issuer");

            Env env{*this, features};
            env.fund(XRP(5000), alice, carol, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(usd(10'000), alice, carol);
            env.close();
            env(pay(gw, alice, usd(5000)));
            env(pay(gw, carol, usd(5000)));
            env.close();
            auto const aseq = env.seq(alice);

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, gw, usd(1'000), settleDelay, pk));

            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] == static_cast<std::uint8_t>(tesSUCCESS));
            env.close();
            env(paychan::create(gw, carol, usd(1'000), settleDelay, alice.pk()),
                Ter(tecNO_PERMISSION));
            env.close();

            auto const ag = env.le(keylet::payChannel(alice.id(), gw.id(), aseq));
            BEAST_EXPECT(ag);

            {
                xrpl::Dir const aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 2);
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(aod.begin(), aod.end(), ag) != aod.end());

                xrpl::Dir const cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 1);

                xrpl::Dir const iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 3);
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(iod.begin(), iod.end(), ag) != iod.end());
            }

            auto const chanAg = paychan::channel(alice, gw, aseq);
            env(paychan::claim(alice, chanAg, usd(1'000), usd(1'000)), Txflags(tfClose));
            {
                BEAST_EXPECT(!env.le(keylet::payChannel(alice.id(), gw.id(), aseq)));

                xrpl::Dir const aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 1);
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(aod.begin(), aod.end(), ag) == aod.end());

                xrpl::Dir const cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 1);

                xrpl::Dir const iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 2);
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(iod.begin(), iod.end(), ag) == iod.end());
            }
        }
    }

    void
    testIOURippleState(FeatureBitset features)
    {
        testcase("IOU RippleState");
        using namespace test::jtx;
        using namespace std::literals;

        struct TestAccountData
        {
            Account src;
            Account dst;
            Account gw;
            bool hasTrustline;
            bool negative;
        };

        std::array<TestAccountData, 8> const tests = {{
            // src > dst && src > issuer && dst no trustline
            {.src = Account("alice2"),
             .dst = Account("bob0"),
             .gw = Account{"gw0"},
             .hasTrustline = false,
             .negative = true},
            // src < dst && src < issuer && dst no trustline
            {.src = Account("carol0"),
             .dst = Account("dan1"),
             .gw = Account{"gw1"},
             .hasTrustline = false,
             .negative = false},
            // dst > src && dst > issuer && dst no trustline
            {.src = Account("dan1"),
             .dst = Account("alice2"),
             .gw = Account{"gw0"},
             .hasTrustline = false,
             .negative = true},
            // dst < src && dst < issuer && dst no trustline
            {.src = Account("bob0"),
             .dst = Account("carol0"),
             .gw = Account{"gw1"},
             .hasTrustline = false,
             .negative = false},
            // src > dst && src > issuer && dst has trustline
            {.src = Account("alice2"),
             .dst = Account("bob0"),
             .gw = Account{"gw0"},
             .hasTrustline = true,
             .negative = true},
            // src < dst && src < issuer && dst has trustline
            {.src = Account("carol0"),
             .dst = Account("dan1"),
             .gw = Account{"gw1"},
             .hasTrustline = true,
             .negative = false},
            // dst > src && dst > issuer && dst has trustline
            {.src = Account("dan1"),
             .dst = Account("alice2"),
             .gw = Account{"gw0"},
             .hasTrustline = true,
             .negative = true},
            // dst < src && dst < issuer && dst has trustline
            {.src = Account("bob0"),
             .dst = Account("carol0"),
             .gw = Account{"gw1"},
             .hasTrustline = true,
             .negative = false},
        }};

        for (auto const& t : tests)
        {
            Env env{*this, features};
            auto const usd = t.gw["USD"];
            env.fund(XRP(5000), t.src, t.dst, t.gw);
            env(fset(t.gw, asfAllowTrustLineLocking));
            env.close();

            if (t.hasTrustline)
            {
                env.trust(usd(100'000), t.src, t.dst);
            }
            else
            {
                env.trust(usd(100'000), t.src);
            }
            env.close();

            env(pay(t.gw, t.src, usd(10'000)));
            if (t.hasTrustline)
                env(pay(t.gw, t.dst, usd(10'000)));
            env.close();

            // src can create paychan
            auto const seq1 = env.seq(t.src);
            auto const delta = usd(1'000);
            auto const pk = t.src.pk();
            auto const settleDelay = 100s;
            env(paychan::create(t.src, t.dst, delta, settleDelay, pk));
            env.close();

            // dst can claim paychan
            auto const preSrc = env.balance(t.src, usd);
            auto const preDst = env.balance(t.dst, usd);

            auto const chan = paychan::channel(t.src, t.dst, seq1);
            auto const sig = paychan::signClaimAuth(pk, t.src.sk(), chan, delta);
            env(paychan::claim(t.dst, chan, delta, delta, Slice(sig), pk));
            env.close();

            BEAST_EXPECT(env.balance(t.src, usd) == preSrc);
            BEAST_EXPECT(env.balance(t.dst, usd) == preDst + delta);
        }
    }

    void
    testIOUGateway(FeatureBitset features)
    {
        testcase("IOU Gateway");
        using namespace test::jtx;
        using namespace std::literals;

        // issuer is source
        {
            auto const gw = Account{"gateway"};
            auto const alice = Account{"alice"};
            Env env{*this, features};
            auto const usd = gw["USD"];
            env.fund(XRP(5000), alice, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(usd(100'000), alice);
            env.close();

            env(pay(gw, alice, usd(10'000)));
            env.close();

            // issuer cannot create paychan
            auto const pk = gw.pk();
            auto const settleDelay = 100s;
            env(paychan::create(gw, alice, usd(1'000), settleDelay, pk), Ter(tecNO_PERMISSION));
            env.close();
        }

        struct TestAccountData
        {
            Account src;
            Account dst;
            bool hasTrustline;
        };

        std::array<TestAccountData, 4> const gwDstTests = {{
            // src > dst && src > issuer && dst has trustline
            {.src = Account("alice2"), .dst = Account{"gw0"}, .hasTrustline = true},
            // src < dst && src < issuer && dst has trustline
            {.src = Account("carol0"), .dst = Account{"gw1"}, .hasTrustline = true},
            // dst > src && dst > issuer && dst has trustline
            {.src = Account("dan1"), .dst = Account{"gw0"}, .hasTrustline = true},
            // dst < src && dst < issuer && dst has trustline
            {.src = Account("bob0"), .dst = Account{"gw1"}, .hasTrustline = true},
        }};

        // issuer is destination
        for (auto const& t : gwDstTests)
        {
            Env env{*this, features};
            auto const usd = t.dst["USD"];
            env.fund(XRP(5000), t.dst, t.src);
            env(fset(t.dst, asfAllowTrustLineLocking));
            env.close();

            env.trust(usd(100'000), t.src);
            env.close();

            env(pay(t.dst, t.src, usd(10'000)));
            env.close();

            // issuer can receive paychan
            auto const seq1 = env.seq(t.src);
            auto const preSrc = env.balance(t.src, usd);
            auto const pk = t.src.pk();
            auto const settleDelay = 100s;
            env(paychan::create(t.src, t.dst, usd(1'000), settleDelay, pk));
            env.close();

            // issuer can claim paychan, no dest trustline
            auto const chan = paychan::channel(t.src, t.dst, seq1);
            auto const sig = paychan::signClaimAuth(pk, t.src.sk(), chan, usd(1'000));
            env(paychan::claim(t.dst, chan, usd(1'000), usd(1'000), Slice(sig), pk));
            env.close();
            auto const preAmount = 10'000;
            BEAST_EXPECT(preSrc == usd(preAmount));
            auto const postAmount = 9000;
            BEAST_EXPECT(env.balance(t.src, usd) == usd(postAmount));
            BEAST_EXPECT(env.balance(t.dst, usd) == usd(0));
        }
    }

    void
    testIOULockedRate(FeatureBitset features)
    {
        testcase("IOU Locked Rate");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const usd = gw["USD"];

        // test locked rate
        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env(rate(gw, 1.25));
            env.close();
            env.trust(usd(100'000), alice);
            env.trust(usd(100'000), bob);
            env.close();
            env(pay(gw, alice, usd(10'000)));
            env(pay(gw, bob, usd(10'000)));
            env.close();

            // alice can create paychan w/ xfer rate
            auto const preAlice = env.balance(alice, usd);
            auto const seq1 = env.seq(alice);
            auto const delta = usd(125);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, delta, settleDelay, pk));
            env.close();
            auto const transferRate = paychan::rate(env, alice, bob, seq1);
            BEAST_EXPECT(transferRate.value == std::uint32_t(1'000'000'000 * 1.25));

            // bob can claim paychan
            auto const chan = paychan::channel(alice, bob, seq1);
            auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, delta);
            env(paychan::claim(bob, chan, delta, delta, Slice(sig), pk));
            env.close();

            BEAST_EXPECT(env.balance(alice, usd) == preAlice - delta);
            BEAST_EXPECT(env.balance(bob, usd) == usd(10'100));
        }
        // test rate change - higher
        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env(rate(gw, 1.25));
            env.close();
            env.trust(usd(100'000), alice);
            env.trust(usd(100'000), bob);
            env.close();
            env(pay(gw, alice, usd(10'000)));
            env(pay(gw, bob, usd(10'000)));
            env.close();

            // alice can create paychan w/ xfer rate
            auto const preAlice = env.balance(alice, usd);
            auto const seq1 = env.seq(alice);
            auto const delta = usd(125);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, delta, settleDelay, pk));
            env.close();
            auto transferRate = paychan::rate(env, alice, bob, seq1);
            BEAST_EXPECT(transferRate.value == std::uint32_t(1'000'000'000 * 1.25));

            // issuer changes rate higher
            env(rate(gw, 1.26));
            env.close();

            // bob can claim paychan - rate unchanged
            auto const chan = paychan::channel(alice, bob, seq1);
            auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, delta);
            env(paychan::claim(bob, chan, delta, delta, Slice(sig), pk));
            env.close();

            BEAST_EXPECT(env.balance(alice, usd) == preAlice - delta);
            BEAST_EXPECT(env.balance(bob, usd) == usd(10'100));
        }
        // test rate change - lower
        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env(rate(gw, 1.25));
            env.close();
            env.trust(usd(100'000), alice);
            env.trust(usd(100'000), bob);
            env.close();
            env(pay(gw, alice, usd(10'000)));
            env(pay(gw, bob, usd(10'000)));
            env.close();

            // alice can create paychan w/ xfer rate
            auto const preAlice = env.balance(alice, usd);
            auto const seq1 = env.seq(alice);
            auto const delta = usd(125);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, delta, settleDelay, pk));
            env.close();
            auto transferRate = paychan::rate(env, alice, bob, seq1);
            BEAST_EXPECT(transferRate.value == std::uint32_t(1'000'000'000 * 1.25));

            // issuer changes rate lower
            env(rate(gw, 1.00));
            env.close();

            // bob can claim paychan - rate changed
            auto const chan = paychan::channel(alice, bob, seq1);
            auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, delta);
            env(paychan::claim(bob, chan, delta, delta, Slice(sig), pk));
            env.close();

            BEAST_EXPECT(env.balance(alice, usd) == preAlice - delta);
            BEAST_EXPECT(env.balance(bob, usd) == usd(10125));
        }

        // test claim/close doesnt charge rate
        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env(rate(gw, 1.25));
            env.close();
            env.trust(usd(100'000), alice);
            env.trust(usd(100'000), bob);
            env.close();
            env(pay(gw, alice, usd(10'000)));
            env(pay(gw, bob, usd(10'000)));
            env.close();

            // alice can create paychan w/ xfer rate
            auto const preAlice = env.balance(alice, usd);
            auto const seq1 = env.seq(alice);
            auto const delta = usd(125);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, delta, settleDelay, pk));
            env.close();
            auto transferRate = paychan::rate(env, alice, bob, seq1);
            BEAST_EXPECT(transferRate.value == std::uint32_t(1'000'000'000 * 1.25));

            // issuer changes rate lower
            env(rate(gw, 1.00));
            env.close();

            // alice can close paychan - rate is not charged
            auto const chan = paychan::channel(alice, bob, seq1);
            env(paychan::claim(bob, chan), Txflags(tfClose));
            env.close();

            BEAST_EXPECT(env.balance(alice, usd) == preAlice);
            BEAST_EXPECT(env.balance(bob, usd) == usd(10000));
        }
    }

    void
    testIOULimitAmount(FeatureBitset features)
    {
        testcase("IOU Limit");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const usd = gw["USD"];

        // test LimitAmount
        {
            Env env{*this, features};
            env.fund(XRP(1'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(usd(10'000), alice, bob);
            env.close();
            env(pay(gw, alice, usd(1'000)));
            env(pay(gw, bob, usd(1'000)));
            env.close();

            // alice can create paychan
            auto seq1 = env.seq(alice);
            auto const delta = usd(125);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, delta, settleDelay, pk));
            env.close();

            // bob can claim
            auto const preBobLimit = env.limit(bob, usd);
            auto const chan = paychan::channel(alice, bob, seq1);
            auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, delta);
            env(paychan::claim(bob, chan, delta, delta, Slice(sig), pk));
            env.close();
            auto const postBobLimit = env.limit(bob, usd);
            // bobs limit is NOT changed
            BEAST_EXPECT(postBobLimit == preBobLimit);
        }
    }

    void
    testIOURequireAuth(FeatureBitset features)
    {
        testcase("IOU Require Auth");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const usd = gw["USD"];

        auto const aliceUSD = alice["USD"];
        auto const bobUSD = bob["USD"];

        Env env{*this, features};
        env.fund(XRP(1'000), alice, bob, gw);
        env(fset(gw, asfAllowTrustLineLocking));
        env(fset(gw, asfRequireAuth));
        env.close();
        env(trust(gw, aliceUSD(10'000)), Txflags(tfSetfAuth));
        env(trust(alice, usd(10'000)));
        env(trust(bob, usd(10'000)));
        env.close();
        env(pay(gw, alice, usd(1'000)));
        env.close();

        // alice cannot create paychan - fails without auth
        auto seq1 = env.seq(alice);
        auto const delta = usd(125);
        auto const pk = alice.pk();
        auto const settleDelay = 100s;
        env(paychan::create(alice, bob, delta, settleDelay, pk), Ter(tecNO_AUTH));
        env.close();

        // set auth on bob
        env(trust(gw, bobUSD(10'000)), Txflags(tfSetfAuth));
        env(trust(bob, usd(10'000)));
        env.close();
        env(pay(gw, bob, usd(1'000)));
        env.close();

        // alice can create paychan - bob has auth
        seq1 = env.seq(alice);
        env(paychan::create(alice, bob, delta, settleDelay, pk));
        env.close();

        // bob can claim
        auto const chan = paychan::channel(alice, bob, seq1);
        auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, delta);
        env(paychan::claim(bob, chan, delta, delta, Slice(sig), pk));
        env.close();
    }

    void
    testIOUFreeze(FeatureBitset features)
    {
        testcase("IOU Freeze");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const usd = gw["USD"];

        // test Global Freeze
        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(usd(100'000), alice);
            env.trust(usd(100'000), bob);
            env.close();
            env(pay(gw, alice, usd(10'000)));
            env(pay(gw, bob, usd(10'000)));
            env.close();
            env(fset(gw, asfGlobalFreeze));
            env.close();

            // setup transaction
            auto seq1 = env.seq(alice);
            auto const delta = usd(125);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;

            // create paychan fails - frozen trustline
            env(paychan::create(alice, bob, delta, settleDelay, pk), Ter(tecFROZEN));
            env.close();

            // clear global freeze
            env(fclear(gw, asfGlobalFreeze));
            env.close();

            // create paychan success
            seq1 = env.seq(alice);
            env(paychan::create(alice, bob, delta, settleDelay, pk));
            env.close();

            // set global freeze
            env(fset(gw, asfGlobalFreeze));
            env.close();

            // bob claim paychan success regardless of frozen assets
            auto const chan = paychan::channel(alice, bob, seq1);
            auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, delta);
            env(paychan::claim(bob, chan, delta, delta, Slice(sig), pk));
            env.close();

            // clear global freeze
            env(fclear(gw, asfGlobalFreeze));
            env.close();

            // create paychan success
            seq1 = env.seq(alice);
            env(paychan::create(alice, bob, delta, settleDelay, pk));
            env.close();

            // set global freeze
            env(fset(gw, asfGlobalFreeze));
            env.close();

            // alice close paychan success regardless of frozen assets
            auto const chan2 = paychan::channel(alice, bob, seq1);
            env(paychan::claim(alice, chan2, delta, delta), Txflags(tfClose));
            env.close();
        }

        // test Individual Freeze
        {
            // Env Setup
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env(trust(alice, usd(100'000)));
            env(trust(bob, usd(100'000)));
            env.close();
            env(pay(gw, alice, usd(10'000)));
            env(pay(gw, bob, usd(10'000)));
            env.close();

            // set freeze on alice trustline
            env(trust(gw, usd(10'000), alice, tfSetFreeze));
            env.close();

            // setup transaction
            auto seq1 = env.seq(alice);
            auto const delta = usd(125);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;

            // create paychan fails - frozen trustline
            env(paychan::create(alice, bob, delta, settleDelay, pk), Ter(tecFROZEN));
            env.close();

            // clear freeze on alice trustline
            env(trust(gw, usd(10'000), alice, tfClearFreeze));
            env.close();

            // create paychan success
            seq1 = env.seq(alice);
            env(paychan::create(alice, bob, delta, settleDelay, pk));
            env.close();

            // set freeze on bob trustline
            env(trust(gw, usd(10'000), bob, tfSetFreeze));
            env.close();

            // bob claim paychan success regardless of frozen assets
            auto const chan = paychan::channel(alice, bob, seq1);
            auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, delta);
            env(paychan::claim(bob, chan, delta, delta, Slice(sig), pk));
            env.close();

            // reset freeze on bob and alice trustline
            env(trust(gw, usd(10'000), alice, tfClearFreeze));
            env(trust(gw, usd(10'000), bob, tfClearFreeze));
            env.close();

            // create paychan success
            seq1 = env.seq(alice);
            env(paychan::create(alice, bob, delta, settleDelay, pk));
            env.close();

            // set freeze on bob trustline
            env(trust(gw, usd(10'000), bob, tfSetFreeze));
            env.close();

            // alice close paychan success regardless of frozen assets
            auto const chan2 = paychan::channel(alice, bob, seq1);
            env(paychan::claim(alice, chan2, delta, delta), Txflags(tfClose));
            env.close();
        }

        // test Deep Freeze
        {
            // Env Setup
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env(trust(alice, usd(100'000)));
            env(trust(bob, usd(100'000)));
            env.close();
            env(pay(gw, alice, usd(10'000)));
            env(pay(gw, bob, usd(10'000)));
            env.close();

            // set freeze on alice trustline
            env(trust(gw, usd(10'000), alice, tfSetFreeze | tfSetDeepFreeze));
            env.close();

            // setup transaction
            auto seq1 = env.seq(alice);
            auto const delta = usd(125);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;

            // create paychan fails - frozen trustline
            env(paychan::create(alice, bob, delta, settleDelay, pk), Ter(tecFROZEN));
            env.close();

            // clear freeze on alice trustline
            env(trust(gw, usd(10'000), alice, tfClearFreeze | tfClearDeepFreeze));
            env.close();

            // create paychan success
            seq1 = env.seq(alice);
            env(paychan::create(alice, bob, delta, settleDelay, pk));
            env.close();

            // set freeze on bob trustline
            env(trust(gw, usd(10'000), bob, tfSetFreeze | tfSetDeepFreeze));
            env.close();

            // bob claim paychan fails because of deep frozen assets
            auto const chan = paychan::channel(alice, bob, seq1);
            auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, delta);
            env(paychan::claim(bob, chan, delta, delta, Slice(sig), pk), Ter(tecFROZEN));
            env.close();

            // reset freeze on alice and bob trustline
            env(trust(gw, usd(10'000), alice, tfClearFreeze | tfClearDeepFreeze));
            env(trust(gw, usd(10'000), bob, tfClearFreeze | tfClearDeepFreeze));
            env.close();

            // create paychan success
            seq1 = env.seq(alice);
            env(paychan::create(alice, bob, delta, settleDelay, pk));
            env.close();

            // set freeze on bob trustline
            env(trust(gw, usd(10'000), bob, tfSetFreeze | tfSetDeepFreeze));
            env.close();

            // bob close paychan success regardless of deep frozen assets
            auto const chan2 = paychan::channel(alice, bob, seq1);
            env(paychan::claim(bob, chan2), Txflags(tfClose));
            env.close();
        }
    }

    void
    testIOUInsf(FeatureBitset features)
    {
        testcase("IOU Insufficient Funds");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const usd = gw["USD"];
        {
            // test tecPATH_PARTIAL
            // ie. has 10'000, paychan 1'000 then try to pay 10'000
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(usd(100'000), alice);
            env.trust(usd(100'000), bob);
            env.close();
            env(pay(gw, alice, usd(10'000)));
            env(pay(gw, bob, usd(10'000)));
            env.close();

            // create paychan success
            auto const delta = usd(1'000);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, delta, settleDelay, pk));
            env.close();
            env(pay(alice, gw, usd(10'000)), Ter(tecPATH_PARTIAL));
        }
        {
            // test tecINSUFFICIENT_FUNDS
            // ie. has 10'000 paychan 1'000 then try to paychan 10'000
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(usd(100'000), alice);
            env.trust(usd(100'000), bob);
            env.close();
            env(pay(gw, alice, usd(10'000)));
            env(pay(gw, bob, usd(10'000)));
            env.close();

            auto const delta = usd(1'000);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, delta, settleDelay, pk));
            env.close();

            env(paychan::create(alice, bob, usd(10'000), settleDelay, pk),
                Ter(tecINSUFFICIENT_FUNDS));
            env.close();
        }
    }

    void
    testIOUPrecisionLoss(FeatureBitset features)
    {
        testcase("IOU Precision Loss");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const usd = gw["USD"];

        // test min create precision loss
        {
            Env env(*this, features);
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(usd(100000000000000000), alice);
            env.trust(usd(100000000000000000), bob);
            env.close();
            env(pay(gw, alice, usd(10000000000000000)));
            env(pay(gw, bob, usd(1)));
            env.close();

            // With a larger mantissa the amounts remain addable
            bool const largeMantissa =
                features[featureSingleAssetVault] || features[featureLendingProtocol];

            // alice cannot create paychan for 1/10 iou - precision loss
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, usd(1), settleDelay, pk),
                Ter(largeMantissa ? (TER)tesSUCCESS : (TER)tecPRECISION_LOSS));
            env.close();

            auto const seq1 = env.seq(alice);
            // alice can create paychan for 1'000 iou
            env(paychan::create(alice, bob, usd(1'000), settleDelay, pk));
            env.close();

            // bob claim paychan success
            auto const chan = paychan::channel(alice, bob, seq1);
            auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, usd(1'000));
            env(paychan::claim(bob, chan, usd(1'000), usd(1'000), Slice(sig), pk));
            env.close();
        }
    }

    void
    testMPTEnablement(FeatureBitset features)
    {
        testcase("MPT Enablement");

        using namespace jtx;
        using namespace std::chrono;

        for (bool const withTokenPaychan : {false, true})
        {
            auto const amend = withTokenPaychan ? features : features - featureTokenPaychan;
            Env env{*this, amend};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            env.fund(XRP(5000), bob);

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env.close();

            auto const openResult = withTokenPaychan ? Ter(tesSUCCESS) : Ter(temBAD_AMOUNT);
            auto const closeResult = withTokenPaychan ? Ter(tesSUCCESS) : Ter(tecNO_TARGET);

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, mpt(1'000), settleDelay, pk), openResult);
            env.close();
            env(paychan::fund(alice, chan, mpt(1'000)), openResult);
            env.close();
            env(paychan::claim(bob, chan), Txflags(tfClose), closeResult);
            env.close();
        }
    }

    void
    testMPTCreatePreflight(FeatureBitset features)
    {
        testcase("MPT Create Preflight");
        using namespace test::jtx;
        using namespace std::literals;

        for (bool const withMPT : {true, false})
        {
            auto const amend = withMPT ? features : features - featureMPTokensV1;
            Env env{*this, amend};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            env.fund(XRP(1'000), alice, bob, gw);

            json::Value jv = paychan::create(alice, bob, XRP(1), 100s, alice.pk());
            jv.removeMember(jss::Amount);
            jv[jss::Amount][jss::mpt_issuance_id] =
                "00000004A407AF5856CCF3C42619DAA925813FC955C72983";
            jv[jss::Amount][jss::value] = "-1";

            auto const result = withMPT ? Ter(temBAD_AMOUNT) : Ter(temDISABLED);
            env(jv, result);
            env.close();
        }

        // temBAD_AMOUNT: amount < 0
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env(pay(gw, bob, mpt(10'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, mpt(-1), settleDelay, pk), Ter(temBAD_AMOUNT));
            env.close();
        }
    }

    void
    testMPTCreatePreclaim(FeatureBitset features)
    {
        testcase("MPT Create Preclaim");
        using namespace test::jtx;
        using namespace std::literals;

        // tecNO_PERMISSION: issuer is the same as the account
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env.close();

            auto const pk = gw.pk();
            auto const settleDelay = 100s;
            env(paychan::create(gw, alice, mpt(1), settleDelay, pk), Ter(tecNO_PERMISSION));
            env.close();
        }

        // tecOBJECT_NOT_FOUND: mpt does not exist
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            env.fund(XRP(10'000), alice, bob, gw);
            env.close();

            auto const mpt = xrpl::test::jtx::MPT(alice.name(), makeMptID(env.seq(alice), alice));
            json::Value jv = paychan::create(alice, bob, mpt(2), 100s, alice.pk());
            jv[jss::Amount][jss::mpt_issuance_id] =
                "00000004A407AF5856CCF3C42619DAA925813FC955C72983";
            env(jv, Ter(tecOBJECT_NOT_FOUND));
            env.close();
        }

        // tecNO_PERMISSION: tfMPTCanEscrow is not enabled
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env(pay(gw, bob, mpt(10'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, mpt(3), settleDelay, pk), Ter(tecNO_PERMISSION));
            env.close();
        }

        // tecOBJECT_NOT_FOUND: account does not have the mpt
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            auto const mpt = mptGw["MPT"];

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, mpt(4), settleDelay, pk), Ter(tecOBJECT_NOT_FOUND));
            env.close();
        }

        // tecNO_AUTH: requireAuth set: account not authorized
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTRequireAuth});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = gw, .holder = alice});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env.close();

            // unauthorize account
            mptGw.authorize({.account = gw, .holder = alice, .flags = tfMPTUnauthorize});

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, mpt(5), settleDelay, pk), Ter(tecNO_AUTH));
            env.close();
        }

        // tecNO_AUTH: requireAuth set: dest not authorized
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTRequireAuth});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = gw, .holder = alice});
            mptGw.authorize({.account = bob});
            mptGw.authorize({.account = gw, .holder = bob});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env(pay(gw, bob, mpt(10'000)));
            env.close();

            // unauthorize dest
            mptGw.authorize({.account = gw, .holder = bob, .flags = tfMPTUnauthorize});

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, mpt(6), settleDelay, pk), Ter(tecNO_AUTH));
            env.close();
        }

        // tecLOCKED: issuer has locked the account
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTCanLock});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env(pay(gw, bob, mpt(10'000)));
            env.close();

            // lock account
            mptGw.set({.account = gw, .holder = alice, .flags = tfMPTLock});

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, mpt(7), settleDelay, pk), Ter(tecLOCKED));
            env.close();
        }

        // tecLOCKED: issuer has locked the dest
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTCanLock});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env(pay(gw, bob, mpt(10'000)));
            env.close();

            // lock dest
            mptGw.set({.account = gw, .holder = bob, .flags = tfMPTLock});

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, mpt(8), settleDelay, pk), Ter(tecLOCKED));
            env.close();
        }

        // tecNO_AUTH: mpt cannot be transferred
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env(pay(gw, bob, mpt(10'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, mpt(9), settleDelay, pk), Ter(tecNO_AUTH));
            env.close();
        }

        // tecINSUFFICIENT_FUNDS: spendable amount is zero
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, bob, mpt(10)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, mpt(11), settleDelay, pk), Ter(tecINSUFFICIENT_FUNDS));
            env.close();
        }

        // tecINSUFFICIENT_FUNDS: spendable amount is less than the amount
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10)));
            env(pay(gw, bob, mpt(10)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, mpt(11), settleDelay, pk), Ter(tecINSUFFICIENT_FUNDS));
            env.close();
        }
    }

    void
    testMPTClaimPreclaim(FeatureBitset features)
    {
        testcase("MPT Claim Preclaim");
        using namespace test::jtx;
        using namespace std::literals;

        // tecNO_AUTH: requireAuth set: dest not authorized
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTRequireAuth});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = gw, .holder = alice});
            mptGw.authorize({.account = bob});
            mptGw.authorize({.account = gw, .holder = bob});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env(pay(gw, bob, mpt(10'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, mpt(10), settleDelay, pk), Ter(tesSUCCESS));
            env.close();

            // unauthorize dest
            mptGw.authorize({.account = gw, .holder = bob, .flags = tfMPTUnauthorize});

            auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, mpt(10));
            env(paychan::claim(bob, chan, mpt(10), mpt(10), Slice(sig), pk), Ter(tecNO_AUTH));
            env.close();
        }

        // tecLOCKED: issuer has locked the dest
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTCanLock});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env(pay(gw, bob, mpt(10'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, mpt(8), settleDelay, pk), Ter(tesSUCCESS));
            env.close();

            // lock dest
            mptGw.set({.account = gw, .holder = bob, .flags = tfMPTLock});

            auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, mpt(8));
            env(paychan::claim(bob, chan, mpt(8), mpt(8), Slice(sig), pk), Ter(tecLOCKED));
            env.close();
        }
    }

    void
    testMPTClaimDoApply(FeatureBitset features)
    {
        testcase("MPT Claim Do Apply");
        using namespace test::jtx;
        using namespace std::literals;

        // tecINSUFFICIENT_RESERVE: insufficient reserve to create MPT
        {
            Env env{*this, features};
            auto const acctReserve = env.current()->fees().accountReserve(0, 1);
            auto const incReserve = env.current()->fees().increment;

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            env.fund(acctReserve + (incReserve - 1), bob);
            env.close();

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, mpt(10), settleDelay, pk), Ter(tesSUCCESS));
            env.close();

            auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, mpt(10));
            env(paychan::claim(bob, chan, mpt(10), mpt(10), Slice(sig), pk),
                Ter(tecINSUFFICIENT_RESERVE));
            env.close();
        }

        // tesSUCCESS: bob submits; claim MPT created
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            env.fund(XRP(10'000), bob);
            env.close();

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, mpt(10), settleDelay, pk), Ter(tesSUCCESS));
            env.close();

            auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, mpt(10));
            env(paychan::claim(bob, chan, mpt(10), mpt(10), Slice(sig), pk), Ter(tesSUCCESS));
            env.close();
        }

        // tecNO_PERMISSION: alice submits; claim MPT not created
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            env.fund(XRP(10'000), bob);
            env.close();

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, mpt(10), settleDelay, pk), Ter(tesSUCCESS));
            env.close();

            env(paychan::claim(alice, chan, mpt(10), mpt(10)), Ter(tecNO_PERMISSION));
            env.close();
        }
    }

    void
    testMPTBalances(FeatureBitset features)
    {
        testcase("MPT Balances");

        using namespace jtx;
        using namespace std::chrono;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account("gw");
        env.fund(XRP(5000), bob);

        MPTTester mptGw(env, gw, {.holders = {alice, carol}});
        mptGw.create(
            {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow | tfMPTCanTransfer});
        mptGw.authorize({.account = alice});
        mptGw.authorize({.account = carol});
        auto const mpt = mptGw["MPT"];
        env(pay(gw, alice, mpt(10'000)));
        env(pay(gw, carol, mpt(10'000)));
        env.close();

        auto outstandingMPT = env.balance(gw, mpt);

        // Create & Claim (Dest) PayChan
        auto const chan = paychan::channel(alice, bob, env.seq(alice));
        {
            auto const preAliceMPT = env.balance(alice, mpt);
            auto const preBobMPT = env.balance(bob, mpt);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, mpt(1'000), settleDelay, pk), Ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, mpt) == preAliceMPT - mpt(1'000));
            BEAST_EXPECT(mptEscrowed(env, alice, mpt) == 1'000);
            BEAST_EXPECT(env.balance(bob, mpt) == preBobMPT);
            BEAST_EXPECT(mptEscrowed(env, bob, mpt) == 0);
            BEAST_EXPECT(env.balance(gw, mpt) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, mpt) == 1'000);
        }
        {
            auto const preAliceMPT = env.balance(alice, mpt);
            auto const preBobMPT = env.balance(bob, mpt);
            auto const pk = alice.pk();
            auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, mpt(1'000));
            env(paychan::claim(bob, chan, mpt(1'000), mpt(1'000), Slice(sig), pk), Ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, mpt) == preAliceMPT);
            BEAST_EXPECT(mptEscrowed(env, alice, mpt) == 0);
            BEAST_EXPECT(env.balance(bob, mpt) == preBobMPT + mpt(1'000));
            BEAST_EXPECT(mptEscrowed(env, bob, mpt) == 0);
            BEAST_EXPECT(env.balance(gw, mpt) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, mpt) == 0);
        }

        // Create & Claim (Account) PayChan
        auto const chan2 = paychan::channel(alice, bob, env.seq(alice));
        {
            auto const preAliceMPT = env.balance(alice, mpt);
            auto const preBobMPT = env.balance(bob, mpt);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, mpt(1'000), settleDelay, pk), Ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, mpt) == preAliceMPT - mpt(1'000));
            BEAST_EXPECT(mptEscrowed(env, alice, mpt) == 1'000);
            BEAST_EXPECT(env.balance(bob, mpt) == preBobMPT);
            BEAST_EXPECT(mptEscrowed(env, bob, mpt) == 0);
            BEAST_EXPECT(env.balance(gw, mpt) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, mpt) == 1'000);
        }
        {
            auto const preAliceMPT = env.balance(alice, mpt);
            auto const preBobMPT = env.balance(bob, mpt);
            env(paychan::claim(alice, chan2, mpt(1'000), mpt(1'000)),
                Txflags(tfClose),
                Ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, mpt) == preAliceMPT);
            BEAST_EXPECT(mptEscrowed(env, alice, mpt) == 0);
            BEAST_EXPECT(env.balance(bob, mpt) == preBobMPT + mpt(1'000));
            BEAST_EXPECT(mptEscrowed(env, bob, mpt) == 0);
            BEAST_EXPECT(env.balance(gw, mpt) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, mpt) == 0);
        }

        // Multiple PayChans
        {
            auto const preAliceMPT = env.balance(alice, mpt);
            auto const preBobMPT = env.balance(bob, mpt);
            auto const preCarolMPT = env.balance(carol, mpt);
            auto const pk = alice.pk();
            auto const pk2 = carol.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, mpt(1'000), settleDelay, pk), Ter(tesSUCCESS));
            env.close();

            env(paychan::create(carol, bob, mpt(1'000), settleDelay, pk2), Ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, mpt) == preAliceMPT - mpt(1'000));
            BEAST_EXPECT(mptEscrowed(env, alice, mpt) == 1'000);
            BEAST_EXPECT(env.balance(bob, mpt) == preBobMPT);
            BEAST_EXPECT(mptEscrowed(env, bob, mpt) == 0);
            BEAST_EXPECT(env.balance(carol, mpt) == preCarolMPT - mpt(1'000));
            BEAST_EXPECT(mptEscrowed(env, carol, mpt) == 1'000);
            BEAST_EXPECT(env.balance(gw, mpt) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, mpt) == 2'000);
        }

        // Max MPT Amount Issued (PayChan 1 MPT)
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(kMaxMpTokenAmount)));
            env.close();

            auto const preAliceMPT = env.balance(alice, mpt);
            auto const preBobMPT = env.balance(bob, mpt);
            auto const outstandingMPT = env.balance(gw, mpt);

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, mpt(1), settleDelay, pk));
            env.close();

            BEAST_EXPECT(env.balance(alice, mpt) == preAliceMPT - mpt(1));
            BEAST_EXPECT(mptEscrowed(env, alice, mpt) == 1);
            BEAST_EXPECT(env.balance(bob, mpt) == preBobMPT);
            BEAST_EXPECT(mptEscrowed(env, bob, mpt) == 0);
            BEAST_EXPECT(env.balance(gw, mpt) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, mpt) == 1);

            auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, mpt(1));
            env(paychan::claim(bob, chan, mpt(1), mpt(1), Slice(sig), pk), Ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, mpt) == preAliceMPT - mpt(1));
            BEAST_EXPECT(mptEscrowed(env, alice, mpt) == 0);
            BEAST_EXPECT(
                !env.le(keylet::mptoken(mpt.mpt(), alice))->isFieldPresent(sfLockedAmount));
            BEAST_EXPECT(env.balance(bob, mpt) == preBobMPT + mpt(1));
            BEAST_EXPECT(mptEscrowed(env, bob, mpt) == 0);
            BEAST_EXPECT(env.balance(gw, mpt) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, mpt) == 0);
            BEAST_EXPECT(
                !env.le(keylet::mptokenIssuance(mpt.mpt()))->isFieldPresent(sfLockedAmount));
        }

        // Max MPT Amount Issued (PayChan Max MPT)
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(kMaxMpTokenAmount)));
            env.close();

            auto const preAliceMPT = env.balance(alice, mpt);
            auto const preBobMPT = env.balance(bob, mpt);
            auto const outstandingMPT = env.balance(gw, mpt);

            // PayChan Max MPT - 10
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan1 = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, mpt(kMaxMpTokenAmount - 10), settleDelay, pk));
            env.close();

            // PayChan 10 MPT
            auto const chan2 = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, mpt(10), settleDelay, pk));
            env.close();

            BEAST_EXPECT(env.balance(alice, mpt) == preAliceMPT - mpt(kMaxMpTokenAmount));
            BEAST_EXPECT(mptEscrowed(env, alice, mpt) == kMaxMpTokenAmount);
            BEAST_EXPECT(env.balance(bob, mpt) == preBobMPT);
            BEAST_EXPECT(mptEscrowed(env, bob, mpt) == 0);
            BEAST_EXPECT(env.balance(gw, mpt) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, mpt) == kMaxMpTokenAmount);

            auto const sig1 =
                paychan::signClaimAuth(pk, alice.sk(), chan1, mpt(kMaxMpTokenAmount - 10));
            env(paychan::claim(
                    bob,
                    chan1,
                    mpt(kMaxMpTokenAmount - 10),
                    mpt(kMaxMpTokenAmount - 10),
                    Slice(sig1),
                    pk),
                Ter(tesSUCCESS));
            env.close();

            auto const sig2 = paychan::signClaimAuth(pk, alice.sk(), chan2, mpt(10));
            env(paychan::claim(bob, chan2, mpt(10), mpt(10), Slice(sig2), pk), Ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, mpt) == preAliceMPT - mpt(kMaxMpTokenAmount));
            BEAST_EXPECT(mptEscrowed(env, alice, mpt) == 0);
            BEAST_EXPECT(env.balance(bob, mpt) == preBobMPT + mpt(kMaxMpTokenAmount));
            BEAST_EXPECT(mptEscrowed(env, bob, mpt) == 0);
            BEAST_EXPECT(env.balance(gw, mpt) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, mpt) == 0);
        }
    }

    void
    testMPTMetaAndOwnership(FeatureBitset features)
    {
        using namespace jtx;
        using namespace std::chrono;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        {
            testcase("MPT Metadata to other");

            Env env{*this, features};
            MPTTester mptGw(env, gw, {.holders = {alice, bob, carol}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            mptGw.authorize({.account = carol});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env(pay(gw, bob, mpt(10'000)));
            env(pay(gw, carol, mpt(10'000)));
            env.close();
            auto const aseq = env.seq(alice);
            auto const bseq = env.seq(bob);

            auto const pk = alice.pk();
            auto const pk2 = bob.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, mpt(1'000), settleDelay, pk));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] == static_cast<std::uint8_t>(tesSUCCESS));
            env.close();
            env(paychan::create(bob, carol, mpt(1'000), settleDelay, pk2));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] == static_cast<std::uint8_t>(tesSUCCESS));
            env.close();

            auto const ab = env.le(keylet::payChannel(alice.id(), bob.id(), aseq));
            BEAST_EXPECT(ab);

            auto const bc = env.le(keylet::payChannel(bob.id(), carol.id(), bseq));
            BEAST_EXPECT(bc);

            {
                xrpl::Dir const aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 2);
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(aod.begin(), aod.end(), ab) != aod.end());

                xrpl::Dir const bod(*env.current(), keylet::ownerDir(bob.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 3);
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(bod.begin(), bod.end(), ab) != bod.end());
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(bod.begin(), bod.end(), bc) != bod.end());

                xrpl::Dir const cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 2);
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(cod.begin(), cod.end(), bc) != cod.end());
            }

            auto const chanAb = paychan::channel(alice, bob, aseq);
            env(paychan::claim(alice, chanAb, mpt(1'000), mpt(1'000)), Txflags(tfClose));
            {
                BEAST_EXPECT(!env.le(keylet::payChannel(alice.id(), bob.id(), aseq)));
                BEAST_EXPECT(env.le(keylet::payChannel(bob.id(), carol.id(), bseq)));

                xrpl::Dir const aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 1);
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(aod.begin(), aod.end(), ab) == aod.end());

                xrpl::Dir const bod(*env.current(), keylet::ownerDir(bob.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 2);
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(bod.begin(), bod.end(), ab) == bod.end());
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(bod.begin(), bod.end(), bc) != bod.end());

                xrpl::Dir const cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 2);
            }

            env.close();
            auto const chanBc = paychan::channel(bob, carol, bseq);
            env(paychan::claim(bob, chanBc, mpt(1'000), mpt(1'000)), Txflags(tfClose));
            {
                BEAST_EXPECT(!env.le(keylet::payChannel(alice.id(), bob.id(), aseq)));
                BEAST_EXPECT(!env.le(keylet::payChannel(bob.id(), carol.id(), bseq)));

                xrpl::Dir const aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 1);
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(aod.begin(), aod.end(), ab) == aod.end());

                xrpl::Dir const bod(*env.current(), keylet::ownerDir(bob.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 1);
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(bod.begin(), bod.end(), ab) == bod.end());
                // NOLINTNEXTLINE(modernize-use-ranges)
                BEAST_EXPECT(std::find(bod.begin(), bod.end(), bc) == bod.end());

                xrpl::Dir const cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 1);
            }
        }
    }

    void
    testMPTGateway(FeatureBitset features)
    {
        testcase("MPT Gateway Balances");
        using namespace test::jtx;
        using namespace std::literals;

        // issuer is source
        {
            auto const gw = Account{"gateway"};
            auto const alice = Account{"alice"};
            Env env{*this, features};

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env.close();

            // issuer cannot create paychan
            auto const pk = gw.pk();
            auto const settleDelay = 100s;
            env(paychan::create(gw, alice, mpt(1'000), settleDelay, pk), Ter(tecNO_PERMISSION));
            env.close();
        }

        // issuer is dest; alice w/ authorization
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env.close();

            // issuer can be destination
            auto const preAliceMPT = env.balance(alice, mpt);
            auto const preOutstanding = env.balance(gw, mpt);
            auto const preEscrowed = issuerMPTEscrowed(env, mpt);
            BEAST_EXPECT(preOutstanding == mpt(-10'000));
            BEAST_EXPECT(preEscrowed == 0);

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = paychan::channel(alice, gw, env.seq(alice));
            env(paychan::create(alice, gw, mpt(1'000), settleDelay, pk));
            env.close();

            BEAST_EXPECT(env.balance(alice, mpt) == preAliceMPT - mpt(1'000));
            BEAST_EXPECT(mptEscrowed(env, alice, mpt) == 1'000);
            BEAST_EXPECT(env.balance(gw, mpt) == preOutstanding);
            BEAST_EXPECT(issuerMPTEscrowed(env, mpt) == preEscrowed + 1'000);

            // issuer (dest) can claim paychan
            auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, mpt(1'000));
            env(paychan::claim(gw, chan, mpt(1'000), mpt(1'000), Slice(sig), pk));
            env.close();

            BEAST_EXPECT(env.balance(alice, mpt) == preAliceMPT - mpt(1'000));
            BEAST_EXPECT(mptEscrowed(env, alice, mpt) == 0);
            BEAST_EXPECT(env.balance(gw, mpt) == preOutstanding + mpt(1'000));
            BEAST_EXPECT(issuerMPTEscrowed(env, mpt) == preEscrowed);
        }
    }

    void
    testMPTLockedRate(FeatureBitset features)
    {
        testcase("MPT Locked Rate");
        using namespace test::jtx;
        using namespace std::literals;

        // test locked rate: claim
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.transferFee = 25000,
                 .ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env(pay(gw, bob, mpt(10'000)));
            env.close();

            // alice can create paychan w/ xfer rate
            auto const preAlice = env.balance(alice, mpt);
            auto const seq1 = env.seq(alice);
            auto const delta = mpt(125);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, mpt(125), settleDelay, pk));
            env.close();
            auto const transferRate = paychan::rate(env, alice, bob, seq1);
            BEAST_EXPECT(transferRate.value == std::uint32_t(1'000'000'000 * 1.25));

            // bob can claim paychan
            auto const chan = paychan::channel(alice, bob, seq1);
            auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, delta);
            env(paychan::claim(bob, chan, delta, delta, Slice(sig), pk));
            env.close();

            BEAST_EXPECT(env.balance(alice, mpt) == preAlice - delta);
            BEAST_EXPECT(env.balance(bob, mpt) == mpt(10'100));
        }

        // test locked rate: close
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.transferFee = 25000,
                 .ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env(pay(gw, bob, mpt(10'000)));
            env.close();

            // alice can create paychan w/ xfer rate
            auto const preAlice = env.balance(alice, mpt);
            auto const preBob = env.balance(bob, mpt);
            auto const seq1 = env.seq(alice);
            auto const delta = mpt(125);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, mpt(125), settleDelay, pk));
            env.close();
            auto const transferRate = paychan::rate(env, alice, bob, seq1);
            BEAST_EXPECT(transferRate.value == std::uint32_t(1'000'000'000 * 1.25));

            // bob can close paychan
            auto const chan = paychan::channel(alice, bob, seq1);
            env(paychan::claim(bob, chan), Txflags(tfClose));
            env.close();

            BEAST_EXPECT(env.balance(alice, mpt) == preAlice);
            BEAST_EXPECT(env.balance(bob, mpt) == preBob);
        }
    }

    // void
    // testMPTRequireAuth(FeatureBitset features)
    // {
    //     testcase("MPT Require Auth");
    //     using namespace test::jtx;
    //     using namespace std::literals;

    //     Env env{*this, features};
    //     auto const baseFee = env.current()->fees().base;
    //     auto const alice = Account("alice");
    //     auto const bob = Account("bob");
    //     auto const gw = Account("gw");

    //     MPTTester mptGw(env, gw, {.holders = {alice, bob}});
    //     mptGw.create(
    //         {.ownerCount = 1,
    //          .holderCount = 0,
    //          .flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTRequireAuth});
    //     mptGw.authorize({.account = alice});
    //     mptGw.authorize({.account = gw, .holder = alice});
    //     mptGw.authorize({.account = bob});
    //     mptGw.authorize({.account = gw, .holder = bob});
    //     auto const MPT = mptGw["MPT"];
    //     env(pay(gw, alice, MPT(10'000)));
    //     env.close();

    //     auto seq = env.seq(alice);
    //     auto const delta = MPT(125);
    //     // alice can create escrow - is authorized
    //     env(escrow::create(alice, bob, MPT(100)),
    //         escrow::condition(escrow::cb1),
    //         escrow::finish_time(env.now() + 1s),
    //         Fee(baseFee * 150));
    //     env.close();

    //     // bob can finish escrow - is authorized
    //     env(escrow::finish(bob, alice, seq),
    //         escrow::condition(escrow::cb1),
    //         escrow::fulfillment(escrow::fb1),
    //         Fee(baseFee * 150));
    //     env.close();
    // }

    void
    testMPTLock(FeatureBitset features)
    {
        testcase("MPT Lock");
        using namespace test::jtx;
        using namespace std::literals;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");

        MPTTester mptGw(env, gw, {.holders = {alice, bob}});
        mptGw.create(
            {.ownerCount = 1,
             .holderCount = 0,
             .flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTCanLock});
        mptGw.authorize({.account = alice});
        mptGw.authorize({.account = bob});
        auto const mpt = mptGw["MPT"];
        env(pay(gw, alice, mpt(10'000)));
        env(pay(gw, bob, mpt(10'000)));
        env.close();

        // alice create paychan
        auto const pk = alice.pk();
        auto const settleDelay = 100s;
        auto const chan = paychan::channel(alice, bob, env.seq(alice));
        env(paychan::create(alice, bob, mpt(100), settleDelay, pk));
        env.close();

        // lock account & dest
        mptGw.set({.account = gw, .holder = alice, .flags = tfMPTLock});
        mptGw.set({.account = gw, .holder = bob, .flags = tfMPTLock});

        // bob cannot claim
        auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, mpt(100));
        env(paychan::claim(bob, chan, mpt(100), mpt(100), Slice(sig), pk), Ter(tecLOCKED));
        env.close();

        // bob can claim/close
        env(paychan::claim(bob, chan), Txflags(tfClose));
        env.close();
    }

    void
    testMPTCanTransfer(FeatureBitset features)
    {
        testcase("MPT Can Transfer");
        using namespace test::jtx;
        using namespace std::literals;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");

        MPTTester mptGw(env, gw, {.holders = {alice, bob}});
        mptGw.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow});
        mptGw.authorize({.account = alice});
        mptGw.authorize({.account = bob});
        auto const mpt = mptGw["MPT"];
        env(pay(gw, alice, mpt(10'000)));
        env(pay(gw, bob, mpt(10'000)));
        env.close();

        // alice cannot create paychan to non issuer
        auto const pk = alice.pk();
        auto const settleDelay = 100s;
        env(paychan::create(alice, bob, mpt(100), settleDelay, pk), Ter(tecNO_AUTH));
        env.close();

        // PayChan Create & Claim
        {
            // alice can create paychan to issuer
            auto const chan = paychan::channel(alice, gw, env.seq(alice));
            env(paychan::create(alice, gw, mpt(100), settleDelay, pk));
            env.close();

            // gw can claim
            auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, mpt(100));
            env(paychan::claim(gw, chan, mpt(100), mpt(100), Slice(sig), pk));
            env.close();
        }

        // PayChan Create & Close
        {
            // alice can create paychan to issuer
            auto const chan = paychan::channel(alice, gw, env.seq(alice));
            env(paychan::create(alice, gw, mpt(100), settleDelay, pk));
            env.close();

            // gw can claim/close
            env(paychan::claim(gw, chan), Txflags(tfClose));
            env.close();
        }
    }

    void
    testMPTDestroy(FeatureBitset features)
    {
        testcase("MPT Destroy");
        using namespace test::jtx;
        using namespace std::literals;

        // tecHAS_OBLIGATIONS: issuer cannot destroy issuance
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env(pay(gw, bob, mpt(10'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, mpt(10), settleDelay, pk));
            env.close();

            env(pay(alice, gw, mpt(10'000)), Ter(tecPATH_PARTIAL));
            env(pay(alice, gw, mpt(9'990)));
            env(pay(bob, gw, mpt(10'000)));
            BEAST_EXPECT(env.balance(alice, mpt) == mpt(0));
            BEAST_EXPECT(mptEscrowed(env, alice, mpt) == 10);
            BEAST_EXPECT(env.balance(bob, mpt) == mpt(0));
            BEAST_EXPECT(mptEscrowed(env, bob, mpt) == 0);
            BEAST_EXPECT(env.balance(gw, mpt) == mpt(-10));
            mptGw.authorize({.account = bob, .flags = tfMPTUnauthorize});
            mptGw.destroy({.id = mptGw.issuanceID(), .ownerCount = 1, .err = tecHAS_OBLIGATIONS});

            auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, mpt(10));
            env(paychan::claim(bob, chan, mpt(10), mpt(10), Slice(sig), pk), Ter(tesSUCCESS));
            env.close();

            env(pay(bob, gw, mpt(10)));
            mptGw.destroy({.id = mptGw.issuanceID(), .ownerCount = 0});
        }

        // tecHAS_OBLIGATIONS: holder cannot destroy mptoken
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            env.fund(XRP(10'000), bob);
            env.close();

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, mpt(10), settleDelay, pk), Ter(tesSUCCESS));
            env.close();

            env(pay(alice, gw, mpt(9'990)));
            env.close();

            BEAST_EXPECT(env.balance(alice, mpt) == mpt(0));
            BEAST_EXPECT(mptEscrowed(env, alice, mpt) == 10);
            mptGw.authorize(
                {.account = alice, .flags = tfMPTUnauthorize, .err = tecHAS_OBLIGATIONS});

            auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, mpt(10));
            env(paychan::claim(bob, chan, mpt(10), mpt(10), Slice(sig), pk), Ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, mpt) == mpt(0));
            BEAST_EXPECT(mptEscrowed(env, alice, mpt) == 0);
            mptGw.authorize({.account = alice, .flags = tfMPTUnauthorize});
            BEAST_EXPECT(!env.le(keylet::mptoken(mpt.mpt(), alice)));
        }
    }

    void
    testIOUClawbackInteraction(FeatureBitset features)
    {
        testcase("IOU Clawback Interaction");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const usd = gw["USD"];

        // Attempt to shelter funds from clawback by locking in channel
        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            env.trust(usd(100'000), alice);
            env.trust(usd(100'000), bob);
            env.close();
            env(pay(gw, alice, usd(5'000)));
            env(pay(gw, bob, usd(5'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const seq1 = env.seq(alice);
            env(paychan::create(alice, bob, usd(4'000), settleDelay, pk));
            env.close();

            BEAST_EXPECT(env.balance(alice, usd) == usd(1'000));
            BEAST_EXPECT(issuerEscrowed(env, gw, usd) == usd(4'000));

            env(claw(gw, alice["USD"](1'000)));
            env.close();
            BEAST_EXPECT(env.balance(alice, usd) == usd(0));

            auto const chan = paychan::channel(alice, bob, seq1);
            BEAST_EXPECT(paychan::channelExists(*env.current(), chan));
            BEAST_EXPECT(paychan::channelAmount(*env.current(), chan) == usd(4'000));
            BEAST_EXPECT(issuerEscrowed(env, gw, usd) == usd(4'000));

            env(paychan::claim(bob, chan), Txflags(tfClose));
            env.close();

            // Alice recovered 4000 USD that survived the clawback
            BEAST_EXPECT(env.balance(alice, usd) == usd(4'000));
            BEAST_EXPECT(issuerEscrowed(env, gw, usd) == usd(0));
        }

        // Clawback from dest with active channel (claim after clawback)
        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            env.trust(usd(100'000), alice);
            env.trust(usd(100'000), bob);
            env.close();
            env(pay(gw, alice, usd(5'000)));
            env(pay(gw, bob, usd(5'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const seq1 = env.seq(alice);
            env(paychan::create(alice, bob, usd(1'000), settleDelay, pk));
            env.close();

            env(claw(gw, bob["USD"](5'000)));
            env.close();
            BEAST_EXPECT(env.balance(bob, usd) == usd(0));

            auto const chan = paychan::channel(alice, bob, seq1);
            auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, usd(1'000));
            env(paychan::claim(bob, chan, usd(1'000), usd(1'000), Slice(sig), pk));
            env.close();

            BEAST_EXPECT(env.balance(bob, usd) == usd(1'000));
        }
    }

    void
    testIOUFundAfterFreeze(FeatureBitset features)
    {
        testcase("IOU Fund After Freeze");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const usd = gw["USD"];

        // Fund channel after destination is frozen
        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(usd(100'000), alice);
            env.trust(usd(100'000), bob);
            env.close();
            env(pay(gw, alice, usd(10'000)));
            env(pay(gw, bob, usd(5'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const seq1 = env.seq(alice);
            env(paychan::create(alice, bob, usd(1'000), settleDelay, pk));
            env.close();

            env(trust(gw, usd(100'000), bob, tfSetFreeze));
            env.close();

            // Cannot fund a channel whose destination is frozen; funding is
            // subject to the same issuer controls as create
            auto const chan = paychan::channel(alice, bob, seq1);
            env(paychan::fund(alice, chan, usd(1'000)), Ter(tecFROZEN));
            env.close();

            BEAST_EXPECT(paychan::channelAmount(*env.current(), chan) == usd(1'000));

            // The destination can still claim funds locked before the freeze
            auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, usd(500));
            env(paychan::claim(bob, chan, usd(500), usd(500), Slice(sig), pk), Ter(tesSUCCESS));
            env.close();
        }

        // Fund channel after sender frozen
        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(usd(100'000), alice);
            env.trust(usd(100'000), bob);
            env.close();
            env(pay(gw, alice, usd(10'000)));
            env(pay(gw, bob, usd(5'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const seq1 = env.seq(alice);
            env(paychan::create(alice, bob, usd(1'000), settleDelay, pk));
            env.close();

            env(trust(gw, usd(100'000), alice, tfSetFreeze));
            env.close();

            auto const chan = paychan::channel(alice, bob, seq1);
            env(paychan::fund(alice, chan, usd(1'000)), Ter(tecFROZEN));
            env.close();
            BEAST_EXPECT(paychan::channelAmount(*env.current(), chan) == usd(1'000));
        }

        // Close channel refund with deep frozen sender
        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(usd(100'000), alice);
            env.trust(usd(100'000), bob);
            env.close();
            env(pay(gw, alice, usd(10'000)));
            env(pay(gw, bob, usd(5'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const seq1 = env.seq(alice);
            env(paychan::create(alice, bob, usd(1'000), settleDelay, pk));
            env.close();
            auto const preAlice = env.balance(alice, usd);

            env(trust(gw, usd(100'000), alice, tfSetFreeze | tfSetDeepFreeze));
            env.close();

            auto const chan = paychan::channel(alice, bob, seq1);
            env(paychan::claim(bob, chan), Txflags(tfClose));
            env.close();

            BEAST_EXPECT(env.balance(alice, usd) == preAlice + usd(1'000));
            BEAST_EXPECT(!paychan::channelExists(*env.current(), chan));
        }
    }

    void
    testIOUFundIssuerControls(FeatureBitset features)
    {
        testcase("IOU Fund Issuer Controls");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const usd = gw["USD"];

        // tecINSUFFICIENT_FUNDS: cannot fund more than the spendable balance
        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(usd(100'000), alice);
            env.trust(usd(100'000), bob);
            env.close();
            env(pay(gw, alice, usd(2'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const seq1 = env.seq(alice);
            env(paychan::create(alice, bob, usd(1'000), settleDelay, pk));
            env.close();

            // alice pays away the rest of her spendable balance
            env(pay(alice, bob, usd(900)));
            env.close();

            auto const chan = paychan::channel(alice, bob, seq1);
            env(paychan::fund(alice, chan, usd(1'000)), Ter(tecINSUFFICIENT_FUNDS));
            env.close();
            BEAST_EXPECT(paychan::channelAmount(*env.current(), chan) == usd(1'000));

            // funding the spendable balance still works
            env(paychan::fund(alice, chan, usd(100)), Ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(paychan::channelAmount(*env.current(), chan) == usd(1'100));
        }

        // tecPRECISION_LOSS: cannot fund an amount the channel amount cannot
        // absorb, even when the source's spendable balance can. Without the
        // channel-amount canAdd guard the source would be debited while the
        // channel amount rounds back unchanged.
        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(usd(100000000000000000), alice);
            env.trust(usd(100000000000000000), bob);
            env.close();
            env(pay(gw, alice, usd(10000000000000000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const seq1 = env.seq(alice);
            env(paychan::create(alice, bob, usd(1000000000000000), settleDelay, pk));
            env.close();

            // alice pays away most of her balance so her small spendable
            // amount passes the helper's canAdd(spendable, amount) check
            env(pay(alice, bob, usd(8999999999999995)));
            env.close();
            BEAST_EXPECT(env.balance(alice, usd) == usd(5));

            auto const chan = paychan::channel(alice, bob, seq1);
            env(paychan::fund(alice, chan, usd(0.001)), Ter(tecPRECISION_LOSS));
            env.close();

            // neither the channel amount nor alice's balance changed
            BEAST_EXPECT(paychan::channelAmount(*env.current(), chan) == usd(1000000000000000));
            BEAST_EXPECT(env.balance(alice, usd) == usd(5));
        }
    }

    void
    testMPTFundIssuerControls(FeatureBitset features)
    {
        testcase("MPT Fund Issuer Controls");
        using namespace test::jtx;
        using namespace std::literals;

        // tecNO_AUTH: cannot fund after the issuer revokes authorization
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTRequireAuth});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = gw, .holder = alice});
            mptGw.authorize({.account = bob});
            mptGw.authorize({.account = gw, .holder = bob});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const seq1 = env.seq(alice);
            env(paychan::create(alice, bob, mpt(100), settleDelay, pk));
            env.close();

            // unauthorize the source account
            mptGw.authorize({.account = gw, .holder = alice, .flags = tfMPTUnauthorize});

            auto const chan = paychan::channel(alice, bob, seq1);
            env(paychan::fund(alice, chan, mpt(100)), Ter(tecNO_AUTH));
            env.close();
            BEAST_EXPECT(paychan::channelAmount(*env.current(), chan) == mpt(100));

            // re-authorize the source, unauthorize the destination
            mptGw.authorize({.account = gw, .holder = alice});
            mptGw.authorize({.account = gw, .holder = bob, .flags = tfMPTUnauthorize});

            env(paychan::fund(alice, chan, mpt(100)), Ter(tecNO_AUTH));
            env.close();
            BEAST_EXPECT(paychan::channelAmount(*env.current(), chan) == mpt(100));

            // funding works again once both parties are authorized
            mptGw.authorize({.account = gw, .holder = bob});
            env(paychan::fund(alice, chan, mpt(100)), Ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(paychan::channelAmount(*env.current(), chan) == mpt(200));
            BEAST_EXPECT(mptEscrowed(env, alice, mpt) == 200);
        }

        // tecLOCKED: cannot fund while the source or destination is locked
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTCanLock});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env(pay(gw, bob, mpt(10'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const seq1 = env.seq(alice);
            env(paychan::create(alice, bob, mpt(100), settleDelay, pk));
            env.close();

            // lock the source account
            mptGw.set({.account = gw, .holder = alice, .flags = tfMPTLock});

            auto const chan = paychan::channel(alice, bob, seq1);
            env(paychan::fund(alice, chan, mpt(100)), Ter(tecLOCKED));
            env.close();
            BEAST_EXPECT(paychan::channelAmount(*env.current(), chan) == mpt(100));

            // unlock the source, lock the destination
            mptGw.set({.account = gw, .holder = alice, .flags = tfMPTUnlock});
            mptGw.set({.account = gw, .holder = bob, .flags = tfMPTLock});

            env(paychan::fund(alice, chan, mpt(100)), Ter(tecLOCKED));
            env.close();
            BEAST_EXPECT(paychan::channelAmount(*env.current(), chan) == mpt(100));

            // funding works again once both parties are unlocked
            mptGw.set({.account = gw, .holder = bob, .flags = tfMPTUnlock});
            env(paychan::fund(alice, chan, mpt(100)), Ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(paychan::channelAmount(*env.current(), chan) == mpt(200));
            BEAST_EXPECT(mptEscrowed(env, alice, mpt) == 200);
        }
    }

    void
    testIOUDeepFreezeAfterCreate(FeatureBitset features)
    {
        testcase("IOU Deep Freeze After Create");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const usd = gw["USD"];

        // Create channel, deep freeze sender, bob claims (sender freeze
        // doesn't block claim since funds already locked)
        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(usd(100'000), alice);
            env.trust(usd(100'000), bob);
            env.close();
            env(pay(gw, alice, usd(10'000)));
            env(pay(gw, bob, usd(5'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const seq1 = env.seq(alice);
            env(paychan::create(alice, bob, usd(1'000), settleDelay, pk));
            env.close();

            // Deep freeze alice's trust line
            env(trust(gw, usd(100'000), alice, tfSetFreeze | tfSetDeepFreeze));
            env.close();

            // Bob claims - only dest freeze is checked, not sender
            auto const chan = paychan::channel(alice, bob, seq1);
            auto const preBob = env.balance(bob, usd);
            auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, usd(500));
            env(paychan::claim(bob, chan, usd(500), usd(500), Slice(sig), pk), Ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(bob, usd) == preBob + usd(500));
        }
    }

    void
    testIOUMultiChannelDrain(FeatureBitset features)
    {
        testcase("IOU Multi Channel Drain");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const usd = gw["USD"];

        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, carol, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(usd(100'000), alice, bob, carol);
            env.close();
            env(pay(gw, alice, usd(5'000)));
            env(pay(gw, bob, usd(5'000)));
            env(pay(gw, carol, usd(5'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;

            auto const seq1 = env.seq(alice);
            env(paychan::create(alice, bob, usd(3'000), settleDelay, pk));
            env.close();
            BEAST_EXPECT(env.balance(alice, usd) == usd(2'000));

            auto const seq2 = env.seq(alice);
            env(paychan::create(alice, carol, usd(2'000), settleDelay, pk));
            env.close();
            BEAST_EXPECT(env.balance(alice, usd) == usd(0));

            env(paychan::create(alice, bob, usd(1), settleDelay, pk), Ter(tecINSUFFICIENT_FUNDS));
            env.close();

            auto const chan1 = paychan::channel(alice, bob, seq1);
            auto sig = paychan::signClaimAuth(pk, alice.sk(), chan1, usd(3'000));
            env(paychan::claim(bob, chan1, usd(3'000), usd(3'000), Slice(sig), pk));
            env.close();
            BEAST_EXPECT(env.balance(bob, usd) == usd(8'000));

            auto const chan2 = paychan::channel(alice, carol, seq2);
            sig = paychan::signClaimAuth(pk, alice.sk(), chan2, usd(2'000));
            env(paychan::claim(carol, chan2, usd(2'000), usd(2'000), Slice(sig), pk));
            env.close();
            BEAST_EXPECT(env.balance(carol, usd) == usd(7'000));
            BEAST_EXPECT(env.balance(alice, usd) == usd(0));
        }
    }

    void
    testIOUTransferRatePartialClaims(FeatureBitset features)
    {
        testcase("IOU Transfer Rate Partial Claims");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const usd = gw["USD"];

        // Partial claim at high rate, rate drops, second claim at lower rate
        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env(rate(gw, 1.25));
            env.close();
            env.trust(usd(100'000), alice);
            env.trust(usd(100'000), bob);
            env.close();
            env(pay(gw, alice, usd(10'000)));
            env(pay(gw, bob, usd(10'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const seq1 = env.seq(alice);
            env(paychan::create(alice, bob, usd(1'000), settleDelay, pk));
            env.close();

            auto const preBob = env.balance(bob, usd);
            auto const chan = paychan::channel(alice, bob, seq1);

            auto sig = paychan::signClaimAuth(pk, alice.sk(), chan, usd(500));
            env(paychan::claim(bob, chan, usd(500), usd(500), Slice(sig), pk));
            env.close();
            BEAST_EXPECT(env.balance(bob, usd) == preBob + usd(400));

            env(rate(gw, 1.0));
            env.close();

            sig = paychan::signClaimAuth(pk, alice.sk(), chan, usd(1'000));
            env(paychan::claim(bob, chan, usd(1'000), usd(1'000), Slice(sig), pk));
            env.close();

            BEAST_EXPECT(env.balance(bob, usd) == preBob + usd(900));
        }

        // Create at parity, issuer raises rate, claim uses locked parity
        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(usd(100'000), alice);
            env.trust(usd(100'000), bob);
            env.close();
            env(pay(gw, alice, usd(10'000)));
            env(pay(gw, bob, usd(10'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const seq1 = env.seq(alice);
            env(paychan::create(alice, bob, usd(1'000), settleDelay, pk));
            env.close();

            env(rate(gw, 2.0));
            env.close();

            auto const preBob = env.balance(bob, usd);
            auto const chan = paychan::channel(alice, bob, seq1);

            auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, usd(1'000));
            env(paychan::claim(bob, chan, usd(1'000), usd(1'000), Slice(sig), pk));
            env.close();

            BEAST_EXPECT(env.balance(bob, usd) == preBob + usd(1'000));
        }
    }

    void
    testIOUTrustLineLimitClaim(FeatureBitset features)
    {
        testcase("IOU Trust Line Limit Claim");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const usd = gw["USD"];

        // Claim that would exceed bob's trust line limit
        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env(trust(alice, usd(100'000)));
            env(trust(bob, usd(500)));
            env.close();
            env(pay(gw, alice, usd(10'000)));
            env(pay(gw, bob, usd(200)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const seq1 = env.seq(alice);
            env(paychan::create(alice, bob, usd(1'000), settleDelay, pk));
            env.close();

            auto const chan = paychan::channel(alice, bob, seq1);

            // Bob claims 250 for self — succeeds (200 + 250 = 450 < 500)
            auto sig = paychan::signClaimAuth(pk, alice.sk(), chan, usd(250));
            env(paychan::claim(bob, chan, usd(250), usd(250), Slice(sig), pk));
            env.close();
            BEAST_EXPECT(env.balance(bob, usd) == usd(450));

            // Bob claims past his limit — succeeds; the limit only protects
            // against unsolicited holdings and bob consents by submitting
            // the claim himself (450 + 350 > 500)
            sig = paychan::signClaimAuth(pk, alice.sk(), chan, usd(600));
            env(paychan::claim(bob, chan, usd(600), usd(600), Slice(sig), pk));
            env.close();
            BEAST_EXPECT(env.balance(bob, usd) == usd(800));

            // Alice claiming on bob's behalf is blocked by bob's limit
            env(paychan::claim(alice, chan, usd(900), usd(900)),
                Txflags(tfClose),
                Ter(tecLIMIT_EXCEEDED));
            env.close();
            BEAST_EXPECT(env.balance(bob, usd) == usd(800));
        }
    }

    void
    testIOUAllowLockingClearedClaim(FeatureBitset features)
    {
        testcase("IOU Allow Locking Cleared Claim");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const usd = gw["USD"];

        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(usd(100'000), alice);
            env.trust(usd(100'000), bob);
            env.close();
            env(pay(gw, alice, usd(10'000)));
            env(pay(gw, bob, usd(10'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const seq1 = env.seq(alice);
            env(paychan::create(alice, bob, usd(1'000), settleDelay, pk));
            env.close();

            env(fclear(gw, asfAllowTrustLineLocking));
            env.close();

            auto const preBob = env.balance(bob, usd);

            auto const chan = paychan::channel(alice, bob, seq1);
            auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, usd(1'000));
            env(paychan::claim(bob, chan, usd(1'000), usd(1'000), Slice(sig), pk));
            env.close();

            BEAST_EXPECT(env.balance(bob, usd) == preBob + usd(1'000));

            env(paychan::create(alice, bob, usd(1'000), settleDelay, pk), Ter(tecNO_PERMISSION));
            env.close();
        }
    }

    void
    testMPTClawbackInteraction(FeatureBitset features)
    {
        testcase("MPT Clawback Interaction");
        using namespace test::jtx;
        using namespace std::literals;

        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTCanClawback});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(5'000)));
            env(pay(gw, bob, mpt(5'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const seq1 = env.seq(alice);
            env(paychan::create(alice, bob, mpt(4'000), settleDelay, pk));
            env.close();

            BEAST_EXPECT(env.balance(alice, mpt) == mpt(1'000));
            BEAST_EXPECT(mptEscrowed(env, alice, mpt) == 4'000);

            mptGw.claw(gw, alice, 1'000);
            BEAST_EXPECT(env.balance(alice, mpt) == mpt(0));

            auto const chan = paychan::channel(alice, bob, seq1);
            BEAST_EXPECT(paychan::channelExists(*env.current(), chan));
            BEAST_EXPECT(mptEscrowed(env, alice, mpt) == 4'000);

            env(paychan::claim(bob, chan), Txflags(tfClose));
            env.close();

            BEAST_EXPECT(env.balance(alice, mpt) == mpt(4'000));
            BEAST_EXPECT(mptEscrowed(env, alice, mpt) == 0);
        }
    }

    void
    testMPTClaimAutoCreate(FeatureBitset features)
    {
        testcase("MPT Claim Auto Create");
        using namespace test::jtx;
        using namespace std::literals;

        // Claim auto-creates MPToken for receiver without authorize
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            env.fund(XRP(10'000), bob);

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env.close();

            BEAST_EXPECT(!env.le(keylet::mptoken(mpt.mpt(), bob)));

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const seq1 = env.seq(alice);
            env(paychan::create(alice, bob, mpt(1'000), settleDelay, pk));
            env.close();

            auto const chan = paychan::channel(alice, bob, seq1);
            auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, mpt(500));
            env(paychan::claim(bob, chan, mpt(500), mpt(500), Slice(sig), pk));
            env.close();

            BEAST_EXPECT(env.le(keylet::mptoken(mpt.mpt(), bob)));
            BEAST_EXPECT(env.balance(bob, mpt) == mpt(500));
        }

        // requireAuth blocks claim even with auto-create
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            env.fund(XRP(10'000), bob);

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTRequireAuth});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = gw, .holder = alice});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, mpt(1'000), settleDelay, pk), Ter(tecNO_AUTH));
            env.close();
        }
    }

    void
    testMPTFreezeClaimClose(FeatureBitset features)
    {
        testcase("MPT Freeze Claim Close");
        using namespace test::jtx;
        using namespace std::literals;

        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTCanLock});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env(pay(gw, bob, mpt(5'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const seq1 = env.seq(alice);
            env(paychan::create(alice, bob, mpt(1'000), settleDelay, pk));
            env.close();

            auto const preAlice = env.balance(alice, mpt);

            mptGw.set({.account = gw, .holder = alice, .flags = tfMPTLock});

            auto const chan = paychan::channel(alice, bob, seq1);
            auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, mpt(500));

            env(paychan::claim(bob, chan, mpt(500), mpt(500), Slice(sig), pk), Ter(tesSUCCESS));
            env.close();

            mptGw.set({.account = gw, .holder = bob, .flags = tfMPTLock});

            auto const sig2 = paychan::signClaimAuth(pk, alice.sk(), chan, mpt(1'000));
            env(paychan::claim(bob, chan, mpt(1'000), mpt(1'000), Slice(sig2), pk), Ter(tecLOCKED));
            env.close();

            env(paychan::claim(bob, chan), Txflags(tfClose));
            env.close();

            BEAST_EXPECT(env.balance(alice, mpt) == preAlice + mpt(500));
            BEAST_EXPECT(!paychan::channelExists(*env.current(), chan));
        }
    }

    void
    testMPTCanEscrowRequired(FeatureBitset features)
    {
        testcase("MPT CanEscrow Required");
        using namespace test::jtx;
        using namespace std::literals;

        // Without canEscrow flag, channel creation fails
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env(pay(gw, bob, mpt(5'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, mpt(1'000), settleDelay, pk), Ter(tecNO_PERMISSION));
            env.close();
        }

        // With canEscrow flag, channel works and claim succeeds
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env(pay(gw, bob, mpt(5'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const seq1 = env.seq(alice);
            env(paychan::create(alice, bob, mpt(1'000), settleDelay, pk));
            env.close();

            auto const chan = paychan::channel(alice, bob, seq1);
            auto const preBob = env.balance(bob, mpt);
            auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, mpt(1'000));
            env(paychan::claim(bob, chan, mpt(1'000), mpt(1'000), Slice(sig), pk));
            env.close();

            BEAST_EXPECT(env.balance(bob, mpt) == preBob + mpt(1'000));
        }
    }

    void
    testIOUWithFeats(FeatureBitset features)
    {
        testIOUEnablement(features);
        testIOUAllowLockingFlag(features);
        testIOUCreatePreflight(features);
        testIOUCreatePreclaim(features);
        testIOUClaimPreclaim(features);
        testIOUClaimDoApply(features);
        // testIOUClaimClosePreclaim(features);
        testIOUBalances(features);
        testIOUMetaAndOwnership(features);
        testIOURippleState(features);
        testIOUGateway(features);
        testIOULockedRate(features);
        testIOULimitAmount(features);
        testIOURequireAuth(features);
        testIOUFreeze(features);
        testIOUInsf(features);
        testIOUPrecisionLoss(features);
        testIOUClawbackInteraction(features);
        testIOUFundAfterFreeze(features);
        testIOUFundIssuerControls(features);
        testIOUDeepFreezeAfterCreate(features);
        testIOUMultiChannelDrain(features);
        testIOUTransferRatePartialClaims(features);
        testIOUTrustLineLimitClaim(features);
        testIOUAllowLockingClearedClaim(features);
    }

    void
    testMPTWithFeats(FeatureBitset features)
    {
        testMPTEnablement(features);
        testMPTCreatePreflight(features);
        testMPTCreatePreclaim(features);
        testMPTClaimPreclaim(features);
        testMPTClaimDoApply(features);
        // testMPTClaimClosePreclaim(features);
        testMPTBalances(features);
        testMPTMetaAndOwnership(features);
        testMPTGateway(features);
        testMPTLockedRate(features);
        // testMPTRequireAuth(features);
        testMPTLock(features);
        testMPTCanTransfer(features);
        testMPTDestroy(features);
        testMPTClawbackInteraction(features);
        testMPTClaimAutoCreate(features);
        testMPTFreezeClaimClose(features);
        testMPTCanEscrowRequired(features);
        testMPTFundIssuerControls(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{testableAmendments()};
        testIOUWithFeats(all);
        testMPTWithFeats(all);
    }
};

BEAST_DEFINE_TESTSUITE(PayChanToken, app, xrpl);
}  // namespace xrpl::test
