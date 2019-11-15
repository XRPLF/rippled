//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <test/jtx.h>
#include <ripple/json/to_string.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/beast/hash/uhash.h>
#include <ripple/beast/unit_test.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/misc/TxQ.h>
#include <boost/lexical_cast.hpp>
#include <boost/optional.hpp>
#include <utility>

namespace ripple {
namespace test {

class Env_test : public beast::unit_test::suite
{
public:
    template <class T>
    static
    std::string
    to_string (T const& t)
    {
        return boost::lexical_cast<
            std::string>(t);
    }

    // Declarations in Account.h
    void
    testAccount()
    {
        using namespace jtx;
        {
            Account a;
            Account b(a);
            a = b;
            a = std::move(b);
            Account c(std::move(a));
        }
        Account("alice");
        Account("alice", KeyType::secp256k1);
        Account("alice", KeyType::ed25519);
        auto const gw = Account("gw");
        [](AccountID){}(gw);
        auto const USD = gw["USD"];
        void(Account("alice") < gw);
        std::set<Account>().emplace(gw);
        std::unordered_set<Account,
            beast::uhash<>>().emplace("alice");
    }

    // Declarations in amount.h
    void
    testAmount()
    {
        using namespace jtx;

        PrettyAmount(0);
        PrettyAmount(1);
        PrettyAmount(0u);
        PrettyAmount(1u);
        PrettyAmount(-1);
        static_assert(! std::is_trivially_constructible<
            PrettyAmount, char>::value, "");
        static_assert(! std::is_trivially_constructible<
            PrettyAmount, unsigned char>::value, "");
        static_assert(! std::is_trivially_constructible<
            PrettyAmount, short>::value, "");
        static_assert(! std::is_trivially_constructible<
            PrettyAmount, unsigned short>::value, "");

        try
        {
            XRP(0.0000001);
            fail("missing exception");
        }
        catch(std::domain_error const&)
        {
            pass();
        }
        XRP(-0.000001);
        try
        {
            XRP(-0.0000009);
            fail("missing exception");
        }
        catch(std::domain_error const&)
        {
            pass();
        }

        BEAST_EXPECT(to_string(XRP(5)) == "5 XRP");
        BEAST_EXPECT(to_string(XRP(.80)) == "0.8 XRP");
        BEAST_EXPECT(to_string(XRP(.005)) == "5000 drops");
        BEAST_EXPECT(to_string(XRP(0.1)) == "0.1 XRP");
        BEAST_EXPECT(to_string(XRP(10000)) == "10000 XRP");
        BEAST_EXPECT(to_string(drops(10)) == "10 drops");
        BEAST_EXPECT(to_string(drops(123400000)) == "123.4 XRP");
        BEAST_EXPECT(to_string(XRP(-5)) == "-5 XRP");
        BEAST_EXPECT(to_string(XRP(-.99)) == "-0.99 XRP");
        BEAST_EXPECT(to_string(XRP(-.005)) == "-5000 drops");
        BEAST_EXPECT(to_string(XRP(-0.1)) == "-0.1 XRP");
        BEAST_EXPECT(to_string(drops(-10)) == "-10 drops");
        BEAST_EXPECT(to_string(drops(-123400000)) == "-123.4 XRP");

        BEAST_EXPECT(XRP(1) == drops(1000000));
        BEAST_EXPECT(XRP(1) == STAmount(1000000));
        BEAST_EXPECT(STAmount(1000000) == XRP(1));

        auto const gw = Account("gw");
        auto const USD = gw["USD"];
        BEAST_EXPECT(to_string(USD(0)) == "0/USD(gw)");
        BEAST_EXPECT(to_string(USD(10)) == "10/USD(gw)");
        BEAST_EXPECT(to_string(USD(-10)) == "-10/USD(gw)");
        BEAST_EXPECT(USD(0) == STAmount(USD, 0));
        BEAST_EXPECT(USD(1) == STAmount(USD, 1));
        BEAST_EXPECT(USD(-1) == STAmount(USD, -1));

        auto const get = [](AnyAmount a){ return a; };
        BEAST_EXPECT(! get(USD(10)).is_any);
        BEAST_EXPECT(get(any(USD(10))).is_any);
    }

