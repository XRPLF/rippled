#include <test/jtx.h>
#include <test/jtx/AMM.h>

#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

class Freeze_test : public beast::unit_test::suite
{
    void
    testRippleState(FeatureBitset features)
    {
        testcase("RippleState Freeze");

        using namespace test::jtx;
        Env env(*this, features);

        Account G1{"G1"};
        Account alice{"alice"};
        Account bob{"bob"};

        env.fund(XRP(1000), G1, alice, bob);
        env.close();

        env.trust(G1["USD"](100), bob);
        env.trust(G1["USD"](100), alice);
        env.close();

        env(pay(G1, bob, G1["USD"](10)));
        env(pay(G1, alice, G1["USD"](100)));
        env.close();

        env(offer(alice, XRP(500), G1["USD"](100)));
        env.close();

        {
            auto lines = getAccountLines(env, bob);
            if (!BEAST_EXPECT(checkArraySize(lines[jss::lines], 1u)))
                return;
            BEAST_EXPECT(lines[jss::lines][0u][jss::account] == G1.human());
            BEAST_EXPECT(lines[jss::lines][0u][jss::limit] == "100");
            BEAST_EXPECT(lines[jss::lines][0u][jss::balance] == "10");
        }

        {
            auto lines = getAccountLines(env, alice);
            if (!BEAST_EXPECT(checkArraySize(lines[jss::lines], 1u)))
                return;
            BEAST_EXPECT(lines[jss::lines][0u][jss::account] == G1.human());
            BEAST_EXPECT(lines[jss::lines][0u][jss::limit] == "100");
            BEAST_EXPECT(lines[jss::lines][0u][jss::balance] == "100");
        }

        {
            // Account with line unfrozen (proving operations normally work)
            //   test: can make Payment on that line
            env(pay(alice, bob, G1["USD"](1)));

            //   test: can receive Payment on that line
            env(pay(bob, alice, G1["USD"](1)));
            env.close();
        }

        {
            // Is created via a TrustSet with SetFreeze flag
            //   test: sets LowFreeze | HighFreeze flags
            env(trust(G1, bob["USD"](0), tfSetFreeze));
            auto affected = env.meta()->getJson(
                JsonOptions::none)[sfAffectedNodes.fieldName];
            if (!BEAST_EXPECT(checkArraySize(affected, 2u)))
                return;
            auto ff =
                affected[1u][sfModifiedNode.fieldName][sfFinalFields.fieldName];
            BEAST_EXPECT(
                ff[sfLowLimit.fieldName] ==
                G1["USD"](0).value().getJson(JsonOptions::none));
            BEAST_EXPECT(ff[jss::Flags].asUInt() & lsfLowFreeze);
            BEAST_EXPECT(!(ff[jss::Flags].asUInt() & lsfHighFreeze));
            env.close();
        }

        {
            // Account with line frozen by issuer
            //    test: can buy more assets on that line
            env(offer(bob, G1["USD"](5), XRP(25)));
            auto affected = env.meta()->getJson(
                JsonOptions::none)[sfAffectedNodes.fieldName];
            if (!BEAST_EXPECT(checkArraySize(affected, 5u)))
                return;
            auto ff =
                affected[3u][sfModifiedNode.fieldName][sfFinalFields.fieldName];
            BEAST_EXPECT(
                ff[sfHighLimit.fieldName] ==
                bob["USD"](100).value().getJson(JsonOptions::none));
            auto amt = STAmount{Issue{to_currency("USD"), noAccount()}, -15}
                           .value()
                           .getJson(JsonOptions::none);
            BEAST_EXPECT(ff[sfBalance.fieldName] == amt);
            env.close();
        }

        {
            //    test: can not sell assets from that line
            env(offer(bob, XRP(1), G1["USD"](5)), ter(tecUNFUNDED_OFFER));

            //    test: can receive Payment on that line
            env(pay(alice, bob, G1["USD"](1)));

            //    test: can not make Payment from that line
            env(pay(bob, alice, G1["USD"](1)), ter(tecPATH_DRY));
        }

        {
            // check G1 account lines
            //    test: shows freeze
            auto lines = getAccountLines(env, G1);
            Json::Value bobLine;
            for (auto const& it : lines[jss::lines])
            {
                if (it[jss::account] == bob.human())
                {
                    bobLine = it;
                    break;
                }
            }
            if (!BEAST_EXPECT(bobLine))
                return;
            BEAST_EXPECT(bobLine[jss::freeze] == true);
            BEAST_EXPECT(bobLine[jss::balance] == "-16");
        }

        {
            //    test: shows freeze peer
            auto lines = getAccountLines(env, bob);
            Json::Value g1Line;
            for (auto const& it : lines[jss::lines])
            {
                if (it[jss::account] == G1.human())
                {
                    g1Line = it;
                    break;
                }
            }
            if (!BEAST_EXPECT(g1Line))
                return;
            BEAST_EXPECT(g1Line[jss::freeze_peer] == true);
            BEAST_EXPECT(g1Line[jss::balance] == "16");
        }

        {
            // Is cleared via a TrustSet with ClearFreeze flag
            //    test: sets LowFreeze | HighFreeze flags
            env(trust(G1, bob["USD"](0), tfClearFreeze));
            auto affected = env.meta()->getJson(
                JsonOptions::none)[sfAffectedNodes.fieldName];
            if (!BEAST_EXPECT(checkArraySize(affected, 2u)))
                return;
            auto ff =
                affected[1u][sfModifiedNode.fieldName][sfFinalFields.fieldName];
            BEAST_EXPECT(
                ff[sfLowLimit.fieldName] ==
                G1["USD"](0).value().getJson(JsonOptions::none));
            BEAST_EXPECT(!(ff[jss::Flags].asUInt() & lsfLowFreeze));
            BEAST_EXPECT(!(ff[jss::Flags].asUInt() & lsfHighFreeze));
            env.close();
        }
    }

    void
    testDeepFreeze(FeatureBitset features)
    {
        testcase("Deep Freeze");

        using namespace test::jtx;
        Env env(*this, features);

        Account G1{"G1"};
        Account A1{"A1"};

        env.fund(XRP(10000), G1, A1);
        env.close();

        env.trust(G1["USD"](1000), A1);
        env.close();

        if (features[featureDeepFreeze])
        {
            //  test: Issuer deep freezing the trust line in a single
            //  transaction
            env(trust(G1, A1["USD"](0), tfSetFreeze | tfSetDeepFreeze));
            {
                auto const flags = getTrustlineFlags(env, 2u, 1u);
                BEAST_EXPECT(flags & lsfLowFreeze);
                BEAST_EXPECT(flags & lsfLowDeepFreeze);
                BEAST_EXPECT(!(flags & (lsfHighFreeze | lsfHighDeepFreeze)));
                env.close();
            }

            //  test: Issuer clearing deep freeze and normal freeze in a single
            //  transaction
            env(trust(G1, A1["USD"](0), tfClearFreeze | tfClearDeepFreeze));
            {
                auto const flags = getTrustlineFlags(env, 2u, 1u);
                BEAST_EXPECT(!(flags & (lsfLowFreeze | lsfLowDeepFreeze)));
                BEAST_EXPECT(!(flags & (lsfHighFreeze | lsfHighDeepFreeze)));
                env.close();
            }

            //  test: Issuer deep freezing not already frozen line must fail
            env(trust(G1, A1["USD"](0), tfSetDeepFreeze),
                ter(tecNO_PERMISSION));

            env(trust(G1, A1["USD"](0), tfSetFreeze));
            env.close();

            //  test: Issuer deep freezing already frozen trust line
            env(trust(G1, A1["USD"](0), tfSetDeepFreeze));
            {
                auto const flags = getTrustlineFlags(env, 2u, 1u);
                BEAST_EXPECT(flags & lsfLowFreeze);
                BEAST_EXPECT(flags & lsfLowDeepFreeze);
                BEAST_EXPECT(!(flags & (lsfHighFreeze | lsfHighDeepFreeze)));
                env.close();
            }

            //  test: Holder clearing freeze flags has no effect. Each sides'
            //  flags are independent
            env(trust(A1, G1["USD"](0), tfClearFreeze | tfClearDeepFreeze));
            {
                auto const flags = getTrustlineFlags(env, 2u, 1u);
                BEAST_EXPECT(flags & lsfLowFreeze);
                BEAST_EXPECT(flags & lsfLowDeepFreeze);
                BEAST_EXPECT(!(flags & (lsfHighFreeze | lsfHighDeepFreeze)));
                env.close();
            }

            //  test: Issuer can't clear normal freeze when line is deep frozen
            env(trust(G1, A1["USD"](0), tfClearFreeze), ter(tecNO_PERMISSION));

            //  test: Issuer clearing deep freeze but normal freeze is still in
            //  effect
            env(trust(G1, A1["USD"](0), tfClearDeepFreeze));
            {
                auto const flags = getTrustlineFlags(env, 2u, 1u);
                BEAST_EXPECT(flags & lsfLowFreeze);
                BEAST_EXPECT(!(flags & lsfLowDeepFreeze));
                BEAST_EXPECT(!(flags & (lsfHighFreeze | lsfHighDeepFreeze)));
                env.close();
            }
        }
        else
        {
            //  test: applying deep freeze before amendment fails
            env(trust(G1, A1["USD"](0), tfSetDeepFreeze), ter(temINVALID_FLAG));

            //  test: clearing deep freeze before amendment fails
            env(trust(G1, A1["USD"](0), tfClearDeepFreeze),
                ter(temINVALID_FLAG));
        }
    }

