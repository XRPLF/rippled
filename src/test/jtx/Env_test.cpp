
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/Env_ss.h>
#include <test/jtx/JTx.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/memo.h>
#include <test/jtx/multisign.h>
#include <test/jtx/noop.h>
#include <test/jtx/offer.h>
#include <test/jtx/owners.h>
#include <test/jtx/paths.h>
#include <test/jtx/pay.h>
#include <test/jtx/prop.h>
#include <test/jtx/rate.h>
#include <test/jtx/regkey.h>
#include <test/jtx/require.h>
#include <test/jtx/rpc.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/seq.h>
#include <test/jtx/sig.h>
#include <test/jtx/tags.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>
#include <test/jtx/trust.h>

#include <xrpld/app/misc/TxQ.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/Constants.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>

#include <boost/lexical_cast.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace xrpl::test {

class Env_test : public beast::unit_test::Suite
{
public:
    template <class T>
    static std::string
    toString(T const& t)
    {
        return boost::lexical_cast<std::string>(t);
    }

    // Declarations in Account.h
    static void
    testAccount()
    {
        using namespace jtx;
        {
            Account a("chad");
            Account b(a);
            a = b;
            a = std::move(b);
            Account const c(std::move(a));
        }
        Account("alice");                      // NOLINT(bugprone-unused-raii)
        Account("alice", KeyType::Secp256k1);  // NOLINT(bugprone-unused-raii)
        Account("alice", KeyType::Ed25519);    // NOLINT(bugprone-unused-raii)
        auto const gw = Account("gw");
        [](AccountID) {}(gw);
        auto const usd = gw["USD"];
        void(Account("alice") < gw);
        std::set<Account>().emplace(gw);
        std::unordered_set<Account, beast::Uhash<>>().emplace("alice");
    }

    // Declarations in amount.h
    void
    testAmount()
    {
        using namespace jtx;

        PrettyAmount(0);   // NOLINT(bugprone-unused-raii)
        PrettyAmount(1);   // NOLINT(bugprone-unused-raii)
        PrettyAmount(0u);  // NOLINT(bugprone-unused-raii)
        PrettyAmount(1u);  // NOLINT(bugprone-unused-raii)
        PrettyAmount(-1);  // NOLINT(bugprone-unused-raii)
        static_assert(!std::is_trivially_constructible_v<PrettyAmount, char>, "");
        static_assert(!std::is_trivially_constructible_v<PrettyAmount, unsigned char>, "");
        static_assert(!std::is_trivially_constructible_v<PrettyAmount, short>, "");
        static_assert(!std::is_trivially_constructible_v<PrettyAmount, unsigned short>, "");

        try
        {
            XRP(0.0000001);
            fail("missing exception");
        }
        catch (std::domain_error const&)
        {
            pass();
        }
        XRP(-0.000001);
        try
        {
            XRP(-0.0000009);
            fail("missing exception");
        }
        catch (std::domain_error const&)
        {
            pass();
        }

        BEAST_EXPECT(toString(XRP(5)) == "5 XRP");
        BEAST_EXPECT(toString(XRP(.80)) == "0.8 XRP");
        BEAST_EXPECT(toString(XRP(.005)) == "5000 drops");
        BEAST_EXPECT(toString(XRP(0.1)) == "0.1 XRP");
        BEAST_EXPECT(toString(XRP(10000)) == "10000 XRP");
        BEAST_EXPECT(toString(drops(10)) == "10 drops");
        BEAST_EXPECT(toString(drops(123400000)) == "123.4 XRP");
        BEAST_EXPECT(toString(XRP(-5)) == "-5 XRP");
        BEAST_EXPECT(toString(XRP(-.99)) == "-0.99 XRP");
        BEAST_EXPECT(toString(XRP(-.005)) == "-5000 drops");
        BEAST_EXPECT(toString(XRP(-0.1)) == "-0.1 XRP");
        BEAST_EXPECT(toString(drops(-10)) == "-10 drops");
        BEAST_EXPECT(toString(drops(-123400000)) == "-123.4 XRP");

        BEAST_EXPECT(XRP(1) == drops(1000000));
        BEAST_EXPECT(XRP(1) == STAmount(1000000));
        BEAST_EXPECT(STAmount(1000000) == XRP(1));

        auto const gw = Account("gw");
        auto const usd = gw["USD"];
        BEAST_EXPECT(toString(usd(0)) == "0/USD(gw)");
        BEAST_EXPECT(toString(usd(10)) == "10/USD(gw)");
        BEAST_EXPECT(toString(usd(-10)) == "-10/USD(gw)");
        BEAST_EXPECT(usd(0) == STAmount(usd, 0));
        BEAST_EXPECT(usd(1) == STAmount(usd, 1));
        BEAST_EXPECT(usd(-1) == STAmount(usd, -1));

        auto const get = [](AnyAmount a) { return a; };
        BEAST_EXPECT(!get(usd(10)).isAny);
        BEAST_EXPECT(get(kAny(usd(10))).isAny);
    }