    // Test Env
    void
    testEnv()
    {
        using namespace jtx;
        auto const n = XRP(10000);
        auto const gw = Account("gw");
        auto const USD = gw["USD"];
        auto const alice = Account("alice");

        // unfunded
        {
            Env env(*this);
            env(pay("alice", "bob", XRP(1000)), seq(1), fee(10), sig("alice"), ter(terNO_ACCOUNT));
        }

        // fund
        {
            Env env(*this);

            // variadics
            env.fund(n, "alice");
            env.fund(n, "bob", "carol");
            env.fund(n, "dave", noripple("eric"));
            env.fund(n, "fred", noripple("gary", "hank"));
            env.fund(n, noripple("irene"));
            env.fund(n, noripple("jim"), "karen");
            env.fund(n, noripple("lisa", "mary"));

            // flags
            env.fund(n, noripple("xavier"));
            env.require(nflags("xavier", asfDefaultRipple));
            env.fund(n, "yana");
            env.require(flags("yana", asfDefaultRipple));
        }

        // trust
        {
            Env env(*this);
            env.fund(n, "alice", "bob", gw);
            env(trust("alice", USD(100)), require(lines("alice", 1)));
        }

        // balance
        {
            Env env(*this);
            BEAST_EXPECT(env.balance(alice) == 0);
            BEAST_EXPECT(env.balance(alice, USD) != 0);
            BEAST_EXPECT(env.balance(alice, USD) == USD(0));
            env.fund(n, alice, gw);
            BEAST_EXPECT(env.balance(alice) == n);
            BEAST_EXPECT(env.balance(gw) == n);
            env.trust(USD(1000), alice);
            env(pay(gw, alice, USD(10)));
            BEAST_EXPECT(to_string(env.balance("alice", USD)) == "10/USD(gw)");
            BEAST_EXPECT(to_string(env.balance(gw, alice["USD"])) == "-10/USD(alice)");
        }

        // seq
        {
            Env env(*this);
            env.fund(n, noripple("alice", gw));
            BEAST_EXPECT(env.seq("alice") == 3);
            BEAST_EXPECT(env.seq(gw) == 3);
        }

        // autofill
        {
            Env env(*this);
            env.fund(n, "alice");
            env.require(balance("alice", n));
            env(noop("alice"), fee(1),                  ter(telINSUF_FEE_P));
            env(noop("alice"), seq(none),               ter(temMALFORMED));
            env(noop("alice"), seq(none), fee(10),      ter(temMALFORMED));
            env(noop("alice"), fee(none),               ter(temMALFORMED));
            env(noop("alice"), sig(none),               ter(temMALFORMED));
            env(noop("alice"), fee(autofill));
            env(noop("alice"), fee(autofill), seq(autofill));
            env(noop("alice"), fee(autofill), seq(autofill), sig(autofill));
        }
    }

    // Env::require
    void
    testRequire()
    {
        using namespace jtx;
        Env env(*this);
        auto const gw = Account("gw");
        auto const USD = gw["USD"];
        env.require(balance("alice", none));
        env.require(balance("alice", XRP(none)));
        env.fund(XRP(10000), "alice", gw);
        env.require(balance("alice", USD(none)));
        env.trust(USD(100), "alice");
        env.require(balance("alice", XRP(10000))); // fee refunded
        env.require(balance("alice", USD(0)));
        env(pay(gw, "alice", USD(10)), require(balance("alice", USD(10))));

        env.require(nflags("alice", asfRequireDest));
        env(fset("alice", asfRequireDest), require(flags("alice", asfRequireDest)));
        env(fclear("alice", asfRequireDest), require(nflags("alice", asfRequireDest)));
    }

    // Signing with secp256k1 and ed25519 keys
    void
    testKeyType()
    {
        using namespace jtx;

        Env env{*this, supported_amendments() | fixMasterKeyAsRegularKey};
        Account const alice("alice", KeyType::ed25519);
        Account const bob("bob", KeyType::secp256k1);
        Account const carol("carol");
        env.fund(XRP(10000), alice, bob);

        // Master key only
        env(noop(alice));
        env(noop(bob));
        env(noop(alice), sig("alice"),                          ter(tefBAD_AUTH));
        env(noop(alice), sig(Account("alice",
            KeyType::secp256k1)),                               ter(tefBAD_AUTH));
        env(noop(bob), sig(Account("bob",
            KeyType::ed25519)),                                 ter(tefBAD_AUTH));
        env(noop(alice), sig(carol),                            ter(tefBAD_AUTH));

        // Master and Regular key
        env(regkey(alice, bob));
        env(noop(alice));
        env(noop(alice), sig(bob));
        env(noop(alice), sig(alice));

        // Regular key only
        env(fset(alice, asfDisableMaster), sig(alice));
        env(noop(alice));
        env(noop(alice), sig(bob));
        env(noop(alice), sig(alice),                            ter(tefMASTER_DISABLED));
        env(fclear(alice, asfDisableMaster), sig(alice),        ter(tefMASTER_DISABLED));
        env(fclear(alice, asfDisableMaster), sig(bob));
        env(noop(alice), sig(alice));
    }