    void
    testCreateFrozenTrustline(FeatureBitset features)
    {
        testcase("Create Frozen Trustline");

        using namespace test::jtx;
        Env env(*this, features);

        Account G1{"G1"};
        Account A1{"A1"};

        env.fund(XRP(10000), G1, A1);
        env.close();

        // test: can create frozen trustline
        {
            env(trust(G1, A1["USD"](1000), tfSetFreeze));
            auto const flags = getTrustlineFlags(env, 5u, 3u, false);
            BEAST_EXPECT(flags & lsfLowFreeze);
            env.close();
            env.require(lines(A1, 1));
        }

        // Cleanup
        env(trust(G1, A1["USD"](0), tfClearFreeze));
        env.close();
        env.require(lines(G1, 0));
        env.require(lines(A1, 0));

        // test: cannot create deep frozen trustline without normal freeze
        if (features[featureDeepFreeze])
        {
            env(trust(G1, A1["USD"](1000), tfSetDeepFreeze),
                ter(tecNO_PERMISSION));
            env.close();
            env.require(lines(A1, 0));
        }

        // test: can create deep frozen trustline together with normal freeze
        if (features[featureDeepFreeze])
        {
            env(trust(G1, A1["USD"](1000), tfSetFreeze | tfSetDeepFreeze));
            auto const flags = getTrustlineFlags(env, 5u, 3u, false);
            BEAST_EXPECT(flags & lsfLowFreeze);
            BEAST_EXPECT(flags & lsfLowDeepFreeze);
            env.close();
            env.require(lines(A1, 1));
        }
    }

    void
    testSetAndClear(FeatureBitset features)
    {
        testcase("Freeze Set and Clear");

        using namespace test::jtx;
        Env env(*this, features);

        Account G1{"G1"};
        Account A1{"A1"};

        env.fund(XRP(10000), G1, A1);
        env.close();

        env.trust(G1["USD"](1000), A1);
        env.close();

        if (features[featureDeepFreeze])
        {
            //  test: can't have both set and clear flag families in the same
            //  transaction
            env(trust(G1, A1["USD"](0), tfSetFreeze | tfClearFreeze),
                ter(tecNO_PERMISSION));
            env(trust(G1, A1["USD"](0), tfSetFreeze | tfClearDeepFreeze),
                ter(tecNO_PERMISSION));
            env(trust(G1, A1["USD"](0), tfSetDeepFreeze | tfClearFreeze),
                ter(tecNO_PERMISSION));
            env(trust(G1, A1["USD"](0), tfSetDeepFreeze | tfClearDeepFreeze),
                ter(tecNO_PERMISSION));
        }
        else
        {
            //  test: old behavior, transaction succeed with no effect on a
            //  trust line
            env(trust(G1, A1["USD"](0), tfSetFreeze | tfClearFreeze));
            {
                auto affected = env.meta()->getJson(
                    JsonOptions::none)[sfAffectedNodes.fieldName];
                BEAST_EXPECT(checkArraySize(
                    affected, 1u));  // means no trustline changes
            }
        }
    }

    void
    testGlobalFreeze(FeatureBitset features)
    {
        testcase("Global Freeze");

        using namespace test::jtx;
        Env env(*this, features);

        Account G1{"G1"};
        Account A1{"A1"};
        Account A2{"A2"};
        Account A3{"A3"};
        Account A4{"A4"};

        env.fund(XRP(12000), G1);
        env.fund(XRP(1000), A1);
        env.fund(XRP(20000), A2, A3, A4);
        env.close();

        env.trust(G1["USD"](1200), A1);
        env.trust(G1["USD"](200), A2);
        env.trust(G1["BTC"](100), A3);
        env.trust(G1["BTC"](100), A4);
        env.close();

        env(pay(G1, A1, G1["USD"](1000)));
        env(pay(G1, A2, G1["USD"](100)));
        env(pay(G1, A3, G1["BTC"](100)));
        env(pay(G1, A4, G1["BTC"](100)));
        env.close();

        env(offer(G1, XRP(10000), G1["USD"](100)), txflags(tfPassive));
        env(offer(G1, G1["USD"](100), XRP(10000)), txflags(tfPassive));
        env(offer(A1, XRP(10000), G1["USD"](100)), txflags(tfPassive));
        env(offer(A2, G1["USD"](100), XRP(10000)), txflags(tfPassive));
        env.close();

        {
            // Is toggled via AccountSet using SetFlag and ClearFlag
            //    test: SetFlag GlobalFreeze
            env.require(nflags(G1, asfGlobalFreeze));
            env(fset(G1, asfGlobalFreeze));
            env.require(flags(G1, asfGlobalFreeze));
            env.require(nflags(G1, asfNoFreeze));

            //    test: ClearFlag GlobalFreeze
            env(fclear(G1, asfGlobalFreeze));
            env.require(nflags(G1, asfGlobalFreeze));
            env.require(nflags(G1, asfNoFreeze));
        }

        {
            // Account without GlobalFreeze (proving operations normally work)
            //    test: visible offers where taker_pays is unfrozen issuer
            auto offers = env.rpc(
                "book_offers",
                std::string("USD/") + G1.human(),
                "XRP")[jss::result][jss::offers];
            if (!BEAST_EXPECT(checkArraySize(offers, 2u)))
                return;
            std::set<std::string> accounts;
            for (auto const& offer : offers)
            {
                accounts.insert(offer[jss::Account].asString());
            }
            BEAST_EXPECT(accounts.find(A2.human()) != std::end(accounts));
            BEAST_EXPECT(accounts.find(G1.human()) != std::end(accounts));

            //    test: visible offers where taker_gets is unfrozen issuer
            offers = env.rpc(
                "book_offers",
                "XRP",
                std::string("USD/") + G1.human())[jss::result][jss::offers];
            if (!BEAST_EXPECT(checkArraySize(offers, 2u)))
                return;
            accounts.clear();
            for (auto const& offer : offers)
            {
                accounts.insert(offer[jss::Account].asString());
            }
            BEAST_EXPECT(accounts.find(A1.human()) != std::end(accounts));
            BEAST_EXPECT(accounts.find(G1.human()) != std::end(accounts));
        }

        {
            // Offers/Payments
            //    test: assets can be bought on the market
            env(offer(A3, G1["BTC"](1), XRP(1)));

            //    test: assets can be sold on the market
            env(offer(A4, XRP(1), G1["BTC"](1)));

            //    test: direct issues can be sent
            env(pay(G1, A2, G1["USD"](1)));

            //    test: direct redemptions can be sent
            env(pay(A2, G1, G1["USD"](1)));

            //    test: via rippling can be sent
            env(pay(A2, A1, G1["USD"](1)));

            //    test: via rippling can be sent back
            env(pay(A1, A2, G1["USD"](1)));
        }

        {
            // Account with GlobalFreeze
            //  set GlobalFreeze first
            //    test: SetFlag GlobalFreeze will toggle back to freeze
            env.require(nflags(G1, asfGlobalFreeze));
            env(fset(G1, asfGlobalFreeze));
            env.require(flags(G1, asfGlobalFreeze));
            env.require(nflags(G1, asfNoFreeze));

            //    test: assets can't be bought on the market
            env(offer(A3, G1["BTC"](1), XRP(1)), ter(tecFROZEN));

            //    test: assets can't be sold on the market
            env(offer(A4, XRP(1), G1["BTC"](1)), ter(tecFROZEN));
        }

        {
            // offers are filtered (seems to be broken?)
            //    test: account_offers always shows own offers
            auto offers = getAccountOffers(env, G1)[jss::offers];
            if (!BEAST_EXPECT(checkArraySize(offers, 2u)))
                return;

            //    test: book_offers shows offers
            //    (should these actually be filtered?)
            offers = env.rpc(
                "book_offers",
                "XRP",
                std::string("USD/") + G1.human())[jss::result][jss::offers];
            if (!BEAST_EXPECT(checkArraySize(offers, 2u)))
                return;

            offers = env.rpc(
                "book_offers",
                std::string("USD/") + G1.human(),
                "XRP")[jss::result][jss::offers];
            if (!BEAST_EXPECT(checkArraySize(offers, 2u)))
                return;
        }

        {
            // Payments
            //    test: direct issues can be sent
            env(pay(G1, A2, G1["USD"](1)));

            //    test: direct redemptions can be sent
            env(pay(A2, G1, G1["USD"](1)));

            //    test: via rippling cant be sent
            env(pay(A2, A1, G1["USD"](1)), ter(tecPATH_DRY));
        }
    }