    // Test Env
    void
    testEnv()
    {
        using namespace jtx;
        auto const n = XRP(10000);
        auto const gw = Account("gw");
        auto const usd = gw["USD"];
        auto const alice = Account("alice");

        // unfunded
        {
            Env env(*this);
            env(pay("alice", "bob", XRP(1000)), Seq(1), Fee(10), Sig("alice"), Ter(terNO_ACCOUNT));
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
            env.require(Nflags("xavier", asfDefaultRipple));
            env.fund(n, "zachary");
            env.require(Flags("zachary", asfDefaultRipple));
        }

        // trust
        {
            Env env(*this);
            env.fund(n, "alice", "bob", gw);
            env.close();
            env(trust("alice", usd(100)), Require(lines("alice", 1)));
        }

        // balance
        {
            Env env(*this);
            BEAST_EXPECT(env.balance(alice) == 0);
            BEAST_EXPECT(env.balance(alice, usd) != 0);
            BEAST_EXPECT(env.balance(alice, usd) == usd(0));
            env.fund(n, alice, gw);
            env.close();
            BEAST_EXPECT(env.balance(alice) == n);
            BEAST_EXPECT(env.balance(gw) == n);
            env.trust(usd(1000), alice);
            env(pay(gw, alice, usd(10)));
            BEAST_EXPECT(toString(env.balance("alice", usd)) == "10/USD(gw)");
            BEAST_EXPECT(toString(env.balance(gw, alice["USD"])) == "-10/USD(alice)");
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
            env.require(Balance("alice", n));
            env(noop("alice"), Fee(1), Ter(telINSUF_FEE_P));
            env(noop("alice"), Seq(kNone), Ter(temMALFORMED));
            env(noop("alice"), Seq(kNone), Fee(10), Ter(temMALFORMED));
            env(noop("alice"), Fee(kNone), Ter(temMALFORMED));
            env(noop("alice"), Sig(kNone), Ter(temMALFORMED));
            env(noop("alice"), Fee(kAutofill));
            env(noop("alice"), Fee(kAutofill), Seq(kAutofill));
            env(noop("alice"), Fee(kAutofill), Seq(kAutofill), Sig(kAutofill));
        }
    }

    // Env::require
    void
    testRequire()
    {
        using namespace jtx;
        Env env(*this);
        auto const gw = Account("gw");
        auto const usd = gw["USD"];
        env.require(Balance("alice", kNone));
        env.require(Balance("alice", XRP(kNone)));
        env.fund(XRP(10000), "alice", gw);
        env.close();
        env.require(Balance("alice", usd(kNone)));
        env.trust(usd(100), "alice");
        env.require(Balance("alice", XRP(10000)));  // fee refunded
        env.require(Balance("alice", usd(0)));
        env(pay(gw, "alice", usd(10)), Require(Balance("alice", usd(10))));

        env.require(Nflags("alice", asfRequireDest));
        env(fset("alice", asfRequireDest), Require(Flags("alice", asfRequireDest)));
        env(fclear("alice", asfRequireDest), Require(Nflags("alice", asfRequireDest)));
    }