    // Payment basics
    void
    testPayments()
    {
        using namespace jtx;
        Env env(*this);
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];

        env.fund(XRP(10000), "alice", "bob", "carol", gw);
        env.require(balance("alice", XRP(10000)));
        env.require(balance("bob", XRP(10000)));
        env.require(balance("carol", XRP(10000)));
        env.require(balance(gw, XRP(10000)));

        env(pay(env.master, "alice", XRP(1000)), fee(none),     ter(temMALFORMED));
        env(pay(env.master, "alice", XRP(1000)), fee(1),        ter(telINSUF_FEE_P));
        env(pay(env.master, "alice", XRP(1000)), seq(none),     ter(temMALFORMED));
        env(pay(env.master, "alice", XRP(1000)), seq(20),       ter(terPRE_SEQ));
        env(pay(env.master, "alice", XRP(1000)), sig(none),     ter(temMALFORMED));
        env(pay(env.master, "alice", XRP(1000)), sig("bob"),    ter(tefBAD_AUTH));

        env(pay(env.master, "dilbert", XRP(1000)), sig(env.master));

        env.trust(USD(100), "alice", "bob", "carol");
        env.require(owners("alice", 1), lines("alice", 1));
        env(rate(gw, 1.05));

        env(pay(gw, "carol", USD(50)));
        env.require(balance("carol", USD(50)));
        env.require(balance(gw, Account("carol")["USD"](-50)));

        env(offer("carol", XRP(50), USD(50)), require(owners("carol", 2)));
        env(pay("alice", "bob", any(USD(10))),                  ter(tecPATH_DRY));
        env(pay("alice", "bob", any(USD(10))),
            paths(XRP), sendmax(XRP(10)),                       ter(tecPATH_PARTIAL));
        env(pay("alice", "bob", any(USD(10))), paths(XRP),
            sendmax(XRP(20)));
        env.require(balance("bob", USD(10)));
        env.require(balance("carol", USD(39.5)));

        env.memoize("eric");
        env(regkey("alice", "eric"));
        env(noop("alice"));
        env(noop("alice"), sig("alice"));
        env(noop("alice"), sig("eric"));
        env(noop("alice"), sig("bob"),                          ter(tefBAD_AUTH));
        env(fset("alice", asfDisableMaster),                    ter(tecNEED_MASTER_KEY));
        env(fset("alice", asfDisableMaster), sig("eric"),       ter(tecNEED_MASTER_KEY));
        env.require(nflags("alice", asfDisableMaster));
        env(fset("alice", asfDisableMaster), sig("alice"));
        env.require(flags("alice", asfDisableMaster));
        env(regkey("alice", disabled),                          ter(tecNO_ALTERNATIVE_KEY));
        env(noop("alice"));
        env(noop("alice"), sig("alice"),                        ter(tefMASTER_DISABLED));
        env(noop("alice"), sig("eric"));
        env(noop("alice"), sig("bob"),                          ter(tefBAD_AUTH));
        env(fclear("alice", asfDisableMaster), sig("bob"),      ter(tefBAD_AUTH));
        env(fclear("alice", asfDisableMaster), sig("alice"),    ter(tefMASTER_DISABLED));
        env(fclear("alice", asfDisableMaster));
        env.require(nflags("alice", asfDisableMaster));
        env(regkey("alice", disabled));
        env(noop("alice"), sig("eric"),                         ter(tefBAD_AUTH));
        env(noop("alice"));
    }

    // Rudimentary test to ensure fail_hard
    // transactions are neither queued nor
    // held.
    void
    testFailHard()
    {
        using namespace jtx;
        Env env(*this);
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];

        auto const alice = Account {"alice"};
        env.fund (XRP(10000), alice);