    void
    testNoFreeze(FeatureBitset features)
    {
        testcase("No Freeze");

        using namespace test::jtx;
        Env env(*this, features);

        Account G1{"G1"};
        Account A1{"A1"};
        Account frozenAcc{"A2"};
        Account deepFrozenAcc{"A3"};

        env.fund(XRP(12000), G1);
        env.fund(XRP(1000), A1);
        env.fund(XRP(1000), frozenAcc);
        env.fund(XRP(1000), deepFrozenAcc);
        env.close();

        env.trust(G1["USD"](1000), A1);
        env.trust(G1["USD"](1000), frozenAcc);
        env.trust(G1["USD"](1000), deepFrozenAcc);
        env.close();

        env(pay(G1, A1, G1["USD"](1000)));
        env(pay(G1, frozenAcc, G1["USD"](1000)));
        env(pay(G1, deepFrozenAcc, G1["USD"](1000)));

        // Freezing and deep freezing some of the trust lines to check deep
        // freeze and clearing of freeze separately
        env(trust(G1, frozenAcc["USD"](0), tfSetFreeze));
        {
            auto const flags = getTrustlineFlags(env, 2u, 1u);
            BEAST_EXPECT(flags & lsfLowFreeze);
            BEAST_EXPECT(!(flags & lsfHighFreeze));
        }
        if (features[featureDeepFreeze])
        {
            env(trust(
                G1, deepFrozenAcc["USD"](0), tfSetFreeze | tfSetDeepFreeze));
            {
                auto const flags = getTrustlineFlags(env, 2u, 1u);
                BEAST_EXPECT(!(flags & (lsfLowFreeze | lsfLowDeepFreeze)));
                BEAST_EXPECT(flags & lsfHighFreeze);
                BEAST_EXPECT(flags & lsfHighDeepFreeze);
            }
        }
        env.close();

        // TrustSet NoFreeze
        //    test: should set NoFreeze in Flags
        env.require(nflags(G1, asfNoFreeze));
        env(fset(G1, asfNoFreeze));
        env.require(flags(G1, asfNoFreeze));
        env.require(nflags(G1, asfGlobalFreeze));

        //    test: cannot be cleared
        env(fclear(G1, asfNoFreeze));
        env.require(flags(G1, asfNoFreeze));
        env.require(nflags(G1, asfGlobalFreeze));

        //    test: can set GlobalFreeze
        env(fset(G1, asfGlobalFreeze));
        env.require(flags(G1, asfNoFreeze));
        env.require(flags(G1, asfGlobalFreeze));

        //    test: cannot unset GlobalFreeze
        env(fclear(G1, asfGlobalFreeze));
        env.require(flags(G1, asfNoFreeze));
        env.require(flags(G1, asfGlobalFreeze));

        //    test: trustlines can't be frozen when no freeze enacted
        if (features[featureDeepFreeze])
        {
            env(trust(G1, A1["USD"](0), tfSetFreeze), ter(tecNO_PERMISSION));

            // test: cannot deep freeze already frozen line when no freeze
            // enacted
            env(trust(G1, frozenAcc["USD"](0), tfSetDeepFreeze),
                ter(tecNO_PERMISSION));
        }
        else
        {
            //  test: previous functionality, checking there's no changes to a
            //  trust line
            env(trust(G1, A1["USD"](0), tfSetFreeze));
            auto affected = env.meta()->getJson(
                JsonOptions::none)[sfAffectedNodes.fieldName];
            if (!BEAST_EXPECT(checkArraySize(affected, 1u)))
                return;

            auto let = affected[0u][sfModifiedNode.fieldName]
                               [sfLedgerEntryType.fieldName];
            BEAST_EXPECT(let == jss::AccountRoot);
        }

        //  test: can clear freeze on account
        env(trust(G1, frozenAcc["USD"](0), tfClearFreeze));
        {
            auto const flags = getTrustlineFlags(env, 2u, 1u);
            BEAST_EXPECT(!(flags & lsfLowFreeze));
        }

        if (features[featureDeepFreeze])
        {
            //  test: can clear deep freeze on account
            env(trust(G1, deepFrozenAcc["USD"](0), tfClearDeepFreeze));
            {
                auto const flags = getTrustlineFlags(env, 2u, 1u);
                BEAST_EXPECT(flags & lsfHighFreeze);
                BEAST_EXPECT(!(flags & lsfHighDeepFreeze));
            }
        }
    }

    void
    testOffersWhenFrozen(FeatureBitset features)
    {
        testcase("Offers for Frozen Trust Lines");

        using namespace test::jtx;
        Env env(*this, features);

        Account G1{"G1"};
        Account A2{"A2"};
        Account A3{"A3"};
        Account A4{"A4"};

        env.fund(XRP(1000), G1, A3, A4);
        env.fund(XRP(2000), A2);
        env.close();

        env.trust(G1["USD"](1000), A2);
        env.trust(G1["USD"](2000), A3);
        env.trust(G1["USD"](2000), A4);
        env.close();

        env(pay(G1, A3, G1["USD"](2000)));
        env(pay(G1, A4, G1["USD"](2000)));
        env.close();

        env(offer(A3, XRP(1000), G1["USD"](1000)), txflags(tfPassive));
        env.close();

        // removal after successful payment
        //    test: make a payment with partially consuming offer
        env(pay(A2, G1, G1["USD"](1)), paths(G1["USD"]), sendmax(XRP(1)));
        env.close();

        //    test: offer was only partially consumed
        auto offers = getAccountOffers(env, A3)[jss::offers];
        if (!BEAST_EXPECT(checkArraySize(offers, 1u)))
            return;
        BEAST_EXPECT(
            offers[0u][jss::taker_gets] ==
            G1["USD"](999).value().getJson(JsonOptions::none));

        //    test: someone else creates an offer providing liquidity
        env(offer(A4, XRP(999), G1["USD"](999)));
        env.close();

        //    test: owner of partially consumed offers line is frozen
        env(trust(G1, A3["USD"](0), tfSetFreeze));
        auto affected =
            env.meta()->getJson(JsonOptions::none)[sfAffectedNodes.fieldName];
        if (!BEAST_EXPECT(checkArraySize(affected, 2u)))
            return;
        auto ff =
            affected[1u][sfModifiedNode.fieldName][sfFinalFields.fieldName];
        BEAST_EXPECT(
            ff[sfHighLimit.fieldName] ==
            G1["USD"](0).value().getJson(JsonOptions::none));
        BEAST_EXPECT(!(ff[jss::Flags].asUInt() & lsfLowFreeze));
        BEAST_EXPECT(ff[jss::Flags].asUInt() & lsfHighFreeze);
        env.close();

        // verify offer on the books
        offers = getAccountOffers(env, A3)[jss::offers];
        if (!BEAST_EXPECT(checkArraySize(offers, 1u)))
            return;

        //    test: Can make a payment via the new offer
        env(pay(A2, G1, G1["USD"](1)), paths(G1["USD"]), sendmax(XRP(1)));
        env.close();

        //    test: Partially consumed offer was removed by tes* payment
        offers = getAccountOffers(env, A3)[jss::offers];
        if (!BEAST_EXPECT(checkArraySize(offers, 0u)))
            return;

        // removal buy successful OfferCreate
        //    test: freeze the new offer
        env(trust(G1, A4["USD"](0), tfSetFreeze));
        affected =
            env.meta()->getJson(JsonOptions::none)[sfAffectedNodes.fieldName];
        if (!BEAST_EXPECT(checkArraySize(affected, 2u)))
            return;
        ff = affected[0u][sfModifiedNode.fieldName][sfFinalFields.fieldName];
        BEAST_EXPECT(
            ff[sfLowLimit.fieldName] ==
            G1["USD"](0).value().getJson(JsonOptions::none));
        BEAST_EXPECT(ff[jss::Flags].asUInt() & lsfLowFreeze);
        BEAST_EXPECT(!(ff[jss::Flags].asUInt() & lsfHighFreeze));
        env.close();

        //    test: can no longer create a crossing offer
        env(offer(A2, G1["USD"](999), XRP(999)));
        affected =
            env.meta()->getJson(JsonOptions::none)[sfAffectedNodes.fieldName];
        if (!BEAST_EXPECT(checkArraySize(affected, 8u)))
            return;
        auto created = affected[0u][sfCreatedNode.fieldName];
        BEAST_EXPECT(
            created[sfNewFields.fieldName][jss::Account] == A2.human());
        env.close();

        //    test: offer was removed by offer_create
        offers = getAccountOffers(env, A4)[jss::offers];
        if (!BEAST_EXPECT(checkArraySize(offers, 0u)))
            return;
    }