    // Signing with secp256k1 and ed25519 keys
    void
    testKeyType()
    {
        using namespace jtx;

        Env env{*this, testableAmendments()};
        Account const alice("alice", KeyType::Ed25519);
        Account const bob("bob", KeyType::Secp256k1);
        Account const carol("carol");
        env.fund(XRP(10000), alice, bob);

        // Master key only
        env(noop(alice));
        env(noop(bob));
        env(noop(alice), Sig("alice"), Ter(tefBAD_AUTH));
        env(noop(alice), Sig(Account("alice", KeyType::Secp256k1)), Ter(tefBAD_AUTH));
        env(noop(bob), Sig(Account("bob", KeyType::Ed25519)), Ter(tefBAD_AUTH));
        env(noop(alice), Sig(carol), Ter(tefBAD_AUTH));

        // Master and Regular key
        env(regkey(alice, bob));
        env(noop(alice));
        env(noop(alice), Sig(bob));
        env(noop(alice), Sig(alice));

        // Regular key only
        env(fset(alice, asfDisableMaster), Sig(alice));
        env(noop(alice));
        env(noop(alice), Sig(bob));
        env(noop(alice), Sig(alice), Ter(tefMASTER_DISABLED));
        env(fclear(alice, asfDisableMaster), Sig(alice), Ter(tefMASTER_DISABLED));
        env(fclear(alice, asfDisableMaster), Sig(bob));
        env(noop(alice), Sig(alice));
    }

