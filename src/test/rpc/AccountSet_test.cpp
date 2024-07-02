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

#include <test/jtx.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/protocol/AmountConversions.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

class AccountSet_test : public beast::unit_test::suite
{
public:
    void
    testNullAccountSet()
    {
        testcase("No AccountSet");

        using namespace test::jtx;
        Env env(*this);
        Account const alice("alice");
        env.fund(XRP(10000), noripple(alice));
        // ask for the ledger entry - account root, to check its flags
        auto const jrr = env.le(alice);
        BEAST_EXPECT(jrr && jrr->at(sfFlags) == 0u);
    }

    void
    testMostFlags()
    {
        testcase("Most Flags");

        using namespace test::jtx;
        Account const alice("alice");

        // Test without DepositAuth enabled initially.
        Env env(*this, supported_amendments() - featureDepositAuth);
        env.fund(XRP(10000), noripple(alice));

        // Give alice a regular key so she can legally set and clear
        // her asfDisableMaster flag.
        Account const alie{"alie", KeyType::secp256k1};
        env(regkey(alice, alie));
        env.close();

        auto testFlags = [this, &alice, &alie, &env](
                             std::initializer_list<std::uint32_t> goodFlags) {
            std::uint32_t const orig_flags = (*env.le(alice))[sfFlags];
            for (std::uint32_t flag{1u};
                 flag < std::numeric_limits<std::uint32_t>::digits;
                 ++flag)
            {
                if (flag == asfNoFreeze)
                {
                    // The asfNoFreeze flag can't be cleared.  It is tested
                    // elsewhere.
                    continue;
                }

                if (flag == asfAuthorizedNFTokenMinter)
                {
                    // The asfAuthorizedNFTokenMinter flag requires the
                    // presence or absence of the sfNFTokenMinter field in
                    // the transaction.  It is tested elsewhere.
                    continue;
                }

                if (flag == asfDisallowIncomingCheck ||
                    flag == asfDisallowIncomingPayChan ||
                    flag == asfDisallowIncomingNFTokenOffer ||
                    flag == asfDisallowIncomingTrustline)
                {
                    // These flags are part of the DisallowIncoming amendment
                    // and are tested elsewhere
                    continue;
                }
                if (flag == asfAllowTrustLineClawback)
                {
                    // The asfAllowTrustLineClawback flag can't be cleared.  It
                    // is tested elsewhere.
                    continue;
                }

                if (std::find(goodFlags.begin(), goodFlags.end(), flag) !=
                    goodFlags.end())
                {
                    // Good flag
                    env.require(nflags(alice, flag));
                    env(fset(alice, flag), sig(alice));
                    env.close();
                    env.require(flags(alice, flag));
                    env(fclear(alice, flag), sig(alie));
                    env.close();
                    env.require(nflags(alice, flag));
                    std::uint32_t const now_flags = (*env.le(alice))[sfFlags];
                    BEAST_EXPECT(now_flags == orig_flags);
                }
                else
                {
                    // Bad flag
                    BEAST_EXPECT((*env.le(alice))[sfFlags] == orig_flags);
                    env(fset(alice, flag), sig(alice));
                    env.close();
                    BEAST_EXPECT((*env.le(alice))[sfFlags] == orig_flags);
                    env(fclear(alice, flag), sig(alie));
                    env.close();
                    BEAST_EXPECT((*env.le(alice))[sfFlags] == orig_flags);
                }
            }
        };

        // Test with featureDepositAuth disabled.
        testFlags(
            {asfRequireDest,
             asfRequireAuth,
             asfDisallowXRP,
             asfGlobalFreeze,
             asfDisableMaster,
             asfDefaultRipple});

        // Enable featureDepositAuth and retest.
        env.enableFeature(featureDepositAuth);
        env.close();
        testFlags(
            {asfRequireDest,
             asfRequireAuth,
             asfDisallowXRP,
             asfGlobalFreeze,
             asfDisableMaster,
             asfDefaultRipple,
             asfDepositAuth});
    }

    void
    testSetAndResetAccountTxnID()
    {
        testcase("Set and reset AccountTxnID");

        using namespace test::jtx;
        Env env(*this);
        Account const alice("alice");
        env.fund(XRP(10000), noripple(alice));

        std::uint32_t const orig_flags = (*env.le(alice))[sfFlags];

        // asfAccountTxnID is special and not actually set as a flag,
        // so we check the field presence instead
        BEAST_EXPECT(!env.le(alice)->isFieldPresent(sfAccountTxnID));
        env(fset(alice, asfAccountTxnID), sig(alice));
        BEAST_EXPECT(env.le(alice)->isFieldPresent(sfAccountTxnID));
        env(fclear(alice, asfAccountTxnID));
        BEAST_EXPECT(!env.le(alice)->isFieldPresent(sfAccountTxnID));
        std::uint32_t const now_flags = (*env.le(alice))[sfFlags];
        BEAST_EXPECT(now_flags == orig_flags);
    }