    void
    testOffersWhenDeepFrozen(FeatureBitset features)
    {
        testcase("Offers on frozen trust lines");

        using namespace test::jtx;
        Env env(*this, features);

        Account G1{"G1"};
        Account A1{"A1"};
        Account A2{"A2"};
        Account A3{"A3"};
        auto const USD{G1["USD"]};

        env.fund(XRP(10000), G1, A1, A2, A3);
        env.close();

        auto const limit = USD(10000);
        env.trust(limit, A1, A2, A3);
        env.close();

        env(pay(G1, A1, USD(1000)));
        env(pay(G1, A2, USD(1000)));
        env.close();

        // Making large passive sell offer
        // Wants to sell 50 USD for 100 XRP
        env(offer(A2, XRP(100), USD(50)), txflags(tfPassive));
        env.close();
        // Making large passive buy offer
        // Wants to buy 100 USD for 100 XRP
        env(offer(A3, USD(100), XRP(100)), txflags(tfPassive));
        env.close();
        env.require(offers(A2, 1), offers(A3, 1));

        // Checking A1 can buy from A2 by crossing it's offer
        env(offer(A1, USD(1), XRP(2)), txflags(tfFillOrKill));
        env.close();
        env.require(balance(A1, USD(1001)), balance(A2, USD(999)));

        // Checking A1 can sell to A3 by crossing it's offer
        env(offer(A1, XRP(1), USD(1)), txflags(tfFillOrKill));
        env.close();
        env.require(balance(A1, USD(1000)), balance(A3, USD(1)));

        // Testing aggressive and passive offer placing, trustline frozen by
        // the issuer
        {
            env(trust(G1, A1["USD"](0), tfSetFreeze));
            env.close();

            // test: can still make passive buy offer
            env(offer(A1, USD(1), XRP(0.5)), txflags(tfPassive));
            env.close();
            env.require(balance(A1, USD(1000)), offers(A1, 1));
            // Cleanup
            env(offer_cancel(A1, env.seq(A1) - 1));
            env.require(offers(A1, 0));
            env.close();

            // test: can still buy from A2
            env(offer(A1, USD(1), XRP(2)), txflags(tfFillOrKill));
            env.close();
            env.require(
                balance(A1, USD(1001)), balance(A2, USD(998)), offers(A1, 0));

            // test: cannot create passive sell offer
            env(offer(A1, XRP(2), USD(1)),
                txflags(tfPassive),
                ter(tecUNFUNDED_OFFER));
            env.close();
            env.require(balance(A1, USD(1001)), offers(A1, 0));

            // test: cannot sell to A3
            env(offer(A1, XRP(1), USD(1)),
                txflags(tfFillOrKill),
                ter(tecUNFUNDED_OFFER));
            env.close();
            env.require(balance(A1, USD(1001)), offers(A1, 0));

            env(trust(G1, A1["USD"](0), tfClearFreeze));
            env.close();
        }

        // Testing aggressive and passive offer placing, trustline deep frozen
        // by the issuer
        if (features[featureDeepFreeze])
        {
            env(trust(G1, A1["USD"](0), tfSetFreeze | tfSetDeepFreeze));
            env.close();

            // test: cannot create passive buy offer
            env(offer(A1, USD(1), XRP(0.5)),
                txflags(tfPassive),
                ter(tecFROZEN));
            env.close();

            // test: cannot buy from A2
            env(offer(A1, USD(1), XRP(2)),
                txflags(tfFillOrKill),
                ter(tecFROZEN));
            env.close();

            // test: cannot create passive sell offer
            env(offer(A1, XRP(2), USD(1)),
                txflags(tfPassive),
                ter(tecUNFUNDED_OFFER));
            env.close();

            // test: cannot sell to A3
            env(offer(A1, XRP(1), USD(1)),
                txflags(tfFillOrKill),
                ter(tecUNFUNDED_OFFER));
            env.close();

            env(trust(G1, A1["USD"](0), tfClearFreeze | tfClearDeepFreeze));
            env.close();
            env.require(balance(A1, USD(1001)), offers(A1, 0));
        }

        // Testing already existing offers behavior after trustline is frozen by
        // the issuer
        {
            env.require(balance(A1, USD(1001)));
            env(offer(A1, XRP(1.9), USD(1)));
            env(offer(A1, USD(1), XRP(1.1)));
            env.close();
            env.require(balance(A1, USD(1001)), offers(A1, 2));

            env(trust(G1, A1["USD"](0), tfSetFreeze));
            env.close();

            // test: A2 wants to sell to A1, must succeed
            env.require(balance(A1, USD(1001)), balance(A2, USD(998)));
            env(offer(A2, XRP(1.1), USD(1)), txflags(tfFillOrKill));
            env.close();
            env.require(
                balance(A1, USD(1002)), balance(A2, USD(997)), offers(A1, 1));

            // test: A3 wants to buy from A1, must fail
            env.require(
                balance(A1, USD(1002)), balance(A3, USD(1)), offers(A1, 1));
            env(offer(A3, USD(1), XRP(1.9)),
                txflags(tfFillOrKill),
                ter(tecKILLED));
            env.close();
            env.require(
                balance(A1, USD(1002)), balance(A3, USD(1)), offers(A1, 0));

            env(trust(G1, A1["USD"](0), tfClearFreeze));
            env.close();
        }

        // Testing existing offers behavior after trustline is deep frozen by
        // the issuer
        if (features[featureDeepFreeze])
        {
            env.require(balance(A1, USD(1002)));
            env(offer(A1, XRP(1.9), USD(1)));
            env(offer(A1, USD(1), XRP(1.1)));
            env.close();
            env.require(balance(A1, USD(1002)), offers(A1, 2));

            env(trust(G1, A1["USD"](0), tfSetFreeze | tfSetDeepFreeze));
            env.close();

            // test: A2 wants to sell to A1, must fail
            env.require(balance(A1, USD(1002)), balance(A2, USD(997)));
            env(offer(A2, XRP(1.1), USD(1)),
                txflags(tfFillOrKill),
                ter(tecKILLED));
            env.close();
            env.require(
                balance(A1, USD(1002)), balance(A2, USD(997)), offers(A1, 1));

            // test: A3 wants to buy from A1, must fail
            env.require(
                balance(A1, USD(1002)), balance(A3, USD(1)), offers(A1, 1));
            env(offer(A3, USD(1), XRP(1.9)),
                txflags(tfFillOrKill),
                ter(tecKILLED));
            env.close();
            env.require(
                balance(A1, USD(1002)), balance(A3, USD(1)), offers(A1, 0));

            env(trust(G1, A1["USD"](0), tfClearFreeze | tfClearDeepFreeze));
            env.close();
        }

        // Testing aggressive and passive offer placing, trustline frozen by
        // the holder
        {
            env(trust(A1, limit, tfSetFreeze));
            env.close();

            // test: A1 can make passive buy offer
            env(offer(A1, USD(1), XRP(0.5)), txflags(tfPassive));
            env.close();
            env.require(balance(A1, USD(1002)), offers(A1, 1));
            //  Cleanup
            env(offer_cancel(A1, env.seq(A1) - 1));
            env.require(offers(A1, 0));
            env.close();

            // test: A1 wants to buy, must fail
            env(offer(A1, USD(1), XRP(2)),
                txflags(tfFillOrKill),
                ter(tecKILLED));
            env.close();
            env.require(
                balance(A1, USD(1002)), balance(A2, USD(997)), offers(A1, 0));

            // test: A1 can create passive sell offer
            env(offer(A1, XRP(2), USD(1)), txflags(tfPassive));
            env.close();
            env.require(balance(A1, USD(1002)), offers(A1, 1));
            // Cleanup
            env(offer_cancel(A1, env.seq(A1) - 1));
            env.require(offers(A1, 0));
            env.close();

            // test: A1 can sell to A3
            env(offer(A1, XRP(1), USD(1)), txflags(tfFillOrKill));
            env.close();
            env.require(balance(A1, USD(1001)), offers(A1, 0));

            env(trust(A1, limit, tfClearFreeze));
            env.close();
        }

        // Testing aggressive and passive offer placing, trustline deep frozen
        // by the holder
        if (features[featureDeepFreeze])
        {
            env(trust(A1, limit, tfSetFreeze | tfSetDeepFreeze));
            env.close();

            // test: A1 cannot create passive buy offer
            env(offer(A1, USD(1), XRP(0.5)),
                txflags(tfPassive),
                ter(tecFROZEN));
            env.close();

            // test: A1 cannot buy, must fail
            env(offer(A1, USD(1), XRP(2)),
                txflags(tfFillOrKill),
                ter(tecFROZEN));
            env.close();

            // test: A1 cannot create passive sell offer
            env(offer(A1, XRP(2), USD(1)),
                txflags(tfPassive),
                ter(tecUNFUNDED_OFFER));
            env.close();

            // test: A1 cannot sell to A3
            env(offer(A1, XRP(1), USD(1)),
                txflags(tfFillOrKill),
                ter(tecUNFUNDED_OFFER));
            env.close();

            env(trust(A1, limit, tfClearFreeze | tfClearDeepFreeze));
            env.close();
        }
    }

