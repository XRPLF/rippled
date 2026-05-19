
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/flags.h>
#include <test/jtx/multisign.h>
#include <test/jtx/noop.h>
#include <test/jtx/owners.h>
#include <test/jtx/pay.h>
#include <test/jtx/rate.h>
#include <test/jtx/regkey.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/sig.h>
#include <test/jtx/tags.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>
#include <test/jtx/token.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AmountConversions.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/apply.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>

namespace xrpl {

class AccountSet_test : public beast::unit_test::Suite
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

        Env env(*this, testableAmendments());
        env.fund(XRP(10000), noripple(alice));

        // Give alice a regular key so she can legally set and clear
        // her asfDisableMaster flag.
        Account const alie{"alie", KeyType::Secp256k1};
        env(regkey(alice, alie));
        env.close();

        auto testFlags = [this, &alice, &alie, &env](
                             std::initializer_list<std::uint32_t> goodFlags) {
            std::uint32_t const origFlags = (*env.le(alice))[sfFlags];
            for (std::uint32_t flag{1u}; flag < std::numeric_limits<std::uint32_t>::digits; ++flag)
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

                if (flag == asfDisallowIncomingCheck || flag == asfDisallowIncomingPayChan ||
                    flag == asfDisallowIncomingNFTokenOffer || flag == asfDisallowIncomingTrustline)
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
                if (flag == asfAllowTrustLineLocking)
                {
                    // These flags are part of the AllowTokenLocking amendment
                    // and are tested elsewhere
                    continue;
                }

                if (std::ranges::find(goodFlags, flag) != goodFlags.end())
                {
                    // Good flag
                    env.require(Nflags(alice, flag));
                    env(fset(alice, flag), Sig(alice));
                    env.close();
                    env.require(Flags(alice, flag));
                    env(fclear(alice, flag), Sig(alie));
                    env.close();
                    env.require(Nflags(alice, flag));
                    std::uint32_t const nowFlags = (*env.le(alice))[sfFlags];
                    BEAST_EXPECT(nowFlags == origFlags);
                }
                else
                {
                    // Bad flag
                    BEAST_EXPECT((*env.le(alice))[sfFlags] == origFlags);
                    env(fset(alice, flag), Sig(alice));
                    env.close();
                    BEAST_EXPECT((*env.le(alice))[sfFlags] == origFlags);
                    env(fclear(alice, flag), Sig(alie));
                    env.close();
                    BEAST_EXPECT((*env.le(alice))[sfFlags] == origFlags);
                }
            }
        };
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

        std::uint32_t const origFlags = (*env.le(alice))[sfFlags];

        // asfAccountTxnID is special and not actually set as a flag,
        // so we check the field presence instead
        BEAST_EXPECT(!env.le(alice)->isFieldPresent(sfAccountTxnID));
        env(fset(alice, asfAccountTxnID), Sig(alice));
        BEAST_EXPECT(env.le(alice)->isFieldPresent(sfAccountTxnID));
        env(fclear(alice, asfAccountTxnID));
        BEAST_EXPECT(!env.le(alice)->isFieldPresent(sfAccountTxnID));
        std::uint32_t const nowFlags = (*env.le(alice))[sfFlags];
        BEAST_EXPECT(nowFlags == origFlags);
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

        env.require(Nflags(alice, asfNoFreeze));
        env(fset(alice, asfNoFreeze), Sig("eric"), Ter(tecNEED_MASTER_KEY));
        env(fset(alice, asfNoFreeze), Sig(alice));
        env.require(Flags(alice, asfNoFreeze));
        env(fclear(alice, asfNoFreeze), Sig(alice));
        // verify flag is still set (clear does not clear in this case)
        env.require(Flags(alice, asfNoFreeze));
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
        // (defined as DOMAIN_BYTES_MAX in AccountSet)
        // test the edge cases: 255, 256, 257.
        std::size_t const maxLength = 256;
        for (std::size_t len = maxLength - 1; len <= maxLength + 1; ++len)
        {
            std::string const domain2 = std::string(len - domain.length() - 1, 'a') + "." + domain;

            BEAST_EXPECT(domain2.length() == len);

            jt[sfDomain.fieldName] = strHex(domain2);

            if (len <= maxLength)
            {
                env(jt);
                BEAST_EXPECT((*env.le(alice))[sfDomain] == makeSlice(domain2));
            }
            else
            {
                env(jt, Ter(telBAD_DOMAIN));
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

        auto const rkp = randomKeyPair(KeyType::Ed25519);
        jt[sfMessageKey.fieldName] = strHex(rkp.first.slice());
        env(jt);
        BEAST_EXPECT(strHex((*env.le(alice))[sfMessageKey]) == strHex(rkp.first.slice()));

        jt[sfMessageKey.fieldName] = "";
        env(jt);
        BEAST_EXPECT(!env.le(alice)->isFieldPresent(sfMessageKey));

        using namespace std::string_literals;
        jt[sfMessageKey.fieldName] = strHex("NOT_REALLY_A_PUBKEY"s);
        env(jt, Ter(telBAD_PUBLIC_KEY));
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
        struct TestResults
        {
            double set;
            TER code;
            double get;
        };

        testcase("TransferRate");

        using namespace test::jtx;
        auto doTests =
            [this](FeatureBitset const& features, std::initializer_list<TestResults> testData) {
                Env env(*this, features);

                Account const alice("alice");
                env.fund(XRP(10000), alice);

                for (auto const& r : testData)
                {
                    env(rate(alice, r.set), Ter(r.code));
                    env.close();

                    // If the field is not present expect the default value
                    if (!(*env.le(alice))[~sfTransferRate])
                    {
                        BEAST_EXPECT(r.get == 1.0);
                    }
                    else
                    {
                        BEAST_EXPECT(*(*env.le(alice))[~sfTransferRate] == r.get * QUALITY_ONE);
                    }
                }
            };

        doTests(
            testableAmendments(),
            {{.set = 1.0, .code = tesSUCCESS, .get = 1.0},
             {.set = 1.1, .code = tesSUCCESS, .get = 1.1},
             {.set = 2.0, .code = tesSUCCESS, .get = 2.0},
             {.set = 2.1, .code = temBAD_TRANSFER_RATE, .get = 2.0},
             {.set = 0.0, .code = tesSUCCESS, .get = 1.0},
             {.set = 2.0, .code = tesSUCCESS, .get = 2.0},
             {.set = 0.9, .code = temBAD_TRANSFER_RATE, .get = 2.0}});
    }

    void
    testGateway()
    {
        testcase("Gateway");

        using namespace test::jtx;

        Account const alice("alice");
        Account const bob("bob");
        Account const gw("gateway");
        auto const usd = gw["USD"];

        // Test gateway with a variety of allowed transfer rates
        for (double transferRate = 1.0; transferRate <= 2.0; transferRate += 0.03125)
        {
            Env env(*this);
            env.fund(XRP(10000), gw, alice, bob);
            env.close();
            env.trust(usd(10), alice, bob);
            env.close();
            env(rate(gw, transferRate));
            env.close();

            auto const amount = usd(1);
            Rate const rate(transferRate * QUALITY_ONE);
            auto const amountWithRate = toAmount<STAmount>(multiply(amount.value(), rate));

            env(pay(gw, alice, usd(10)));
            env.close();
            env(pay(alice, bob, usd(1)), Sendmax(usd(10)));
            env.close();

            env.require(Balance(alice, usd(10) - amountWithRate));
            env.require(Balance(bob, usd(1)));
        }

        // Since fix1201 was enabled on Nov 14 2017 a rate in excess of
        // 2.0 has been blocked by the transactor.  But there are a few
        // accounts on the MainNet that have larger-than-currently-allowed
        // TransferRates.  We'll bypass the transactor so we can check
        // operation of these legacy TransferRates.
        //
        // Two out-of-bound values are currently in the ledger (March 2020)
        // They are 4.0 and 4.294967295.  So those are the values we test.
        for (double const transferRate : {4.0, 4.294967295})
        {
            Env env(*this);
            env.fund(XRP(10000), gw, alice, bob);
            env.close();
            env.trust(usd(10), alice, bob);
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
            env.app().getOpenLedger().modify([&gw, transferRate](OpenView& view, beast::Journal j) {
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

            auto const amount = usd(1);
            auto const amountWithRate =
                toAmount<STAmount>(multiply(amount.value(), Rate(transferRate * QUALITY_ONE)));

            env(pay(gw, alice, usd(10)));
            env(pay(alice, bob, amount), Sendmax(usd(10)));

            env.require(Balance(alice, usd(10) - amountWithRate));
            env.require(Balance(bob, amount));
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
        env(jt, Ter(temINVALID_FLAG));

        jt = fset(alice, asfRequireAuth);
        jt[jss::ClearFlag] = asfRequireAuth;
        env(jt, Ter(temINVALID_FLAG));

        jt = fset(alice, asfRequireDest);
        jt[jss::ClearFlag] = asfRequireDest;
        env(jt, Ter(temINVALID_FLAG));

        jt = fset(alice, asfDisallowXRP);
        jt[sfFlags.fieldName] = tfAllowXRP;
        env(jt, Ter(temINVALID_FLAG));

        jt = fset(alice, asfRequireAuth);
        jt[sfFlags.fieldName] = tfOptionalAuth;
        env(jt, Ter(temINVALID_FLAG));

        jt = fset(alice, asfRequireDest);
        jt[sfFlags.fieldName] = tfOptionalDestTag;
        env(jt, Ter(temINVALID_FLAG));

        jt = fset(alice, asfRequireDest);
        jt[sfFlags.fieldName] = tfAccountSetMask;
        env(jt, Ter(temINVALID_FLAG));

        env(fset(alice, asfDisableMaster), Sig(alice), Ter(tecNO_ALTERNATIVE_KEY));
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

        env(fset(alice, asfRequireAuth), Ter(tecOWNERS));

        // Remove the signer list.  After that asfRequireAuth should succeed.
        env(signers(alice, test::jtx::kNone));
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
        env.require(Owners(alice, 1), tickets(alice, 1));

        // Try using a ticket that alice doesn't have.
        env(noop(alice), ticket::Use(ticketSeq + 1), Ter(terPRE_TICKET));
        env.close();
        env.require(Owners(alice, 1), tickets(alice, 1));

        // Actually use alice's ticket.  Note that if a transaction consumes
        // a ticket then the account's sequence number does not advance.
        std::uint32_t const aliceSeq{env.seq(alice)};
        env(noop(alice), ticket::Use(ticketSeq));
        env.close();
        env.require(Owners(alice, 0), tickets(alice, 0));
        BEAST_EXPECT(aliceSeq == env.seq(alice));

        // Try re-using a ticket that alice already used.
        env(noop(alice), ticket::Use(ticketSeq), Ter(tefNO_TICKET));
        env.close();
    }

    void
    testBadSigningKey()
    {
        using namespace test::jtx;
        testcase("Bad signing key");
        Env env(*this);
        Account const alice("alice");

        env.fund(XRP(10000), alice);
        env.close();

        auto jtx = env.jt(noop("alice"), Ter(temBAD_SIGNATURE));
        if (!BEAST_EXPECT(jtx.stx))
            return;
        auto stx = std::make_shared<STTx>(*jtx.stx);
        stx->at(sfSigningPubKey) = makeSlice(std::string("badkey"));

        env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal j) {
            auto const result = xrpl::apply(env.app(), view, *stx, TapNone, j);
            BEAST_EXPECT(result.ter == temBAD_SIGNATURE);
            BEAST_EXPECT(!result.applied);
            return result.applied;
        });
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
        testBadSigningKey();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(AccountSet, app, xrpl, 1);

}  // namespace xrpl