    void
    testSetNoFreeze()
    {
        testcase("Set NoFreeze");

        using namespace test::jtx;
        Env env(*this);
        Account const alice("alice");
        env.fund(XRP(10000), noripple(alice));
        env.memoize("eric");
        env(regkey(alice, "eric"));

        env.require(nflags(alice, asfNoFreeze));
        env(fset(alice, asfNoFreeze), sig("eric"), ter(tecNEED_MASTER_KEY));
        env(fset(alice, asfNoFreeze), sig(alice));
        env.require(flags(alice, asfNoFreeze));
        env(fclear(alice, asfNoFreeze), sig(alice));
        // verify flag is still set (clear does not clear in this case)
        env.require(flags(alice, asfNoFreeze));
    }

    void
    testDomain()
    {
        testcase("Domain");

        using namespace test::jtx;
        Env env(*this);
        Account const alice("alice");
        env.fund(XRP(10000), alice);
        auto jt = noop(alice);
        // The Domain field is represented as the hex string of the lowercase
        // ASCII of the domain. For example, the domain example.com would be
        // represented as "6578616d706c652e636f6d".
        //
        // To remove the Domain field from an account, send an AccountSet with
        // the Domain set to an empty string.
        std::string const domain = "example.com";
        jt[sfDomain.fieldName] = strHex(domain);
        env(jt);
        BEAST_EXPECT((*env.le(alice))[sfDomain] == makeSlice(domain));

        jt[sfDomain.fieldName] = "";
        env(jt);
        BEAST_EXPECT(!env.le(alice)->isFieldPresent(sfDomain));

        // The upper limit on the length is 256 bytes
        // (defined as DOMAIN_BYTES_MAX in SetAccount)
        // test the edge cases: 255, 256, 257.
        std::size_t const maxLength = 256;
        for (std::size_t len = maxLength - 1; len <= maxLength + 1; ++len)
        {
            std::string domain2 =
                std::string(len - domain.length() - 1, 'a') + "." + domain;

            BEAST_EXPECT(domain2.length() == len);

            jt[sfDomain.fieldName] = strHex(domain2);

            if (len <= maxLength)
            {
                env(jt);
                BEAST_EXPECT((*env.le(alice))[sfDomain] == makeSlice(domain2));
            }
            else
            {
                env(jt, ter(telBAD_DOMAIN));
            }
        }
    }

    void
    testMessageKey()
    {
        testcase("MessageKey");

        using namespace test::jtx;
        Env env(*this);
        Account const alice("alice");
        env.fund(XRP(10000), alice);
        auto jt = noop(alice);

        auto const rkp = randomKeyPair(KeyType::ed25519);
        jt[sfMessageKey.fieldName] = strHex(rkp.first.slice());
        env(jt);
        BEAST_EXPECT(
            strHex((*env.le(alice))[sfMessageKey]) ==
            strHex(rkp.first.slice()));

        jt[sfMessageKey.fieldName] = "";
        env(jt);
        BEAST_EXPECT(!env.le(alice)->isFieldPresent(sfMessageKey));

        using namespace std::string_literals;
        jt[sfMessageKey.fieldName] = strHex("NOT_REALLY_A_PUBKEY"s);
        env(jt, ter(telBAD_PUBLIC_KEY));
    }

    void
    testWalletID()
    {
        testcase("WalletID");

        using namespace test::jtx;
        Env env(*this);
        Account const alice("alice");
        env.fund(XRP(10000), alice);
        auto jt = noop(alice);

        std::string const locator =
            "9633EC8AF54F16B5286DB1D7B519EF49EEFC050C0C8AC4384F1D88ACD1BFDF05";
        jt[sfWalletLocator.fieldName] = locator;
        env(jt);
        BEAST_EXPECT(to_string((*env.le(alice))[sfWalletLocator]) == locator);

        jt[sfWalletLocator.fieldName] = "";
        env(jt);
        BEAST_EXPECT(!env.le(alice)->isFieldPresent(sfWalletLocator));
    }