    void
    testPathsWhenFrozen(FeatureBitset features)
    {
        testcase("Longer paths payment on frozen trust lines");
        using namespace test::jtx;
        using path = test::jtx::path;

        Env env(*this, features);
        Account G1{"G1"};
        Account A1{"A1"};
        Account A2{"A2"};
        auto const USD{G1["USD"]};

        env.fund(XRP(10000), G1, A1, A2);
        env.close();

        auto const limit = USD(10000);
        env.trust(limit, A1, A2);
        env.close();

        env(pay(G1, A1, USD(1000)));
        env(pay(G1, A2, USD(1000)));
        env.close();

        env(offer(A2, XRP(100), USD(100)), txflags(tfPassive));
        env.close();

        // Testing payments A1 <-> G1 using offer from A2 frozen by issuer.
        {
            env(trust(G1, A2["USD"](0), tfSetFreeze));
            env.close();

            // test: A1 cannot send USD using XRP through A2 offer
            env(pay(A1, G1, USD(10)),
                path(~USD),
                sendmax(XRP(11)),
                txflags(tfNoRippleDirect),
                ter(tecPATH_PARTIAL));
            env.close();

            // test: G1 cannot send USD using XRP through A2 offer
            env(pay(G1, A1, USD(10)),
                path(~USD),
                sendmax(XRP(11)),
                txflags(tfNoRippleDirect),
                ter(tecPATH_PARTIAL));
            env.close();

            env(trust(G1, A2["USD"](0), tfClearFreeze));
            env.close();
        }

        // Testing payments A1 <-> G1 using offer from A2 deep frozen by issuer.
        if (features[featureDeepFreeze])
        {
            env(trust(G1, A2["USD"](0), tfSetFreeze | tfSetDeepFreeze));
            env.close();

            // test: A1 cannot send USD using XRP through A2 offer
            env(pay(A1, G1, USD(10)),
                path(~USD),
                sendmax(XRP(11)),
                txflags(tfNoRippleDirect),
                ter(tecPATH_PARTIAL));
            env.close();

            // test: G1 cannot send USD using XRP through A2 offer
            env(pay(G1, A1, USD(10)),
                path(~USD),
                sendmax(XRP(11)),
                txflags(tfNoRippleDirect),
                ter(tecPATH_PARTIAL));
            env.close();

            env(trust(G1, A2["USD"](0), tfClearFreeze | tfClearDeepFreeze));
            env.close();
        }

        // Testing payments A1 <-> G1 using offer from A2 frozen by currency
        // holder.
        {
            env(trust(A2, limit, tfSetFreeze));
            env.close();

            // test: A1 can send USD using XRP through A2 offer
            env(pay(A1, G1, USD(10)),
                path(~USD),
                sendmax(XRP(11)),
                txflags(tfNoRippleDirect));
            env.close();

            // test: G1 can send USD using XRP through A2 offer
            env(pay(G1, A1, USD(10)),
                path(~USD),
                sendmax(XRP(11)),
                txflags(tfNoRippleDirect));
            env.close();

            env(trust(A2, limit, tfClearFreeze));
            env.close();
        }

        // Testing payments A1 <-> G1 using offer from A2 deep frozen by
        // currency holder.
        if (features[featureDeepFreeze])
        {
            env(trust(A2, limit, tfSetFreeze | tfSetDeepFreeze));
            env.close();

            // test: A1 cannot send USD using XRP through A2 offer
            env(pay(A1, G1, USD(10)),
                path(~USD),
                sendmax(XRP(11)),
                txflags(tfNoRippleDirect),
                ter(tecPATH_PARTIAL));
            env.close();

            // test: G1 cannot send USD using XRP through A2 offer
            env(pay(G1, A1, USD(10)),
                path(~USD),
                sendmax(XRP(11)),
                txflags(tfNoRippleDirect),
                ter(tecPATH_PARTIAL));
            env.close();

            env(trust(A2, limit, tfClearFreeze | tfClearDeepFreeze));
            env.close();
        }

        // Cleanup
        env(offer_cancel(A1, env.seq(A1) - 1));
        env.require(offers(A1, 0));
        env.close();

        env(offer(A2, USD(100), XRP(100)), txflags(tfPassive));
        env.close();

        // Testing payments A1 <-> G1 using offer from A2 frozen by issuer.
        {
            env(trust(G1, A2["USD"](0), tfSetFreeze));
            env.close();

            // test: A1 can send XRP using USD through A2 offer
            env(pay(A1, G1, XRP(10)),
                path(~XRP),
                sendmax(USD(11)),
                txflags(tfNoRippleDirect));
            env.close();

            // test: G1 can send XRP using USD through A2 offer
            env(pay(G1, A1, XRP(10)),
                path(~XRP),
                sendmax(USD(11)),
                txflags(tfNoRippleDirect));
            env.close();

            env(trust(G1, A2["USD"](0), tfClearFreeze));
            env.close();
        }

        // Testing payments A1 <-> G1 using offer from A2 deep frozen by
        // issuer.
        if (features[featureDeepFreeze])
        {
            env(trust(G1, A2["USD"](0), tfSetFreeze | tfSetDeepFreeze));
            env.close();

            // test: A1 cannot send XRP using USD through A2 offer
            env(pay(A1, G1, XRP(10)),
                path(~XRP),
                sendmax(USD(11)),
                txflags(tfNoRippleDirect),
                ter(tecPATH_PARTIAL));
            env.close();

            // test: G1 cannot send XRP using USD through A2 offer
            env(pay(G1, A1, XRP(10)),
                path(~XRP),
                sendmax(USD(11)),
                txflags(tfNoRippleDirect),
                ter(tecPATH_PARTIAL));
            env.close();

            env(trust(G1, A2["USD"](0), tfClearFreeze | tfClearDeepFreeze));
            env.close();
        }

        // Testing payments A1 <-> G1 using offer from A2 frozen by currency
        // holder.
        {
            env(trust(A2, limit, tfSetFreeze));
            env.close();

            // test: A1 can send XRP using USD through A2 offer
            env(pay(A1, G1, XRP(10)),
                path(~XRP),
                sendmax(USD(11)),
                txflags(tfNoRippleDirect));
            env.close();

            // test: G1 can send XRP using USD through A2 offer
            env(pay(G1, A1, XRP(10)),
                path(~XRP),
                sendmax(USD(11)),
                txflags(tfNoRippleDirect));
            env.close();

            env(trust(A2, limit, tfClearFreeze));
            env.close();
        }

        // Testing payments A1 <-> G1 using offer from A2 deep frozen by
        // currency holder.
        if (features[featureDeepFreeze])
        {
            env(trust(A2, limit, tfSetFreeze | tfSetDeepFreeze));
            env.close();

            // test: A1 cannot send XRP using USD through A2 offer
            env(pay(A1, G1, XRP(10)),
                path(~XRP),
                sendmax(USD(11)),
                txflags(tfNoRippleDirect),
                ter(tecPATH_PARTIAL));
            env.close();

            // test: G1 cannot send XRP using USD through A2 offer
            env(pay(G1, A1, XRP(10)),
                path(~XRP),
                sendmax(USD(11)),
                txflags(tfNoRippleDirect),
                ter(tecPATH_PARTIAL));
            env.close();

            env(trust(A2, limit, tfClearFreeze | tfClearDeepFreeze));
            env.close();
        }

        // Cleanup
        env(offer_cancel(A1, env.seq(A1) - 1));
        env.require(offers(A1, 0));
        env.close();
    }