    // Payment basics
    void
    testPayments()
    {
        using namespace jtx;
        Env env(*this);
        auto const gw = Account("gateway");
        auto const usd = gw["USD"];

        env.fund(XRP(10000), "alice", "bob", "carol", gw);
        env.require(Balance("alice", XRP(10000)));
        env.require(Balance("bob", XRP(10000)));
        env.require(Balance("carol", XRP(10000)));
        env.require(Balance(gw, XRP(10000)));

        env(pay(env.master, "alice", XRP(1000)), Fee(kNone), Ter(temMALFORMED));
        env(pay(env.master, "alice", XRP(1000)), Fee(1), Ter(telINSUF_FEE_P));
        env(pay(env.master, "alice", XRP(1000)), Seq(kNone), Ter(temMALFORMED));
        env(pay(env.master, "alice", XRP(1000)), Seq(20), Ter(terPRE_SEQ));
        env(pay(env.master, "alice", XRP(1000)), Sig(kNone), Ter(temMALFORMED));
        env(pay(env.master, "alice", XRP(1000)), Sig("bob"), Ter(tefBAD_AUTH));

        env(pay(env.master, "dilbert", XRP(1000)), Sig(env.master));

        env.trust(usd(100), "alice", "bob", "carol");
        env.require(Owners("alice", 1), lines("alice", 1));
        env(rate(gw, 1.05));

        env(pay(gw, "carol", usd(50)));
        env.require(Balance("carol", usd(50)));
        env.require(Balance(gw, Account("carol")["USD"](-50)));

        env(offer("carol", XRP(50), usd(50)), Require(Owners("carol", 2)));
        env(pay("alice", "bob", kAny(usd(10))), Ter(tecPATH_DRY));
        env(pay("alice", "bob", kAny(usd(10))), Paths(XRP), Sendmax(XRP(10)), Ter(tecPATH_PARTIAL));
        env(pay("alice", "bob", kAny(usd(10))), Paths(XRP), Sendmax(XRP(20)));
        env.require(Balance("bob", usd(10)));
        env.require(Balance("carol", usd(39.5)));

        env.memoize("eric");
        env(regkey("alice", "eric"));
        env(noop("alice"));
        env(noop("alice"), Sig("alice"));
        env(noop("alice"), Sig("eric"));
        env(noop("alice"), Sig("bob"), Ter(tefBAD_AUTH));
        env(fset("alice", asfDisableMaster), Ter(tecNEED_MASTER_KEY));
        env(fset("alice", asfDisableMaster), Sig("eric"), Ter(tecNEED_MASTER_KEY));
        env.require(Nflags("alice", asfDisableMaster));
        env(fset("alice", asfDisableMaster), Sig("alice"));
        env.require(Flags("alice", asfDisableMaster));
        env(regkey("alice", kDisabled), Ter(tecNO_ALTERNATIVE_KEY));
        env(noop("alice"));
        env(noop("alice"), Sig("alice"), Ter(tefMASTER_DISABLED));
        env(noop("alice"), Sig("eric"));
        env(noop("alice"), Sig("bob"), Ter(tefBAD_AUTH));
        env(fclear("alice", asfDisableMaster), Sig("bob"), Ter(tefBAD_AUTH));
        env(fclear("alice", asfDisableMaster), Sig("alice"), Ter(tefMASTER_DISABLED));
        env(fclear("alice", asfDisableMaster));
        env.require(Nflags("alice", asfDisableMaster));
        env(regkey("alice", kDisabled));
        env(noop("alice"), Sig("eric"), Ter(tefBAD_AUTH));
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
        auto const usd = gw["USD"];

        auto const alice = Account{"alice"};
        env.fund(XRP(10000), alice);

        auto const localTxCnt = env.app().getOPs().getLocalTxCount();
        auto const queueTxCount = env.app().getTxQ().getMetrics(*env.current()).txCount;
        auto const openTxCount = env.current()->txCount();
        BEAST_EXPECT(localTxCnt == 2 && queueTxCount == 0 && openTxCount == 2);

        auto applyTxn = [&env](auto&&... txnArgs) {
            auto jt = env.jt(txnArgs...);
            Serializer s;
            jt.stx->add(s);

            json::Value args{json::ValueType::Object};

            args[jss::tx_blob] = strHex(s.slice());
            args[jss::fail_hard] = true;

            return env.rpc("json", "submit", args.toStyledString());
        };

        auto jr = applyTxn(noop(alice), Fee(1));

        BEAST_EXPECT(jr[jss::result][jss::engine_result] == "telINSUF_FEE_P");
        BEAST_EXPECT(env.app().getTxQ().getMetrics(*env.current()).txCount == queueTxCount);
        BEAST_EXPECT(env.app().getOPs().getLocalTxCount() == localTxCnt);
        BEAST_EXPECT(env.current()->txCount() == openTxCount);

        jr = applyTxn(noop(alice), Sig("bob"));

        BEAST_EXPECT(jr[jss::result][jss::engine_result] == "tefBAD_AUTH");
        BEAST_EXPECT(env.app().getTxQ().getMetrics(*env.current()).txCount == queueTxCount);
        BEAST_EXPECT(env.app().getOPs().getLocalTxCount() == localTxCnt);
        BEAST_EXPECT(env.current()->txCount() == openTxCount);

        jr = applyTxn(noop(alice), Seq(20));

        BEAST_EXPECT(jr[jss::result][jss::engine_result] == "terPRE_SEQ");
        BEAST_EXPECT(env.app().getTxQ().getMetrics(*env.current()).txCount == queueTxCount);
        BEAST_EXPECT(env.app().getOPs().getLocalTxCount() == localTxCnt);
        BEAST_EXPECT(env.current()->txCount() == openTxCount);

        jr = applyTxn(offer(alice, XRP(1000), usd(1000)));

        BEAST_EXPECT(jr[jss::result][jss::engine_result] == "tecUNFUNDED_OFFER");
        BEAST_EXPECT(env.app().getTxQ().getMetrics(*env.current()).txCount == queueTxCount);
        BEAST_EXPECT(env.app().getOPs().getLocalTxCount() == localTxCnt);
        BEAST_EXPECT(env.current()->txCount() == openTxCount);

        jr = applyTxn(noop(alice), Fee(drops(-10)));

        BEAST_EXPECT(jr[jss::result][jss::engine_result] == "temBAD_FEE");
        BEAST_EXPECT(env.app().getTxQ().getMetrics(*env.current()).txCount == queueTxCount);
        BEAST_EXPECT(env.app().getOPs().getLocalTxCount() == localTxCnt);
        BEAST_EXPECT(env.current()->txCount() == openTxCount);

        jr = applyTxn(noop(alice));

        BEAST_EXPECT(jr[jss::result][jss::engine_result] == "tesSUCCESS");
        BEAST_EXPECT(env.app().getOPs().getLocalTxCount() == localTxCnt + 1);
        BEAST_EXPECT(env.current()->txCount() == openTxCount + 1);
    }