    void
    testEmailHash()
    {
        testcase("EmailHash");

        using namespace test::jtx;
        Env env(*this);
        Account const alice("alice");
        env.fund(XRP(10000), alice);
        auto jt = noop(alice);

        std::string const mh("5F31A79367DC3137FADA860C05742EE6");
        jt[sfEmailHash.fieldName] = mh;
        env(jt);
        BEAST_EXPECT(to_string((*env.le(alice))[sfEmailHash]) == mh);

        jt[sfEmailHash.fieldName] = "";
        env(jt);
        BEAST_EXPECT(!env.le(alice)->isFieldPresent(sfEmailHash));
    }

    void
    testTransferRate()
    {
        struct test_results
        {
            double set;
            TER code;
            double get;
        };

        testcase("TransferRate");

        using namespace test::jtx;
        auto doTests = [this](
                           FeatureBitset const& features,
                           std::initializer_list<test_results> testData) {
            Env env(*this, features);

            Account const alice("alice");
            env.fund(XRP(10000), alice);

            for (auto const& r : testData)
            {
                env(rate(alice, r.set), ter(r.code));
                env.close();

                // If the field is not present expect the default value
                if (!(*env.le(alice))[~sfTransferRate])
                    BEAST_EXPECT(r.get == 1.0);
                else
                    BEAST_EXPECT(
                        *(*env.le(alice))[~sfTransferRate] ==
                        r.get * QUALITY_ONE);
            }
        };

        doTests(
            supported_amendments(),
            {{1.0, tesSUCCESS, 1.0},
             {1.1, tesSUCCESS, 1.1},
             {2.0, tesSUCCESS, 2.0},
             {2.1, temBAD_TRANSFER_RATE, 2.0},
             {0.0, tesSUCCESS, 1.0},
             {2.0, tesSUCCESS, 2.0},
             {0.9, temBAD_TRANSFER_RATE, 2.0}});
    }

    void
    testGateway()
    {
        testcase("Gateway");

        using namespace test::jtx;

        Account const alice("alice");
        Account const bob("bob");
        Account const gw("gateway");
        auto const USD = gw["USD"];

        // Test gateway with a variety of allowed transfer rates
        for (double transferRate = 1.0; transferRate <= 2.0;
             transferRate += 0.03125)
        {
            Env env(*this);
            env.fund(XRP(10000), gw, alice, bob);
            env.close();
            env.trust(USD(10), alice, bob);
            env.close();
            env(rate(gw, transferRate));
            env.close();

            auto const amount = USD(1);
            Rate const rate(transferRate * QUALITY_ONE);
            auto const amountWithRate =
                toAmount<STAmount>(multiply(amount.value(), rate));

            env(pay(gw, alice, USD(10)));
            env.close();
            env(pay(alice, bob, USD(1)), sendmax(USD(10)));
            env.close();

            env.require(balance(alice, USD(10) - amountWithRate));
            env.require(balance(bob, USD(1)));
        }

        // Since fix1201 was enabled on Nov 14 2017 a rate in excess of
        // 2.0 has been blocked by the transactor.  But there are a few
        // accounts on the MainNet that have larger-than-currently-allowed
        // TransferRates.  We'll bypass the transactor so we can check
        // operation of these legacy TransferRates.
        //
        // Two out-of-bound values are currently in the ledger (March 2020)
        // They are 4.0 and 4.294967295.  So those are the values we test.
        for (double transferRate : {4.0, 4.294967295})
        {
            Env env(*this);
            env.fund(XRP(10000), gw, alice, bob);
            env.close();
            env.trust(USD(10), alice, bob);
            env.close();

            // We'd like to use transferRate here, but the transactor
            // blocks transfer rates that large.  So we use an acceptable
            // transfer rate here and later hack the ledger to replace
            // the acceptable value with an out-of-bounds value.
            env(rate(gw, 2.0));
            env.close();

            // Because we're hacking the ledger we need the account to have
            // non-zero sfMintedNFTokens and sfBurnedNFTokens fields.  This
            // prevents an exception when the AccountRoot template is applied.
            {
                uint256 const nftId0{token::getNextID(env, gw, 0u)};
                env(token::mint(gw, 0u));
                env.close();

                env(token::burn(gw, nftId0));
                env.close();
            }

            // Note that we're bypassing almost all of the ledger's safety
            // checks with this modify() call.  If you call close() between
            // here and the end of the test all the effort will be lost.
            env.app().openLedger().modify(
                [&gw, transferRate](OpenView& view, beast::Journal j) {
                    // Get the account root we want to hijack.
                    auto const sle = view.read(keylet::account(gw.id()));
                    if (!sle)
                        return false;  // This would be really surprising!

                    // We'll insert a replacement for the account root
                    // with the higher (currently invalid) transfer rate.
                    auto replacement = std::make_shared<SLE>(*sle, sle->key());
                    (*replacement)[sfTransferRate] =
                        static_cast<std::uint32_t>(transferRate * QUALITY_ONE);
                    view.rawReplace(replacement);
                    return true;
                });

            auto const amount = USD(1);
            auto const amountWithRate = toAmount<STAmount>(
                multiply(amount.value(), Rate(transferRate * QUALITY_ONE)));

            env(pay(gw, alice, USD(10)));
            env(pay(alice, bob, amount), sendmax(USD(10)));

            env.require(balance(alice, USD(10) - amountWithRate));
            env.require(balance(bob, amount));
        }
    }