    void
    testPaymentsWhenDeepFrozen(FeatureBitset features)
    {
        testcase("Direct payments on frozen trust lines");

        using namespace test::jtx;
        Env env(*this, features);

        Account G1{"G1"};
        Account A1{"A1"};
        Account A2{"A2"};
        auto const USD{G1["USD"]};

        env.fund(XRP(10000), G1, A1, A2);
        env.close();

        auto const limit = USD(10000);
        env.trust(limit, A1, A2);
        env.close();

        env(pay(G1, A1, USD(1000)));
        env(pay(G1, A2, USD(1000)));
        env.close();

        // Checking payments before freeze
        // To issuer:
        env(pay(A1, G1, USD(1)));
        env(pay(A2, G1, USD(1)));
        env.close();

        // To each other:
        env(pay(A1, A2, USD(1)));
        env(pay(A2, A1, USD(1)));
        env.close();

        // Freeze A1
        env(trust(G1, A1["USD"](0), tfSetFreeze));
        env.close();

        // Issuer and A1 can send payments to each other
        env(pay(A1, G1, USD(1)));
        env(pay(G1, A1, USD(1)));
        env.close();

        // A1 cannot send tokens to A2
        env(pay(A1, A2, USD(1)), ter(tecPATH_DRY));

        // A2 can still send to A1
        env(pay(A2, A1, USD(1)));
        env.close();

        if (features[featureDeepFreeze])
        {
            // Deep freeze A1
            env(trust(G1, A1["USD"](0), tfSetDeepFreeze));
            env.close();

            // Issuer and A1 can send payments to each other
            env(pay(A1, G1, USD(1)));
            env(pay(G1, A1, USD(1)));
            env.close();

            // A1 cannot send tokens to A2
            env(pay(A1, A2, USD(1)), ter(tecPATH_DRY));

            // A2 cannot send tokens to A1
            env(pay(A2, A1, USD(1)), ter(tecPATH_DRY));

            // Clear deep freeze on A1
            env(trust(G1, A1["USD"](0), tfClearDeepFreeze));
            env.close();
        }

        // Clear freeze on A1
        env(trust(G1, A1["USD"](0), tfClearFreeze));
        env.close();

        // A1 freezes trust line
        env(trust(A1, limit, tfSetFreeze));
        env.close();

        // Issuer and A2 must not be affected
        env(pay(A2, G1, USD(1)));
        env(pay(G1, A2, USD(1)));
        env.close();

        // A1 can send tokens to the issuer
        env(pay(A1, G1, USD(1)));
        env.close();
        // A1 can send tokens to A2
        env(pay(A1, A2, USD(1)));
        env.close();

        // Issuer can sent tokens to A1
        env(pay(G1, A1, USD(1)));
        // A2 cannot send tokens to A1
        env(pay(A2, A1, USD(1)), ter(tecPATH_DRY));

        if (features[featureDeepFreeze])
        {
            // A1 deep freezes trust line
            env(trust(A1, limit, tfSetDeepFreeze));
            env.close();

            // Issuer and A2 must not be affected
            env(pay(A2, G1, USD(1)));
            env(pay(G1, A2, USD(1)));
            env.close();

            // A1 can still send token to issuer
            env(pay(A1, G1, USD(1)));
            env.close();

            // Issuer can send tokens to A1
            env(pay(G1, A1, USD(1)));
            // A2 cannot send tokens to A1
            env(pay(A2, A1, USD(1)), ter(tecPATH_DRY));
            // A1 cannot send tokens to A2
            env(pay(A1, A2, USD(1)), ter(tecPATH_DRY));
        }
    }

    void
    testChecksWhenFrozen(FeatureBitset features)
    {
        testcase("Checks on frozen trust lines");

        using namespace test::jtx;
        Env env(*this, features);

        Account G1{"G1"};
        Account A1{"A1"};
        Account A2{"A2"};
        auto const USD{G1["USD"]};

        env.fund(XRP(10000), G1, A1, A2);
        env.close();

        auto const limit = USD(10000);
        env.trust(limit, A1, A2);
        env.close();

        env(pay(G1, A1, USD(1000)));
        env(pay(G1, A2, USD(1000)));
        env.close();

        // Confirming we can write and cash checks
        {
            uint256 const checkId{getCheckIndex(G1, env.seq(G1))};
            env(check::create(G1, A1, USD(10)));
            env.close();
            env(check::cash(A1, checkId, USD(10)));
            env.close();
        }

        {
            uint256 const checkId{getCheckIndex(G1, env.seq(G1))};
            env(check::create(G1, A2, USD(10)));
            env.close();
            env(check::cash(A2, checkId, USD(10)));
            env.close();
        }

        {
            uint256 const checkId{getCheckIndex(A1, env.seq(A1))};
            env(check::create(A1, G1, USD(10)));
            env.close();
            env(check::cash(G1, checkId, USD(10)));
            env.close();
        }

        {
            uint256 const checkId{getCheckIndex(A1, env.seq(A1))};
            env(check::create(A1, A2, USD(10)));
            env.close();
            env(check::cash(A2, checkId, USD(10)));
            env.close();
        }

        {
            uint256 const checkId{getCheckIndex(A2, env.seq(A2))};
            env(check::create(A2, G1, USD(10)));
            env.close();
            env(check::cash(G1, checkId, USD(10)));
            env.close();
        }

        {
            uint256 const checkId{getCheckIndex(A2, env.seq(A2))};
            env(check::create(A2, A1, USD(10)));
            env.close();
            env(check::cash(A1, checkId, USD(10)));
            env.close();
        }

        // Testing creation and cashing of checks on a trustline frozen by
        // issuer
        {
            env(trust(G1, A1["USD"](0), tfSetFreeze));
            env.close();

            // test: issuer writes check to A1.
            {
                uint256 const checkId{getCheckIndex(G1, env.seq(G1))};
                env(check::create(G1, A1, USD(10)));
                env.close();
                env(check::cash(A1, checkId, USD(10)), ter(tecFROZEN));
                env.close();
            }

            // test: A2 writes check to A1.
            {
                uint256 const checkId{getCheckIndex(A2, env.seq(A2))};
                env(check::create(A2, A1, USD(10)));
                env.close();
                // Same as previous test
                env(check::cash(A1, checkId, USD(10)), ter(tecFROZEN));
                env.close();
            }

            // test: A1 writes check to issuer
            {
                env(check::create(A1, G1, USD(10)), ter(tecFROZEN));
                env.close();
            }

            // test: A1 writes check to A2
            {
                // Same as previous test
                env(check::create(A1, A2, USD(10)), ter(tecFROZEN));
                env.close();
            }

            // Unfreeze the trustline to create a couple of checks so that we
            // could try to cash them later when the trustline is frozen again.
            env(trust(G1, A1["USD"](0), tfClearFreeze));
            env.close();

            uint256 const checkId1{getCheckIndex(A1, env.seq(A1))};
            env(check::create(A1, G1, USD(10)));
            env.close();
            uint256 const checkId2{getCheckIndex(A1, env.seq(A1))};
            env(check::create(A1, A2, USD(10)));
            env.close();

            env(trust(G1, A1["USD"](0), tfSetFreeze));
            env.close();

            // test: issuer tries to cash the check from A1
            {
                env(check::cash(G1, checkId1, USD(10)), ter(tecPATH_PARTIAL));
                env.close();
            }

            // test: A2 tries to cash the check from A1
            {
                env(check::cash(A2, checkId2, USD(10)), ter(tecPATH_PARTIAL));
                env.close();
            }

            env(trust(G1, A1["USD"](0), tfClearFreeze));
            env.close();
        }

        // Testing creation and cashing of checks on a trustline deep frozen by
        // issuer
        if (features[featureDeepFreeze])
        {
            env(trust(G1, A1["USD"](0), tfSetFreeze | tfSetDeepFreeze));
            env.close();

            // test: issuer writes check to A1.
            {
                uint256 const checkId{getCheckIndex(G1, env.seq(G1))};
                env(check::create(G1, A1, USD(10)));
                env.close();

                env(check::cash(A1, checkId, USD(10)), ter(tecFROZEN));
                env.close();
            }

            // test: A2 writes check to A1.
            {
                uint256 const checkId{getCheckIndex(A2, env.seq(A2))};
                env(check::create(A2, A1, USD(10)));
                env.close();
                // Same as previous test
                env(check::cash(A1, checkId, USD(10)), ter(tecFROZEN));
                env.close();
            }

            // test: A1 writes check to issuer
            {
                env(check::create(A1, G1, USD(10)), ter(tecFROZEN));
                env.close();
            }

            // test: A1 writes check to A2
            {
                // Same as previous test
                env(check::create(A1, A2, USD(10)), ter(tecFROZEN));
                env.close();
            }

            // Unfreeze the trustline to create a couple of checks so that we
            // could try to cash them later when the trustline is frozen again.
            env(trust(G1, A1["USD"](0), tfClearFreeze | tfClearDeepFreeze));
            env.close();

            uint256 const checkId1{getCheckIndex(A1, env.seq(A1))};
            env(check::create(A1, G1, USD(10)));
            env.close();
            uint256 const checkId2{getCheckIndex(A1, env.seq(A1))};
            env(check::create(A1, A2, USD(10)));
            env.close();

            env(trust(G1, A1["USD"](0), tfSetFreeze | tfSetDeepFreeze));
            env.close();

            // test: issuer tries to cash the check from A1
            {
                env(check::cash(G1, checkId1, USD(10)), ter(tecPATH_PARTIAL));
                env.close();
            }

            // test: A2 tries to cash the check from A1
            {
                env(check::cash(A2, checkId2, USD(10)), ter(tecPATH_PARTIAL));
                env.close();
            }

            env(trust(G1, A1["USD"](0), tfClearFreeze | tfClearDeepFreeze));
            env.close();
        }

        // Testing creation and cashing of checks on a trustline frozen by
        // a currency holder
        {
            env(trust(A1, limit, tfSetFreeze));
            env.close();

            // test: issuer writes check to A1.
            {
                env(check::create(G1, A1, USD(10)), ter(tecFROZEN));
                env.close();
            }

            // test: A2 writes check to A1.
            {
                env(check::create(A2, A1, USD(10)), ter(tecFROZEN));
                env.close();
            }

            // test: A1 writes check to issuer
            {
                uint256 const checkId{getCheckIndex(A1, env.seq(A1))};
                env(check::create(A1, G1, USD(10)));
                env.close();
                env(check::cash(G1, checkId, USD(10)));
                env.close();
            }

            // test: A1 writes check to A2
            {
                uint256 const checkId{getCheckIndex(A1, env.seq(A1))};
                env(check::create(A1, A2, USD(10)));
                env.close();
                env(check::cash(A2, checkId, USD(10)));
                env.close();
            }

            env(trust(A1, limit, tfClearFreeze));
            env.close();
        }

        // Testing creation and cashing of checks on a trustline deep frozen by
        // a currency holder
        if (features[featureDeepFreeze])
        {
            env(trust(A1, limit, tfSetFreeze | tfSetDeepFreeze));
            env.close();

            // test: issuer writes check to A1.
            {
                env(check::create(G1, A1, USD(10)), ter(tecFROZEN));
                env.close();
            }

            // test: A2 writes check to A1.
            {
                env(check::create(A2, A1, USD(10)), ter(tecFROZEN));
                env.close();
            }

            // test: A1 writes check to issuer
            {
                uint256 const checkId{getCheckIndex(A1, env.seq(A1))};
                env(check::create(A1, G1, USD(10)));
                env.close();
                env(check::cash(G1, checkId, USD(10)), ter(tecPATH_PARTIAL));
                env.close();
            }

            // test: A1 writes check to A2
            {
                uint256 const checkId{getCheckIndex(A1, env.seq(A1))};
                env(check::create(A1, A2, USD(10)));
                env.close();
                env(check::cash(A2, checkId, USD(10)), ter(tecPATH_PARTIAL));
                env.close();
            }

            env(trust(A1, limit, tfClearFreeze | tfClearDeepFreeze));
            env.close();
        }
    }