    // Multi-sign basics
    void
    testMultiSign()
    {
        using namespace jtx;

        Env env(*this);
        env.fund(XRP(10000), "alice");
        env(signers("alice", 1, {{"alice", 1}, {"bob", 2}}), Ter(temBAD_SIGNER));
        env(signers("alice", 1, {{"bob", 1}, {"carol", 2}}));
        env(noop("alice"));

        auto const baseFee = env.current()->fees().base;
        env(noop("alice"), Msig("bob"), Fee(2 * baseFee));
        env(noop("alice"), Msig("carol"), Fee(2 * baseFee));
        env(noop("alice"), Msig("bob", "carol"), Fee(3 * baseFee));
        env(noop("alice"),
            Msig("bob", "carol", "dilbert"),
            Fee(4 * baseFee),
            Ter(tefBAD_SIGNATURE));

        env(signers("alice", kNone));
    }

    void
    testTicket()
    {
        using namespace jtx;
        // create syntax
        ticket::create("alice", 1);

        {
            Env env(*this);
            env.fund(XRP(10000), "alice");
            env(noop("alice"), Require(Owners("alice", 0), tickets("alice", 0)));
            env(ticket::create("alice", 1), Require(Owners("alice", 1), tickets("alice", 1)));
        }
    }

    struct UDT
    {
    };