    void
    testBadInputs()
    {
        testcase("Bad inputs");

        using namespace test::jtx;
        Env env(*this);
        Account const alice("alice");
        env.fund(XRP(10000), alice);

        auto jt = fset(alice, asfDisallowXRP);
        jt[jss::ClearFlag] = asfDisallowXRP;
        env(jt, ter(temINVALID_FLAG));

        jt = fset(alice, asfRequireAuth);
        jt[jss::ClearFlag] = asfRequireAuth;
        env(jt, ter(temINVALID_FLAG));

        jt = fset(alice, asfRequireDest);
        jt[jss::ClearFlag] = asfRequireDest;
        env(jt, ter(temINVALID_FLAG));

        jt = fset(alice, asfDisallowXRP);
        jt[sfFlags.fieldName] = tfAllowXRP;
        env(jt, ter(temINVALID_FLAG));

        jt = fset(alice, asfRequireAuth);
        jt[sfFlags.fieldName] = tfOptionalAuth;
        env(jt, ter(temINVALID_FLAG));

        jt = fset(alice, asfRequireDest);
        jt[sfFlags.fieldName] = tfOptionalDestTag;
        env(jt, ter(temINVALID_FLAG));

        jt = fset(alice, asfRequireDest);
        jt[sfFlags.fieldName] = tfAccountSetMask;
        env(jt, ter(temINVALID_FLAG));

        env(fset(alice, asfDisableMaster),
            sig(alice),
            ter(tecNO_ALTERNATIVE_KEY));
    }

    void
    testRequireAuthWithDir()
    {
        testcase("Require auth");

        using namespace test::jtx;
        Env env(*this);
        Account const alice("alice");
        Account const bob("bob");

        env.fund(XRP(10000), alice);
        env.close();

        // alice should have an empty directory.
        BEAST_EXPECT(dirIsEmpty(*env.closed(), keylet::ownerDir(alice)));

        // Give alice a signer list, then there will be stuff in the directory.
        env(signers(alice, 1, {{bob, 1}}));
        env.close();
        BEAST_EXPECT(!dirIsEmpty(*env.closed(), keylet::ownerDir(alice)));

        env(fset(alice, asfRequireAuth), ter(tecOWNERS));

        // Remove the signer list.  After that asfRequireAuth should succeed.
        env(signers(alice, test::jtx::none));
        env.close();
        BEAST_EXPECT(dirIsEmpty(*env.closed(), keylet::ownerDir(alice)));

        env(fset(alice, asfRequireAuth));
    }

    void
    testTicket()
    {
        using namespace test::jtx;
        Env env(*this);
        Account const alice("alice");

        env.fund(XRP(10000), alice);
        env.close();

        std::uint32_t const ticketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, 1));
        env.close();
        env.require(owners(alice, 1), tickets(alice, 1));

        // Try using a ticket that alice doesn't have.
        env(noop(alice), ticket::use(ticketSeq + 1), ter(terPRE_TICKET));
        env.close();
        env.require(owners(alice, 1), tickets(alice, 1));

        // Actually use alice's ticket.  Note that if a transaction consumes
        // a ticket then the account's sequence number does not advance.
        std::uint32_t const aliceSeq{env.seq(alice)};
        env(noop(alice), ticket::use(ticketSeq));
        env.close();
        env.require(owners(alice, 0), tickets(alice, 0));
        BEAST_EXPECT(aliceSeq == env.seq(alice));

        // Try re-using a ticket that alice already used.
        env(noop(alice), ticket::use(ticketSeq), ter(tefNO_TICKET));
        env.close();
    }

    void
    run() override
    {
        testNullAccountSet();
        testMostFlags();
        testSetAndResetAccountTxnID();
        testSetNoFreeze();
        testDomain();
        testGateway();
        testMessageKey();
        testWalletID();
        testEmailHash();
        testBadInputs();
        testRequireAuthWithDir();
        testTransferRate();
        testTicket();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(AccountSet, app, ripple, 1);

}  // namespace ripple