    void
    testAMMWhenFreeze(FeatureBitset features)
    {
        testcase("AMM payments on frozen trust lines");
        using namespace test::jtx;
        using path = test::jtx::path;

        Env env(*this, features);
        Account G1{"G1"};
        Account A1{"A1"};
        Account A2{"A2"};
        auto const USD{G1["USD"]};

        env.fund(XRP(10000), G1, A1, A2);
        env.close();

        env.trust(G1["USD"](10000), A1, A2);
        env.close();

        env(pay(G1, A1, USD(1000)));
        env(pay(G1, A2, USD(1000)));
        env.close();

        AMM ammG1(env, G1, XRP(1'000), USD(1'000));
        env.close();

        // Testing basic payment using AMM when freezing one of the trust lines.
        {
            env(trust(G1, A1["USD"](0), tfSetFreeze));
            env.close();

            // test: can still use XRP to make payment
            env(pay(A1, A2, USD(10)),
                path(~USD),
                sendmax(XRP(11)),
                txflags(tfNoRippleDirect));
            env.close();

            // test: cannot use USD to make payment
            env(pay(A1, A2, XRP(10)),
                path(~XRP),
                sendmax(USD(11)),
                txflags(tfNoRippleDirect),
                ter(tecPATH_DRY));
            env.close();

            // test: can still receive USD payments.
            env(pay(A2, A1, USD(10)),
                path(~USD),
                sendmax(XRP(11)),
                txflags(tfNoRippleDirect));
            env.close();

            // test: can still receive XRP payments.
            env(pay(A2, A1, XRP(10)),
                path(~XRP),
                sendmax(USD(11)),
                txflags(tfNoRippleDirect));
            env.close();

            env(trust(G1, A1["USD"](0), tfClearFreeze));
            env.close();
        }

        // Testing basic payment using AMM when deep freezing one of the trust
        // lines.
        if (features[featureDeepFreeze])
        {
            env(trust(G1, A1["USD"](0), tfSetFreeze | tfSetDeepFreeze));
            env.close();

            // test: can still use XRP to make payment
            env(pay(A1, A2, USD(10)),
                path(~USD),
                sendmax(XRP(11)),
                txflags(tfNoRippleDirect));
            env.close();

            // test: cannot use USD to make payment
            env(pay(A1, A2, XRP(10)),
                path(~XRP),
                sendmax(USD(11)),
                txflags(tfNoRippleDirect),
                ter(tecPATH_DRY));
            env.close();

            // test: cannot receive USD payments.
            env(pay(A2, A1, USD(10)),
                path(~USD),
                sendmax(XRP(11)),
                txflags(tfNoRippleDirect),
                ter(tecPATH_DRY));
            env.close();

            // test: can still receive XRP payments.
            env(pay(A2, A1, XRP(10)),
                path(~XRP),
                sendmax(USD(11)),
                txflags(tfNoRippleDirect));
            env.close();

            env(trust(G1, A1["USD"](0), tfClearFreeze | tfClearDeepFreeze));
            env.close();
        }
    }