        auto const localTxCnt = env.app().getOPs().getLocalTxCount();
        auto const queueTxCount = env.app().getTxQ().getMetrics(*env.current()).txCount;
        auto const openTxCount = env.current()->txCount();
        BEAST_EXPECT(localTxCnt == 2 && queueTxCount == 0 && openTxCount == 2);

        auto applyTxn = [&env](auto&& ...txnArgs)
        {
            auto jt = env.jt (txnArgs...);
            Serializer s;
            jt.stx->add (s);

            Json::Value args {Json::objectValue};

            args[jss::tx_blob] = strHex (s.slice ());
            args[jss::fail_hard] = true;

            return env.rpc ("json", "submit", args.toStyledString());
        };

        auto jr = applyTxn(noop (alice), fee (1));

        BEAST_EXPECT(jr[jss::result][jss::engine_result] == "telINSUF_FEE_P");
        BEAST_EXPECT(env.app().getTxQ().getMetrics(*env.current()).txCount == queueTxCount);
        BEAST_EXPECT(env.app().getOPs().getLocalTxCount () == localTxCnt);
        BEAST_EXPECT(env.current()->txCount() == openTxCount);

        jr = applyTxn(noop (alice),  sig("bob"));

        BEAST_EXPECT(jr[jss::result][jss::engine_result] == "tefBAD_AUTH");
        BEAST_EXPECT(env.app().getTxQ().getMetrics(*env.current()).txCount == queueTxCount);
        BEAST_EXPECT(env.app().getOPs().getLocalTxCount () == localTxCnt);
        BEAST_EXPECT(env.current()->txCount() == openTxCount);

        jr = applyTxn(noop (alice),  seq(20));

        BEAST_EXPECT(jr[jss::result][jss::engine_result] == "terPRE_SEQ");
        BEAST_EXPECT(env.app().getTxQ().getMetrics(*env.current()).txCount == queueTxCount);
        BEAST_EXPECT(env.app().getOPs().getLocalTxCount () == localTxCnt);
        BEAST_EXPECT(env.current()->txCount() == openTxCount);

        jr = applyTxn(offer(alice, XRP(1000), USD(1000)));

        BEAST_EXPECT(jr[jss::result][jss::engine_result] == "tecUNFUNDED_OFFER");
        BEAST_EXPECT(env.app().getTxQ().getMetrics(*env.current()).txCount == queueTxCount);
        BEAST_EXPECT(env.app().getOPs().getLocalTxCount () == localTxCnt);
        BEAST_EXPECT(env.current()->txCount() == openTxCount);

        jr = applyTxn(noop (alice), fee (drops (-10)));

        BEAST_EXPECT(jr[jss::result][jss::engine_result] == "temBAD_FEE");
        BEAST_EXPECT(env.app().getTxQ().getMetrics(*env.current()).txCount == queueTxCount);
        BEAST_EXPECT(env.app().getOPs().getLocalTxCount () == localTxCnt);
        BEAST_EXPECT(env.current()->txCount() == openTxCount);

        jr = applyTxn(noop (alice));

        BEAST_EXPECT(jr[jss::result][jss::engine_result] == "tesSUCCESS");
        BEAST_EXPECT(env.app().getOPs().getLocalTxCount () == localTxCnt + 1);
        BEAST_EXPECT(env.current()->txCount() == openTxCount + 1);
    }

    // Multi-sign basics
    void
    testMultiSign()
    {
        using namespace jtx;

        Env env(*this);
        env.fund(XRP(10000), "alice");
        env(signers("alice", 1,
            { { "alice", 1 }, { "bob", 2 } }),                  ter(temBAD_SIGNER));
        env(signers("alice", 1,
            { { "bob", 1 }, { "carol", 2 } }));
        env(noop("alice"));

        auto const baseFee = env.current()->fees().base;
        env(noop("alice"), msig("bob"), fee(2 * baseFee));
        env(noop("alice"), msig("carol"), fee(2 * baseFee));
        env(noop("alice"), msig("bob", "carol"), fee(3 * baseFee));
        env(noop("alice"), msig("bob", "carol", "dilbert"),
            fee(4 * baseFee), ter(tefBAD_SIGNATURE));

        env(signers("alice", none));
    }