    void
    testJTxProperties()
    {
        struct T
        {
        };
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

    void
    testProp()
    {
        using namespace jtx;
        Env env(*this);
        env.fund(XRP(100000), "alice");
        auto jt1 = env.jt(noop("alice"));
        BEAST_EXPECT(!jt1.get<std::uint16_t>());
        auto jt2 = env.jt(noop("alice"), Prop<std::uint16_t>(-1));
        BEAST_EXPECT(jt2.get<std::uint16_t>());
        BEAST_EXPECT(*jt2.get<std::uint16_t>() == 65535);
        auto jt3 = env.jt(noop("alice"), Prop<std::string>("Hello, world!"), Prop<bool>(false));
        BEAST_EXPECT(jt3.get<std::string>());
        BEAST_EXPECT(*jt3.get<std::string>() == "Hello, world!");
        BEAST_EXPECT(jt3.get<bool>());
        BEAST_EXPECT(!*jt3.get<bool>());
    }

    void
    testJTxCopy()
    {
        struct T
        {
        };
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

    void
    testJTxMove()
    {
        struct T
        {
        };
        using namespace jtx;
        JTx jt1;
        jt1.set<int>(7);
        BEAST_EXPECT(jt1.get<int>());
        BEAST_EXPECT(*jt1.get<int>() == 7);
        BEAST_EXPECT(!jt1.get<UDT>());
        JTx jt2(std::move(jt1));
        BEAST_EXPECT(!jt1.get<int>());  // NOLINT(bugprone-use-after-move)
        BEAST_EXPECT(!jt1.get<UDT>());  // NOLINT(bugprone-use-after-move)
        BEAST_EXPECT(jt2.get<int>());
        BEAST_EXPECT(*jt2.get<int>() == 7);
        BEAST_EXPECT(!jt2.get<UDT>());
        jt1 = std::move(jt2);
        BEAST_EXPECT(!jt2.get<int>());  // NOLINT(bugprone-use-after-move)
        BEAST_EXPECT(!jt2.get<UDT>());  // NOLINT(bugprone-use-after-move)
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
        env(noop("alice"), MemoData("data"));
        env(noop("alice"), MemoFormat("format"));
        env(noop("alice"), MemoType("type"));
        env(noop("alice"), Memo("data", "format", "type"));
        env(noop("alice"), Memo("data1", "format1", "type1"), Memo("data2", "format2", "type2"));
    }

    void
    testMemoResult()
    {
        using namespace jtx;
        Env env(*this);
        JTx jt(noop("alice"));
        Memo("data", "format", "type")(env, jt);

        auto const& memo = jt.jv["Memos"][0u]["Memo"];
        BEAST_EXPECT(memo["MemoData"].asString() == strHex(std::string("data")));
        BEAST_EXPECT(memo["MemoFormat"].asString() == strHex(std::string("format")));
        BEAST_EXPECT(memo["MemoType"].asString() == strHex(std::string("type")));
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
        auto const usd = gw["USD"];
        env.fund(XRP(10000), "alice", "bob");
        env.close();
        env.json(
            pay("alice", "bob", usd(10)),
            Path(Account("alice")),
            Path("bob"),
            Path(usd),
            Path(~XRP),
            Path(~usd),
            Path("bob", usd, ~XRP, ~usd));
    }

    // Test that jtx can re-sign a transaction that's already been signed.
    void
    testResignSigned()
    {
        using namespace jtx;
        Env env(*this);

        env.fund(XRP(10000), "alice");
        auto const baseFee = env.current()->fees().base;
        std::uint32_t const aliceSeq = env.seq("alice");

        // Sign jsonNoOp.
        json::Value const jsonNoOp =
            env.json(noop("alice"), Fee(baseFee), Seq(aliceSeq), Sig("alice"));
        // Re-sign jsonNoOp.
        JTx const jt = env.jt(jsonNoOp);
        env(jt);
    }

    void
    testSignAndSubmit()
    {
        using namespace jtx;
        Env env(*this);
        EnvSs envs(env);

        auto const baseFee = env.current()->fees().base;

        auto const alice = Account("alice");
        env.fund(XRP(10000), alice);

        {
            envs(noop(alice), Fee(kNone), Seq(kNone))();

            // Make sure we get the right account back.
            auto tx = env.tx();
            if (BEAST_EXPECT(tx))
            {
                BEAST_EXPECT(tx->getAccountID(sfAccount) == alice.id());
                BEAST_EXPECT(tx->getTxnType() == ttACCOUNT_SET);
            }
        }

        {
            auto params = json::Value(json::ValueType::Null);
            envs(noop(alice), Fee(kNone), Seq(kNone))(params);

            // Make sure we get the right account back.
            auto tx = env.tx();
            if (BEAST_EXPECT(tx))
            {
                BEAST_EXPECT(tx->getAccountID(sfAccount) == alice.id());
                BEAST_EXPECT(tx->getTxnType() == ttACCOUNT_SET);
            }
        }

        {
            auto params = json::Value(json::ValueType::Object);
            // Force the factor low enough to fail
            params[jss::fee_mult_max] = 1;
            params[jss::fee_div_max] = 2;

            auto const expectedErrorString = "Fee of " + std::to_string(baseFee.drops()) +
                " exceeds the requested tx limit of " + std::to_string(baseFee.drops() / 2);
            envs(noop(alice), Fee(kNone), Seq(kNone), Rpc(RpcHighFee, expectedErrorString))(params);

            auto tx = env.tx();
            BEAST_EXPECT(!tx);
        }
    }

    void
    testFeatures()
    {
        testcase("Env features");
        using namespace jtx;
        auto const supported = testableAmendments();

        // this finds a feature that is not in
        // the supported amendments list and tests that it can be
        // enabled explicitly

        auto const neverSupportedFeat = [&]() -> std::optional<uint256> {
            auto const n = supported.size();
            for (size_t i = 0; i < n; ++i)
            {
                if (!supported[i])
                    return bitsetIndexToFeature(i);
            }

            return std::nullopt;
        }();

        if (!neverSupportedFeat)
        {
            log << "No unsupported features found - skipping test." << std::endl;
            pass();
            return;
        }

        auto hasFeature = [](Env& env, uint256 const& f) {
            return (env.app().config().features.contains(f));
        };

        {
            // default Env has all supported features
            Env env{*this};
            BEAST_EXPECT(supported.count() == env.app().config().features.size());
            foreachFeature(
                supported, [&](uint256 const& f) { this->BEAST_EXPECT(hasFeature(env, f)); });
        }

        {
            // a Env FeatureBitset has *only* those features
            Env env{*this, FeatureBitset{featureDynamicMPT | featureTokenEscrow}};
            BEAST_EXPECT(env.app().config().features.size() == 2);
            foreachFeature(supported, [&](uint256 const& f) {
                bool const has = (f == featureDynamicMPT || f == featureTokenEscrow);
                this->BEAST_EXPECT(has == hasFeature(env, f));
            });
        }

        auto const missingSomeFeatures =
            testableAmendments() - featureDynamicMPT - featureTokenEscrow;
        BEAST_EXPECT(missingSomeFeatures.count() == (supported.count() - 2));
        {
            // a Env supported_features_except is missing *only* those features
            Env env{*this, missingSomeFeatures};
            BEAST_EXPECT(env.app().config().features.size() == (supported.count() - 2));
            foreachFeature(supported, [&](uint256 const& f) {
                bool const hasnot = (f == featureDynamicMPT || f == featureTokenEscrow);
                this->BEAST_EXPECT(hasnot != hasFeature(env, f));
            });
        }

        {
            // add a feature that is NOT in the supported amendments list
            // along with a list of explicit amendments
            // the unsupported feature should be enabled along with
            // the two supported ones
            Env env{
                *this, FeatureBitset{featureDynamicMPT, featureTokenEscrow, *neverSupportedFeat}};

            // this app will have just 2 supported amendments and
            // one additional never supported feature flag
            BEAST_EXPECT(env.app().config().features.size() == (2 + 1));
            BEAST_EXPECT(hasFeature(env, *neverSupportedFeat));

            foreachFeature(supported, [&](uint256 const& f) {
                bool const has = (f == featureDynamicMPT || f == featureTokenEscrow);
                this->BEAST_EXPECT(has == hasFeature(env, f));
            });
        }

        {
            // add a feature that is NOT in the supported amendments list
            // and omit a few standard amendments
            // the unsupported features should be enabled
            Env env{*this, missingSomeFeatures | FeatureBitset{*neverSupportedFeat}};

            // this app will have all supported amendments minus 2 and then the
            // one additional never supported feature flag
            BEAST_EXPECT(env.app().config().features.size() == (supported.count() - 2 + 1));
            BEAST_EXPECT(hasFeature(env, *neverSupportedFeat));
            foreachFeature(supported, [&](uint256 const& f) {
                bool const hasnot = (f == featureDynamicMPT || f == featureTokenEscrow);
                this->BEAST_EXPECT(hasnot != hasFeature(env, f));
            });
        }

        {
            // add a feature that is NOT in the supported amendments list
            // along with all supported amendments
            // the unsupported features should be enabled
            Env env{*this, testableAmendments().set(*neverSupportedFeat)};

            // this app will have all supported amendments and then the
            // one additional never supported feature flag
            BEAST_EXPECT(env.app().config().features.size() == (supported.count() + 1));
            BEAST_EXPECT(hasFeature(env, *neverSupportedFeat));
            foreachFeature(
                supported, [&](uint256 const& f) { this->BEAST_EXPECT(hasFeature(env, f)); });
        }
    }

    void
    testExceptionalShutdown()
    {
        except([this] {
            jtx::Env const env{
                *this,
                jtx::envconfig([](std::unique_ptr<Config> cfg) {
                    (*cfg).deprecatedClearSection(Sections::kPortRpc);
                    return cfg;
                }),
                nullptr,
                beast::Severity::Disabled};
        });
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
        testMemoResult();
        testAdvance();
        testClose();
        testPath();
        testResignSigned();
        testSignAndSubmit();
        testFeatures();
        testExceptionalShutdown();
    }
};

BEAST_DEFINE_TESTSUITE(Env, jtx, xrpl);

}  // namespace xrpl::test
