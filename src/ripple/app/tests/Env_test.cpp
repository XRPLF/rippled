//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2012-2015 Ripple Labs Inc.

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

#include <BeastConfig.h>
#include <ripple/app/tests/Env.h>
#include <ripple/json/to_string.h>
#include <ripple/protocol/TxFlags.h>
#include <beast/unit_test/suite.h>

namespace ripple {
namespace test {

class Env_test : public beast::unit_test::suite
{
public:
    void
    testAutofill()
    {
        using namespace jtx;
        Env env(*this);
        env.fund(XRP(10000), "alice", "bob");
        env(noop("alice"));
        env(noop("alice"), seq(none), fee(10),                  ter(temMALFORMED));
        env(noop("alice"), fee(none),                           ter(temMALFORMED));
    }

    // Signing with secp256k1 and ed25519 keys
    void
    testKeyType()
    {
        using namespace jtx;

        Env env(*this);
        Account const alice("alice", KeyType::ed25519);
        Account const bob("bob", KeyType::secp256k1);
        Account const carol("carol");
        env.fund(XRP(10000), alice, bob);

        // Master key only
        env(noop(alice));
        env(noop(bob));
        env(noop(alice), sig("alice"),                          ter(tefBAD_AUTH_MASTER));
        env(noop(alice), sig(Account("alice",
            KeyType::secp256k1)),                               ter(tefBAD_AUTH_MASTER));
        env(noop(bob), sig(Account("bob",
            KeyType::ed25519)),                                 ter(tefBAD_AUTH_MASTER));
        env(noop(alice), sig(carol),                            ter(tefBAD_AUTH_MASTER));

        // Master and Regular key
        env(regkey(alice, bob));
        env(noop(alice));
        env(noop(alice), sig(bob));
        env(noop(alice), sig(alice));

        // Regular key only
        env(set(alice, asfDisableMaster), sig(alice));
        env(noop(alice));
        env(noop(alice), sig(bob));
        env(noop(alice), sig(alice),                            ter(tefMASTER_DISABLED));
        env(clear(alice, asfDisableMaster), sig(alice),         ter(tefMASTER_DISABLED));
        env(clear(alice, asfDisableMaster), sig(bob));
        env(noop(alice), sig(alice));
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

        env(noop("alice"), msig("bob"));
        env(noop("alice"), msig("carol"));
        env(noop("alice"), msig("bob", "carol"));
        env(noop("alice"), msig("bob", "carol", "dilbert"),     ter(tefBAD_SIGNATURE));
    }

    // Two level Multi-sign
    void
    testMultiSign2()
    {
        using namespace jtx;

        Env env(*this);
        env.fund(XRP(10000), "alice", "bob", "carol");
        env.fund(XRP(10000), "david", "eric", "frank", "greg");
        env(signers("alice", 2, { { "bob", 1 },   { "carol", 1 } }));
        env(signers("bob",   1, { { "david", 1 }, { "eric", 1 } }));
        env(signers("carol", 1, { { "frank", 1 }, { "greg", 1 } }));

        env(noop("alice"), msig2(
        { { "bob", "david" } }),                                ter(tefBAD_QUORUM));
        
        env(noop("alice"), msig2(
        { { "bob", "david" }, { "bob", "eric" } }),             ter(tefBAD_QUORUM));

        env(noop("alice"), msig2(
        { { "carol", "frank" } }),                              ter(tefBAD_QUORUM));
        
        env(noop("alice"), msig2(
        { { "carol", "frank" }, { "carol", "greg" } }),         ter(tefBAD_QUORUM));

        env(noop("alice"), msig2(
        { { "bob", "david" }, { "carol", "frank" } }));

        env(noop("alice"), msig2(
        { { "bob", "david" }, { "bob", "eric" },
          { "carol", "frank" }, { "carol", "greg" } }));
    }

    // Payment basics
    void
    testPayments()
    {
        using namespace jtx;

        Env env(*this);
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];

        env(pay(env.master, "alice", XRP(1000)), fee(none),     ter(temMALFORMED));
        env(pay(env.master, "alice", XRP(1000)), fee(1),        ter(telINSUF_FEE_P));
        env(pay(env.master, "alice", XRP(1000)), seq(none),     ter(temMALFORMED));
        env(pay(env.master, "alice", XRP(1000)), seq(2),        ter(terPRE_SEQ));
        env(pay(env.master, "alice", XRP(1000)), sig(none),     ter(temMALFORMED));
        env(pay(env.master, "alice", XRP(1000)), sig("bob"),    ter(tefBAD_AUTH_MASTER));

        env(pay(env.master, "dilbert", XRP(1000)), sig(env.master));

        env.fund(XRP(10000), "alice", "bob", "carol", gw);
        expect(env["alice"].balance(XRP) == XRP(10000));
        expect(env["bob"].balance(XRP) == XRP(10000));
        expect(env["carol"].balance(XRP) == XRP(10000));
        expect(env[gw].balance(XRP) == XRP(10000));

        env.trust(USD(100), "alice", "bob", "carol");
        env(rate(gw, 1.05));

        env(pay(gw, "carol", USD(50)));
        expect(env["carol"].balance(USD) == USD(50));
        expect(env[gw].balance(Account("carol")["USD"]) == USD(-50));

        env(offer("carol", XRP(50), USD(50)));
        env(pay("alice", "bob", any(USD(10))),                  ter(tecPATH_DRY));
        env(pay("alice", "bob", any(USD(10))),
            paths(XRP), sendmax(XRP(10)),                       ter(tecPATH_PARTIAL));
        env(pay("alice", "bob", any(USD(10))), paths(XRP),
            sendmax(XRP(20)));
        expect(env["bob"].balance(USD) == USD(10));
        expect(env["carol"].balance(USD) == USD(39.5));

        env.memoize("eric");
        env(regkey("alice", "eric"));
        env(noop("alice"));
        env(noop("alice"), sig("alice"));
        env(noop("alice"), sig("eric"));
        env(noop("alice"), sig("bob"),                          ter(tefBAD_AUTH));
        env(set("alice", asfDisableMaster),                     ter(tecNEED_MASTER_KEY));
        env(set("alice", asfDisableMaster), sig("eric"),        ter(tecNEED_MASTER_KEY));
        expect(! (env["alice"].flags() & lsfDisableMaster));
        env(set("alice", asfDisableMaster), sig("alice"));
        expect(env["alice"].flags() & lsfDisableMaster);
        env(regkey("alice", disabled),                          ter(tecMASTER_DISABLED));
        env(noop("alice"));
        env(noop("alice"), sig("alice"),                        ter(tefMASTER_DISABLED));
        env(noop("alice"), sig("eric"));
        env(noop("alice"), sig("bob"),                          ter(tefBAD_AUTH));
        env(clear("alice", asfDisableMaster), sig("bob"),       ter(tefBAD_AUTH));
        env(clear("alice", asfDisableMaster), sig("alice"),     ter(tefMASTER_DISABLED));
        env(clear("alice", asfDisableMaster));
        expect(! (env["alice"].flags() & lsfDisableMaster));
        env(regkey("alice", disabled));
        env(noop("alice"), sig("eric"),                         ter(tefBAD_AUTH_MASTER));
        env(noop("alice"));
    }

    void
    run()
    {
        testAutofill();
        testKeyType();
        testMultiSign();
        testMultiSign2();
        testPayments();
    }
};

BEAST_DEFINE_TESTSUITE(Env,app,ripple)

} // test
} // ripple