    void
    testTicket()
    {
        using namespace jtx;
        // create syntax
        ticket::create("alice", "bob");
        ticket::create("alice", 60);
        ticket::create("alice", "bob", 60);
        ticket::create("alice", 60, "bob");

        {
            Env env(*this, supported_amendments().set(featureTickets));
            env.fund(XRP(10000), "alice");
            env(noop("alice"),                  require(owners("alice", 0), tickets("alice", 0)));
            env(ticket::create("alice"),        require(owners("alice", 1), tickets("alice", 1)));
            env(ticket::create("alice"),        require(owners("alice", 2), tickets("alice", 2)));
        }
    }

    struct UDT
    {
    };

    void testJTxProperties()
    {
        struct T { };
        using namespace jtx;
        JTx jt1;
        // Test a straightforward
        // property
        BEAST_EXPECT(!jt1.get<int>());
        jt1.set<int>(7);
        BEAST_EXPECT(jt1.get<int>());
        BEAST_EXPECT(*jt1.get<int>() == 7);
        BEAST_EXPECT(!jt1.get<UDT>());

        // Test that the property is
        // replaced if it exists.
        jt1.set<int>(17);
        BEAST_EXPECT(jt1.get<int>());
        BEAST_EXPECT(*jt1.get<int>() == 17);
        BEAST_EXPECT(!jt1.get<UDT>());

        // Test that modifying the
        // returned prop is saved
        *jt1.get<int>() = 42;
        BEAST_EXPECT(jt1.get<int>());
        BEAST_EXPECT(*jt1.get<int>() == 42);
        BEAST_EXPECT(!jt1.get<UDT>());

        // Test get() const
        auto const& jt2 = jt1;
        BEAST_EXPECT(jt2.get<int>());
        BEAST_EXPECT(*jt2.get<int>() == 42);
        BEAST_EXPECT(!jt2.get<UDT>());
    }

    void testProp()
    {
        using namespace jtx;
        Env env(*this);
        env.fund(XRP(100000), "alice");
        auto jt1 = env.jt(noop("alice"));
        BEAST_EXPECT(!jt1.get<std::uint16_t>());
        auto jt2 = env.jt(noop("alice"),
            prop<std::uint16_t>(-1));
        BEAST_EXPECT(jt2.get<std::uint16_t>());
        BEAST_EXPECT(*jt2.get<std::uint16_t>() ==
            65535);
        auto jt3 = env.jt(noop("alice"),
            prop<std::string>(
                "Hello, world!"),
                    prop<bool>(false));
        BEAST_EXPECT(jt3.get<std::string>());
        BEAST_EXPECT(*jt3.get<std::string>() ==
            "Hello, world!");
        BEAST_EXPECT(jt3.get<bool>());
        BEAST_EXPECT(!*jt3.get<bool>());
    }

    void testJTxCopy()
    {
        struct T { };
        using namespace jtx;
        JTx jt1;
        jt1.set<int>(7);
        BEAST_EXPECT(jt1.get<int>());
        BEAST_EXPECT(*jt1.get<int>() == 7);
        BEAST_EXPECT(!jt1.get<UDT>());
        JTx jt2(jt1);
        BEAST_EXPECT(jt2.get<int>());
        BEAST_EXPECT(*jt2.get<int>() == 7);
        BEAST_EXPECT(!jt2.get<UDT>());
        JTx jt3;
        jt3 = jt1;
        BEAST_EXPECT(jt3.get<int>());
        BEAST_EXPECT(*jt3.get<int>() == 7);
        BEAST_EXPECT(!jt3.get<UDT>());
    }

    void testJTxMove()
    {
        struct T { };
        using namespace jtx;
        JTx jt1;
        jt1.set<int>(7);
        BEAST_EXPECT(jt1.get<int>());
        BEAST_EXPECT(*jt1.get<int>() == 7);
        BEAST_EXPECT(!jt1.get<UDT>());
        JTx jt2(std::move(jt1));
        BEAST_EXPECT(!jt1.get<int>());
        BEAST_EXPECT(!jt1.get<UDT>());
        BEAST_EXPECT(jt2.get<int>());
        BEAST_EXPECT(*jt2.get<int>() == 7);
        BEAST_EXPECT(!jt2.get<UDT>());
        jt1 = std::move(jt2);
        BEAST_EXPECT(!jt2.get<int>());
        BEAST_EXPECT(!jt2.get<UDT>());
        BEAST_EXPECT(jt1.get<int>());
        BEAST_EXPECT(*jt1.get<int>() == 7);
        BEAST_EXPECT(!jt1.get<UDT>());
    }