    void
    testNFTOffersWhenFreeze(FeatureBitset features)
    {
        testcase("NFT offers on frozen trust lines");
        using namespace test::jtx;

        Env env(*this, features);
        Account G1{"G1"};
        Account A1{"A1"};
        Account A2{"A2"};
        auto const USD{G1["USD"]};

        env.fund(XRP(10000), G1, A1, A2);
        env.close();

        auto const limit = USD(10000);
        env.trust(limit, A1, A2);
        env.close();

        env(pay(G1, A1, USD(1000)));
        env(pay(G1, A2, USD(1000)));
        env.close();

        // Testing A2 nft offer sell when A2 frozen by issuer
        {
            auto const sellOfferIndex = createNFTSellOffer(env, A2, USD(10));
            env(trust(G1, A2["USD"](0), tfSetFreeze));
            env.close();

            // test: A2 can still receive USD for his NFT
            env(token::acceptSellOffer(A1, sellOfferIndex));
            env.close();

            env(trust(G1, A2["USD"](0), tfClearFreeze));
            env.close();
        }

        // Testing A2 nft offer sell when A2 deep frozen by issuer
        if (features[featureDeepFreeze])
        {
            auto const sellOfferIndex = createNFTSellOffer(env, A2, USD(10));

            env(trust(G1, A2["USD"](0), tfSetFreeze | tfSetDeepFreeze));
            env.close();

            // test: A2 cannot receive USD for his NFT
            env(token::acceptSellOffer(A1, sellOfferIndex), ter(tecFROZEN));
            env.close();

            env(trust(G1, A2["USD"](0), tfClearFreeze | tfClearDeepFreeze));
            env.close();
        }

        // Testing A1 nft offer sell when A2 frozen by issuer
        {
            auto const sellOfferIndex = createNFTSellOffer(env, A1, USD(10));
            env(trust(G1, A2["USD"](0), tfSetFreeze));
            env.close();

            // test: A2 cannot send USD for NFT
            env(token::acceptSellOffer(A2, sellOfferIndex),
                ter(tecINSUFFICIENT_FUNDS));
            env.close();

            env(trust(G1, A2["USD"](0), tfClearFreeze));
            env.close();
        }

        // Testing A1 nft offer sell when A2 deep frozen by issuer
        if (features[featureDeepFreeze])
        {
            auto const sellOfferIndex = createNFTSellOffer(env, A1, USD(10));
            env(trust(G1, A2["USD"](0), tfSetFreeze | tfSetDeepFreeze));
            env.close();

            // test: A2 cannot send USD for NFT
            env(token::acceptSellOffer(A2, sellOfferIndex),
                ter(tecINSUFFICIENT_FUNDS));
            env.close();

            env(trust(G1, A2["USD"](0), tfClearFreeze | tfClearDeepFreeze));
            env.close();
        }

        // Testing A1 nft buy offer when A2 deep frozen by issuer
        if (features[featureDeepFreeze] &&
            features[fixEnforceNFTokenTrustlineV2])
        {
            env(trust(G1, A2["USD"](0), tfSetFreeze | tfSetDeepFreeze));
            env.close();

            uint256 const nftID{token::getNextID(env, A2, 0u, tfTransferable)};
            env(token::mint(A2, 0), txflags(tfTransferable));
            env.close();

            auto const buyIdx = keylet::nftoffer(A1, env.seq(A1)).key;
            env(token::createOffer(A1, nftID, USD(10)), token::owner(A2));
            env.close();

            env(token::acceptBuyOffer(A2, buyIdx), ter(tecFROZEN));
            env.close();

            env(trust(G1, A2["USD"](0), tfClearFreeze | tfClearDeepFreeze));
            env.close();

            env(token::acceptBuyOffer(A2, buyIdx));
            env.close();
        }

        // Testing A2 nft offer sell when A2 frozen by currency holder
        {
            auto const sellOfferIndex = createNFTSellOffer(env, A2, USD(10));
            env(trust(A2, limit, tfSetFreeze));
            env.close();

            // test: offer can still be accepted.
            env(token::acceptSellOffer(A1, sellOfferIndex));
            env.close();

            env(trust(A2, limit, tfClearFreeze));
            env.close();
        }

        // Testing A2 nft offer sell when A2 deep frozen by currency holder
        if (features[featureDeepFreeze])
        {
            auto const sellOfferIndex = createNFTSellOffer(env, A2, USD(10));

            env(trust(A2, limit, tfSetFreeze | tfSetDeepFreeze));
            env.close();

            // test: A2 cannot receive USD for his NFT
            env(token::acceptSellOffer(A1, sellOfferIndex), ter(tecFROZEN));
            env.close();

            env(trust(A2, limit, tfClearFreeze | tfClearDeepFreeze));
            env.close();
        }

        // Testing A1 nft offer sell when A2 frozen by currency holder
        {
            auto const sellOfferIndex = createNFTSellOffer(env, A1, USD(10));
            env(trust(A2, limit, tfSetFreeze));
            env.close();

            // test: A2 cannot send USD for NFT
            env(token::acceptSellOffer(A2, sellOfferIndex));
            env.close();

            env(trust(A2, limit, tfClearFreeze));
            env.close();
        }

        // Testing A1 nft offer sell when A2 deep frozen by currency holder
        if (features[featureDeepFreeze])
        {
            auto const sellOfferIndex = createNFTSellOffer(env, A1, USD(10));
            env(trust(A2, limit, tfSetFreeze | tfSetDeepFreeze));
            env.close();

            // test: A2 cannot send USD for NFT
            env(token::acceptSellOffer(A2, sellOfferIndex),
                ter(tecINSUFFICIENT_FUNDS));
            env.close();

            env(trust(A2, limit, tfClearFreeze | tfClearDeepFreeze));
            env.close();
        }

        // Testing brokered offer acceptance
        if (features[featureDeepFreeze] &&
            features[fixEnforceNFTokenTrustlineV2])
        {
            Account broker{"broker"};
            env.fund(XRP(10000), broker);
            env.close();
            env(trust(G1, broker["USD"](1000), tfSetFreeze | tfSetDeepFreeze));
            env.close();

            uint256 const nftID{token::getNextID(env, A2, 0u, tfTransferable)};
            env(token::mint(A2, 0), txflags(tfTransferable));
            env.close();

            uint256 const sellIdx = keylet::nftoffer(A2, env.seq(A2)).key;
            env(token::createOffer(A2, nftID, USD(10)), txflags(tfSellNFToken));
            env.close();
            auto const buyIdx = keylet::nftoffer(A1, env.seq(A1)).key;
            env(token::createOffer(A1, nftID, USD(11)), token::owner(A2));
            env.close();

            env(token::brokerOffers(broker, buyIdx, sellIdx),
                token::brokerFee(USD(1)),
                ter(tecFROZEN));
            env.close();
        }

        // Testing transfer fee
        if (features[featureDeepFreeze] &&
            features[fixEnforceNFTokenTrustlineV2])
        {
            Account minter{"minter"};
            env.fund(XRP(10000), minter);
            env.close();
            env(trust(G1, minter["USD"](1000)));
            env.close();

            uint256 const nftID{
                token::getNextID(env, minter, 0u, tfTransferable, 1u)};
            env(token::mint(minter, 0),
                token::xferFee(1u),
                txflags(tfTransferable));
            env.close();

            uint256 const minterSellIdx =
                keylet::nftoffer(minter, env.seq(minter)).key;
            env(token::createOffer(minter, nftID, drops(1)),
                txflags(tfSellNFToken));
            env.close();
            env(token::acceptSellOffer(A2, minterSellIdx));
            env.close();

            uint256 const sellIdx = keylet::nftoffer(A2, env.seq(A2)).key;
            env(token::createOffer(A2, nftID, USD(100)),
                txflags(tfSellNFToken));
            env.close();
            env(trust(G1, minter["USD"](1000), tfSetFreeze | tfSetDeepFreeze));
            env.close();
            env(token::acceptSellOffer(A1, sellIdx), ter(tecFROZEN));
            env.close();
        }
    }

    // Helper function to extract trustline flags from open ledger
    uint32_t
    getTrustlineFlags(
        test::jtx::Env& env,
        size_t expectedArraySize,
        size_t expectedArrayIndex,
        bool modified = true)
    {
        using namespace test::jtx;
        auto const affected =
            env.meta()->getJson(JsonOptions::none)[sfAffectedNodes.fieldName];
        if (!BEAST_EXPECT(checkArraySize(affected, expectedArraySize)))
            return 0;

        if (modified)
        {
            return affected[expectedArrayIndex][sfModifiedNode.fieldName]
                           [sfFinalFields.fieldName][jss::Flags]
                               .asUInt();
        }

        return affected[expectedArrayIndex][sfCreatedNode.fieldName]
                       [sfNewFields.fieldName][jss::Flags]
                           .asUInt();
    }

    // Helper function that returns the index of the next check on account
    uint256
    getCheckIndex(AccountID const& account, std::uint32_t uSequence)
    {
        return keylet::check(account, uSequence).key;
    }

    uint256
    createNFTSellOffer(
        test::jtx::Env& env,
        test::jtx::Account const& account,
        test::jtx::PrettyAmount const& currency)
    {
        using namespace test::jtx;
        uint256 const nftID{token::getNextID(env, account, 0u, tfTransferable)};
        env(token::mint(account, 0), txflags(tfTransferable));
        env.close();

        uint256 const sellOfferIndex =
            keylet::nftoffer(account, env.seq(account)).key;
        env(token::createOffer(account, nftID, currency),
            txflags(tfSellNFToken));
        env.close();

        return sellOfferIndex;
    }

public:
    void
    run() override
    {
        auto testAll = [this](FeatureBitset features) {
            testRippleState(features);
            testDeepFreeze(features);
            testCreateFrozenTrustline(features);
            testSetAndClear(features);
            testGlobalFreeze(features);
            testNoFreeze(features);
            testOffersWhenFrozen(features);
            testOffersWhenDeepFrozen(features);
            testPaymentsWhenDeepFrozen(features);
            testChecksWhenFrozen(features);
            testAMMWhenFreeze(features);
            testPathsWhenFrozen(features);
            testNFTOffersWhenFreeze(features);
        };
        using namespace test::jtx;
        auto const sa = testable_amendments();
        testAll(
            sa - featureDeepFreeze - featurePermissionedDEX -
            fixEnforceNFTokenTrustlineV2);
        testAll(sa - featurePermissionedDEX - fixEnforceNFTokenTrustlineV2);
        testAll(sa - featureDeepFreeze - featurePermissionedDEX);
        testAll(sa - featurePermissionedDEX);
        testAll(sa - fixEnforceNFTokenTrustlineV2);
        testAll(sa - featureDeepFreeze);
        testAll(sa);
    }
};

BEAST_DEFINE_TESTSUITE(Freeze, app, ripple);
}  // namespace ripple
