//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/Feature.h>
#include <test/jtx.h>

namespace ripple {

class SetRegularKey_test : public beast::unit_test::suite
{
public:

    void testDisableMasterKey()
    {
        testcase("Disable master key");
        using namespace test::jtx;
        Env env(*this);
        Account const alice("alice");
        Account const bob("bob");
        env.fund(XRP(10000), alice, bob);

        // Master and Regular key
        env(regkey(alice, bob));
        auto const ar = env.le(alice);
        BEAST_EXPECT(ar->isFieldPresent(sfRegularKey) && (ar->getAccountID(sfRegularKey) == bob.id()));

        env(noop(alice), sig(bob));
        env(noop(alice), sig(alice));

        // Regular key only
        env(fset(alice, asfDisableMaster), sig(alice));
        env(noop(alice), sig(bob));
        env(noop(alice), sig(alice), ter(tefMASTER_DISABLED));
        env(fclear(alice, asfDisableMaster), sig(alice), ter(tefMASTER_DISABLED));
        env(fclear(alice, asfDisableMaster), sig(bob));
        env(noop(alice), sig(alice));
    }

    /**
     * @see https://ripplelabs.atlassian.net/browse/RIPD-1721
     */
    void testSetRegularKeyToMasterKey()
    {
        testcase("Set regular key to master key (before fix1721)");
        using namespace test::jtx;
        Env env {*this, supported_amendments() - fix1721};
        Account const alice("alice");
        env.fund(XRP(10000), alice);

        // Must be possible unless amendment `fix1721` enabled.
        log << "set regular key to master key";
        env(regkey(alice, alice), sig(alice));
        log << "disable master key";
        env(fset(alice, asfDisableMaster), sig(alice));
        // No way to sign.
        log << "implicitly sign transaction with master key";
        env(noop(alice), ter(tefMASTER_DISABLED));
        env(noop(alice), sig(alice), ter(tefMASTER_DISABLED));
    }

    /**
     * @see https://ripplelabs.atlassian.net/browse/RIPD-1721
     */
    void testSetRegularKeyToMasterKeyAfterFix1721()
    {
        testcase("Set regular key to master key (after fix1721)");
        using namespace test::jtx;
        Env env {*this, supported_amendments() | fix1721};
        Account const alice("alice");
        env.fund(XRP(10000), alice);

        // Must be possible unless amendment `fix1721` enabled.
        env(regkey(alice, alice), ter(temCANT_USE_MASTER_KEY));
    }

    void testPasswordSpent()
    {
        testcase("Password spent");
        using namespace test::jtx;
        Env env(*this);
        Account const alice("alice");
        Account const bob("bob");
        env.fund(XRP(10000), alice, bob);

        auto ar = env.le(alice);
        BEAST_EXPECT(ar->isFieldPresent(sfFlags) && ((ar->getFieldU32(sfFlags) & lsfPasswordSpent) == 0));

        env(regkey(alice, bob), sig(alice), fee(0));

        ar = env.le(alice);
        BEAST_EXPECT(ar->isFieldPresent(sfFlags) && ((ar->getFieldU32(sfFlags) & lsfPasswordSpent) == lsfPasswordSpent));

        // The second SetRegularKey transaction with Fee=0 should fail.
        env(regkey(alice, bob), sig(alice), fee(0), ter(telINSUF_FEE_P));

        env.trust(bob["USD"](1), alice);
        env(pay(bob, alice, bob["USD"](1)));
        ar = env.le(alice);
        BEAST_EXPECT(ar->isFieldPresent(sfFlags) && ((ar->getFieldU32(sfFlags) & lsfPasswordSpent) == 0));
    }

    void testUniversalMaskError()
    {
        testcase("Universal mask");
        using namespace test::jtx;
        Env env(*this);
        Account const alice("alice");
        Account const bob("bob");
        env.fund(XRP(10000), alice, bob);

        auto jv = regkey(alice, bob);
        jv[sfFlags.fieldName] = tfUniversalMask;
        env(jv, ter(temINVALID_FLAG));
    }

    void run() override
    {
        testDisableMasterKey();
        testSetRegularKeyToMasterKey();
        testSetRegularKeyToMasterKeyAfterFix1721();
        testPasswordSpent();
        testUniversalMaskError();
    }

};

BEAST_DEFINE_TESTSUITE(SetRegularKey,app,ripple);

}