    void
    testMemo()
    {
        using namespace jtx;
        Env env(*this);
        env.fund(XRP(10000), "alice");
        env(noop("alice"), memodata("data"));
        env(noop("alice"), memoformat("format"));
        env(noop("alice"), memotype("type"));
        env(noop("alice"), memondata("format", "type"));
        env(noop("alice"), memonformat("data", "type"));
        env(noop("alice"), memontype("data", "format"));
        env(noop("alice"), memo("data", "format", "type"));
        env(noop("alice"), memo("data1", "format1", "type1"), memo("data2", "format2", "type2"));
    }

    void
    testAdvance()
    {
        using namespace jtx;
        Env env(*this);
        auto seq = env.current()->seq();
        BEAST_EXPECT(seq == env.closed()->seq() + 1);
        env.close();
        BEAST_EXPECT(env.closed()->seq() == seq);
        BEAST_EXPECT(env.current()->seq() == seq + 1);
        env.close();
        BEAST_EXPECT(env.closed()->seq() == seq + 1);
        BEAST_EXPECT(env.current()->seq() == seq + 2);
    }

    void
    testClose()
    {
        using namespace jtx;
        Env env(*this);
        env.close();
        env.close();
        env.fund(XRP(100000), "alice", "bob");
        env.close();
        env(pay("alice", "bob", XRP(100)));
        env.close();
        env(noop("alice"));
        env.close();
        env(noop("bob"));
    }

    void
    testPath()
    {
        using namespace jtx;
        Env env(*this);
        auto const gw = Account("gw");
        auto const USD = gw["USD"];
        env.fund(XRP(10000), "alice", "bob");
        env.json(
            pay("alice", "bob", USD(10)),
                path(Account("alice")),
                path("bob"),
                path(USD),
                path(~XRP),
                path(~USD),
                path("bob", USD, ~XRP, ~USD)
                );
    }

    // Test that jtx can re-sign a transaction that's already been signed.
    void testResignSigned()
    {
        using namespace jtx;
        Env env(*this);

        env.fund(XRP(10000), "alice");
        auto const baseFee = env.current()->fees().base;
        std::uint32_t const aliceSeq = env.seq ("alice");

        // Sign jsonNoop.
        Json::Value jsonNoop = env.json (
            noop ("alice"), fee(baseFee), seq(aliceSeq), sig("alice"));
        // Re-sign jsonNoop.
        JTx jt = env.jt (jsonNoop);
        env (jt);
    }

    void testSignAndSubmit()
    {
        using namespace jtx;
        Env env(*this);
        Env_ss envs(env);

        auto const alice = Account("alice");
        env.fund(XRP(10000), alice);

        {
            envs(noop(alice), fee(none), seq(none))();

            // Make sure we get the right account back.
            auto tx = env.tx();
            if (BEAST_EXPECT(tx))
            {
                BEAST_EXPECT(tx->getAccountID(sfAccount) == alice.id());
                BEAST_EXPECT(tx->getTxnType() == ttACCOUNT_SET);
            }
        }

        {
            auto params = Json::Value(Json::nullValue);
            envs(noop(alice), fee(none), seq(none))(params);

            // Make sure we get the right account back.
            auto tx = env.tx();
            if (BEAST_EXPECT(tx))
            {
                BEAST_EXPECT(tx->getAccountID(sfAccount) == alice.id());
                BEAST_EXPECT(tx->getTxnType() == ttACCOUNT_SET);
            }
        }

        {
            auto params = Json::Value(Json::objectValue);
            // Force the factor low enough to fail
            params[jss::fee_mult_max] = 1;
            params[jss::fee_div_max] = 2;
            // RPC errors result in temINVALID
            envs(noop(alice), fee(none),
                seq(none), ter(temINVALID))(params);

            auto tx = env.tx();
            BEAST_EXPECT(!tx);
        }
    }

    void testFeatures()
    {
        testcase("Env features");
        using namespace jtx;
        auto const supported = supported_amendments();

        // this finds a feature that is not in
        // the supported amendments list and tests that it can be
        // enabled explicitly

        auto const neverSupportedFeat = [&]() -> boost::optional<uint256>
        {
            auto const n = supported.size();
            for(size_t i = 0; i < n; ++i)
                if (!supported[i])
                    return bitsetIndexToFeature(i);

            return boost::none;
        }();

        if (!neverSupportedFeat)
        {
            log << "No unsupported features found - skipping test." << std::endl;
            pass();
            return;
        }

        auto hasFeature = [](Env& env, uint256 const& f)
        {
            return (env.app().config().features.find (f) !=
                    env.app().config().features.end());
        };

        {
            // default Env has all supported features
            Env env{*this};
            BEAST_EXPECT(
                supported.count() == env.app().config().features.size());
            foreachFeature(supported, [&](uint256 const& f) {
                this->BEAST_EXPECT(hasFeature(env, f));
            });
        }

        {
            // a Env FeatureBitset has *only* those features
            Env env{*this, FeatureBitset(featureEscrow, featureFlow)};
            BEAST_EXPECT(env.app().config().features.size() == 2);
            foreachFeature(supported, [&](uint256 const& f) {
                bool const has = (f == featureEscrow || f == featureFlow);
                this->BEAST_EXPECT(has == hasFeature(env, f));
            });
        }

        auto const noFlowOrEscrow =
            supported_amendments() - featureEscrow - featureFlow;
        {
            // a Env supported_features_except is missing *only* those features
            Env env{*this, noFlowOrEscrow};
            BEAST_EXPECT(
                env.app().config().features.size() == (supported.count() - 2));
            foreachFeature(supported, [&](uint256 const& f) {
                bool hasnot = (f == featureEscrow || f == featureFlow);
                this->BEAST_EXPECT(hasnot != hasFeature(env, f));
            });
        }

        {
            // add a feature that is NOT in the supported amendments list
            // along with a list of explicit amendments
            // the unsupported feature should be enabled along with
            // the two supported ones
            Env env{
                *this,
                FeatureBitset(featureEscrow, featureFlow, *neverSupportedFeat)};

            // this app will have just 2 supported amendments and
            // one additional never supported feature flag
            BEAST_EXPECT(env.app().config().features.size() == (2 + 1));
            BEAST_EXPECT(hasFeature(env, *neverSupportedFeat));

            foreachFeature(supported, [&](uint256 const& f) {
                bool has = (f == featureEscrow || f == featureFlow);
                this->BEAST_EXPECT(has == hasFeature(env, f));
            });
        }

        {
            // add a feature that is NOT in the supported amendments list
            // and omit a few standard amendments
            // the unsupported features should be enabled
            Env env{*this,
                    noFlowOrEscrow | FeatureBitset{*neverSupportedFeat}};

            // this app will have all supported amendments minus 2 and then the
            // one additional never supported feature flag
            BEAST_EXPECT(
                env.app().config().features.size() ==
                (supported.count() - 2 + 1));
            BEAST_EXPECT(hasFeature(env, *neverSupportedFeat));
            foreachFeature(supported, [&](uint256 const& f) {
                bool hasnot = (f == featureEscrow || f == featureFlow);
                this->BEAST_EXPECT(hasnot != hasFeature(env, f));
            });
        }

        {
            // add a feature that is NOT in the supported amendments list
            // along with all supported amendments
            // the unsupported features should be enabled
            Env env{*this, supported_amendments().set(*neverSupportedFeat)};

            // this app will have all supported amendments and then the
            // one additional never supported feature flag
            BEAST_EXPECT(
                env.app().config().features.size() == (supported.count() + 1));
            BEAST_EXPECT(hasFeature(env, *neverSupportedFeat));
            foreachFeature(supported, [&](uint256 const& f) {
                this->BEAST_EXPECT(hasFeature(env, f));
            });
        }
    }

    void testExceptionalShutdown()
    {
        except(
            [this]
            {
                jtx::Env env {*this,
                              jtx::envconfig([](std::unique_ptr<Config> cfg)
                              {
                                  (*cfg).deprecatedClearSection("port_rpc");
                                  return cfg;
                              })};
            }
        );
        pass();
    }

    void
    run() override
    {
        testAccount();
        testAmount();
        testEnv();
        testRequire();
        testKeyType();
        testPayments();
        testFailHard();
        testMultiSign();
        testTicket();
        testJTxProperties();
        testProp();
        testJTxCopy();
        testJTxMove();
        testMemo();
        testAdvance();
        testClose();
        testPath();
        testResignSigned();
        testSignAndSubmit();
        testFeatures();
        testExceptionalShutdown();
    }
};

BEAST_DEFINE_TESTSUITE(Env,app,ripple);

} // test
} // ripple
