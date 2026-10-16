#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>  // IWYU pragma: keep
#include <test/jtx/envconfig.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/offer.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/ledger/helpers/TokenIssuanceHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

namespace xrpl {

class TokenIssuance_test : public beast::unit_test::Suite
{
    FeatureBitset const all_{test::jtx::testableAmendments()};

    static json::Value
    tiCreate(test::jtx::Account const& account, std::string const& currency)
    {
        json::Value jv;
        jv[jss::TransactionType] = jss::TokenIssuanceCreate;
        jv[jss::Account] = account.human();
        jv[sfCurrency.jsonName] = currency;
        return jv;
    }

    static json::Value
    tiSet(test::jtx::Account const& account, std::string const& currency)
    {
        json::Value jv;
        jv[jss::TransactionType] = jss::TokenIssuanceSet;
        jv[jss::Account] = account.human();
        jv[sfCurrency.jsonName] = currency;
        return jv;
    }

    static json::Value
    tiDestroy(test::jtx::Account const& account, std::string const& currency)
    {
        json::Value jv;
        jv[jss::TransactionType] = jss::TokenIssuanceDestroy;
        jv[jss::Account] = account.human();
        jv[sfCurrency.jsonName] = currency;
        return jv;
    }

    static json::Value
    tiConvert(test::jtx::Account const& account, STAmount const& amount)
    {
        json::Value jv;
        jv[jss::TransactionType] = jss::TokenConvert;
        jv[jss::Account] = account.human();
        jv[jss::Amount] = amount.getJson(JsonOptions::Values::None);
        return jv;
    }

    static std::shared_ptr<SLE const>
    issuanceSle(test::jtx::Env& env, test::jtx::Account const& issuer, test::jtx::IOU const& iou)
    {
        return env.le(keylet::tokenIssuance(issuer.id(), iou.currency));
    }

    bool
    expectIssued(
        test::jtx::Env& env,
        test::jtx::Account const& issuer,
        test::jtx::IOU const& iou,
        Number const& expected)
    {
        auto const sle = issuanceSle(env, issuer, iou);
        if (!BEAST_EXPECT(sle))
            return false;
        Number const issued = (*sle)[sfIssuedAmount];
        return BEAST_EXPECTS(issued == expected, to_string(issued) + " != " + to_string(expected));
    }

    void
    testDisabled(FeatureBitset features)
    {
        testcase("disabled");
        using namespace test::jtx;

        Env env(*this, features - featureTokenIssuance);
        Account const gw{"gw"};
        env.fund(XRP(10'000), gw);
        env.close();

        env(tiCreate(gw, "USD"), Ter(temDISABLED));
        env(tiSet(gw, "USD"), Ter(temDISABLED));
        env(tiDestroy(gw, "USD"), Ter(temDISABLED));
        env(tiConvert(gw, gw["USD"](10)), Ter(temDISABLED));
    }

    void
    testCreatePreflight(FeatureBitset features)
    {
        testcase("create preflight");
        using namespace test::jtx;

        Env env(*this, features);
        Account const gw{"gw"};
        env.fund(XRP(10'000), gw);
        env.close();

        // XRP is not a valid currency for an issuance
        env(tiCreate(gw, "XRP"), Ter(temBAD_CURRENCY));

        // Bad transfer fee
        {
            auto jv = tiCreate(gw, "USD");
            jv[sfTransferFee.jsonName] = 50'001;
            env(jv, Ter(temBAD_TRANSFER_FEE));
        }

        // Zero maximum
        {
            auto jv = tiCreate(gw, "USD");
            jv[sfMaximumAmount.jsonName] = "0";
            jv[sfTokenScale.jsonName] = 0;
            env(jv, Ter(temMALFORMED));
        }

        // Maximum above the representable bound
        {
            auto jv = tiCreate(gw, "USD");
            jv[sfMaximumAmount.jsonName] = "1000000000000001";
            jv[sfTokenScale.jsonName] = 0;
            env(jv, Ter(temMALFORMED));
        }

        // Maximum without a scale
        {
            auto jv = tiCreate(gw, "USD");
            jv[sfMaximumAmount.jsonName] = "1000";
            env(jv, Ter(temMALFORMED));
        }

        // Scale out of range
        {
            auto jv = tiCreate(gw, "USD");
            jv[sfMaximumAmount.jsonName] = "1000";
            jv[sfTokenScale.jsonName] = 19;
            env(jv, Ter(temMALFORMED));
        }

        // Invalid flags
        env(tiCreate(gw, "USD"), Txflags(0x00000008), Ter(temINVALID_FLAG));

        // Empty metadata
        {
            auto jv = tiCreate(gw, "USD");
            jv[sfMPTokenMetadata.jsonName] = "";
            env(jv, Ter(temMALFORMED));
        }
    }

    void
    testCreate(FeatureBitset features)
    {
        testcase("create");
        using namespace test::jtx;

        Env env(*this, features);
        Account const gw{"gw"};
        auto const USD = gw["USD"];
        env.fund(XRP(10'000), gw);
        env.close();

        BEAST_EXPECT(env.ownerCount(gw) == 0);

        // Uncapped, metadata-only issuance
        env(tiCreate(gw, "USD"));
        env.close();

        {
            auto const sle = issuanceSle(env, gw, USD);
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT((*sle)[sfIssuer] == gw.id());
            BEAST_EXPECT(!sle->isFieldPresent(sfMaximumAmount));
            Number const issued = (*sle)[sfIssuedAmount];
            BEAST_EXPECT(issued == Number{});
        }
        BEAST_EXPECT(env.ownerCount(gw) == 1);

        // Duplicate fails
        env(tiCreate(gw, "USD"), Ter(tecDUPLICATE));

        // Capped issuance for another currency, with CannotLock renounced
        {
            auto jv = tiCreate(gw, "EUR");
            jv[sfMaximumAmount.jsonName] = "1000";
            jv[sfTokenScale.jsonName] = 0;
            env(jv, Txflags(tfTokenCannotLock));
            env.close();

            auto const sle = issuanceSle(env, gw, gw["EUR"]);
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT((*sle)[sfMaximumAmount] == 1000);
            BEAST_EXPECT((*sle)[sfTokenScale] == 0);
            BEAST_EXPECT(sle->isFlag(lsfTokenCannotLock));
        }
        BEAST_EXPECT(env.ownerCount(gw) == 2);
    }

    void
    testSetAndLock(FeatureBitset features)
    {
        testcase("set and lock");
        using namespace test::jtx;

        Env env(*this, features);
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        auto const USD = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        env.trust(USD(1'000), alice, bob);
        env.close();

        // Set on a missing issuance
        env(tiSet(gw, "USD"), Ter(tecNO_ENTRY));

        env(tiCreate(gw, "USD"));
        env.close();

        env(pay(gw, alice, USD(100)));
        env.close();

        // Contradictory flags
        env(tiSet(gw, "USD"), Txflags(tfTokenLock | tfTokenUnlock), Ter(temINVALID_FLAG));
        env(tiSet(gw, "USD"), Txflags(tfTokenLock | tfTokenCannotLock), Ter(temINVALID_FLAG));

        // Lock the currency: holder-to-holder transfers stop, redemption
        // to the issuer still works (GlobalFreeze semantics, scoped).
        env(tiSet(gw, "USD"), Txflags(tfTokenLock));
        env.close();
        {
            auto const sle = issuanceSle(env, gw, USD);
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT(sle->isFlag(lsfTokenLocked));
        }

        env(pay(alice, bob, USD(10)), Ter(tecPATH_DRY));
        env.close();
        env(pay(alice, gw, USD(10)));
        env.close();
        env.require(Balance(alice, USD(90)));

        // Unlock restores transfers
        env(tiSet(gw, "USD"), Txflags(tfTokenUnlock));
        env.close();
        env(pay(alice, bob, USD(10)));
        env.close();
        env.require(Balance(bob, USD(10)));

        // Metadata and fee updates
        {
            auto jv = tiSet(gw, "USD");
            jv[sfTransferFee.jsonName] = 100;
            jv[sfMPTokenMetadata.jsonName] = strHex(std::string{"meta"});
            env(jv);
            env.close();
            auto const sle = issuanceSle(env, gw, USD);
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT((*sle)[sfTransferFee] == 100);
            BEAST_EXPECT(sle->isFieldPresent(sfMPTokenMetadata));
        }

        // Renounce locking, then locking fails
        env(tiSet(gw, "USD"), Txflags(tfTokenCannotLock));
        env.close();
        env(tiSet(gw, "USD"), Txflags(tfTokenLock), Ter(tecNO_PERMISSION));
    }

    void
    testDestroy(FeatureBitset features)
    {
        testcase("destroy");
        using namespace test::jtx;

        Env env(*this, features);
        Account const gw{"gw"};
        Account const alice{"alice"};
        auto const USD = gw["USD"];
        env.fund(XRP(10'000), gw, alice);
        env.close();
        env.trust(USD(1'000), alice);
        env.close();

        env(tiDestroy(gw, "USD"), Ter(tecNO_ENTRY));

        env(tiCreate(gw, "USD"));
        env.close();

        env(pay(gw, alice, USD(100)));
        env.close();

        if (!expectIssued(env, gw, USD, Number(100)))
            return;

        // Outstanding tokens block destruction. Close before the redemption
        // so the failed destroy is not replayed after it in canonical order.
        env(tiDestroy(gw, "USD"), Ter(tecHAS_OBLIGATIONS));
        env.close();

        env(pay(alice, gw, USD(100)));
        env.close();

        if (!expectIssued(env, gw, USD, Number(0)))
            return;

        env(tiDestroy(gw, "USD"));
        env.close();
        BEAST_EXPECT(!issuanceSle(env, gw, USD));
        BEAST_EXPECT(env.ownerCount(gw) == 0);
    }

    void
    testSupplyCap(FeatureBitset features)
    {
        testcase("supply cap");
        using namespace test::jtx;

        Env env(*this, features);
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        auto const USD = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob, carol);
        env.close();
        env.trust(USD(10'000), alice, bob, carol);
        env.close();

        {
            auto jv = tiCreate(gw, "USD");
            jv[sfMaximumAmount.jsonName] = "1000";
            jv[sfTokenScale.jsonName] = 0;
            env(jv);
            env.close();
        }

        // Issue up to 600
        env(pay(gw, alice, USD(600)));
        env.close();
        if (!expectIssued(env, gw, USD, Number(600)))
            return;

        // 500 more would breach the cap
        env(pay(gw, alice, USD(500)), Ter(tecPATH_PARTIAL));
        env.close();
        expectIssued(env, gw, USD, Number(600));

        // Redemption frees headroom
        env(pay(alice, gw, USD(100)));
        env.close();
        if (!expectIssued(env, gw, USD, Number(500)))
            return;

        // Fill the cap exactly
        env(pay(gw, alice, USD(500)));
        env.close();
        if (!expectIssued(env, gw, USD, Number(1000)))
            return;

        env(pay(gw, bob, USD(1)), Ter(tecPATH_PARTIAL));
        env.close();

        // Holder-to-holder transfers are unaffected by the cap
        env(pay(alice, bob, USD(250)));
        expectIssued(env, gw, USD, Number(1000));
        env.close();
        env.require(Balance(bob, USD(250)));
        env.require(Balance(alice, USD(750)));
        if (!expectIssued(env, gw, USD, Number(1000)))
            return;

        // An issuer's offer within the remaining headroom crosses in full
        env(pay(alice, gw, USD(400)));
        env.close();
        if (!expectIssued(env, gw, USD, Number(600)))
            return;

        env(offer(gw, XRP(300), USD(300)));
        env.close();
        env(offer(bob, USD(300), XRP(300)));
        env.close();
        env.require(Balance(bob, USD(550)));
        if (!expectIssued(env, gw, USD, Number(900)))
            return;

        // An issuer's offer beyond the headroom can never push issuance past
        // the cap: the supply invariant rejects the crossing outright.
        env(offer(gw, XRP(600), USD(600)));
        env.close();
        env(offer(carol, USD(600), XRP(600)), Ter(tecINVARIANT_FAILED));
        env.close();
        env.require(Balance(carol, USD(0)));
        if (!expectIssued(env, gw, USD, Number(900)))
            return;
    }

    void
    testBinding(FeatureBitset features)
    {
        testcase("binding");
        using namespace test::jtx;

        Env env(*this, features);
        Account const gw{"gw"};
        Account const alice{"alice"};

        MPTTester mpt(env, gw, {.holders = {alice}});
        mpt.create({.maxAmt = 100'000, .assetScale = 2});
        auto const badId = makeMptID(env.seq(gw) + 100, gw.id());

        // Binding a nonexistent MPT issuance
        {
            auto jv = tiCreate(gw, "USD");
            jv[sfMaximumAmount.jsonName] = "100000";
            jv[sfTokenScale.jsonName] = 2;
            jv[sfMPTokenIssuanceID.jsonName] = to_string(badId);
            env(jv, Ter(tecOBJECT_NOT_FOUND));
        }

        // Cap mismatch
        {
            auto jv = tiCreate(gw, "USD");
            jv[sfMaximumAmount.jsonName] = "99999";
            jv[sfTokenScale.jsonName] = 2;
            jv[sfMPTokenIssuanceID.jsonName] = to_string(mpt.issuanceID());
            env(jv, Ter(tecWRONG_ASSET));
        }

        // Scale mismatch
        {
            auto jv = tiCreate(gw, "USD");
            jv[sfMaximumAmount.jsonName] = "100000";
            jv[sfTokenScale.jsonName] = 3;
            jv[sfMPTokenIssuanceID.jsonName] = to_string(mpt.issuanceID());
            env(jv, Ter(tecWRONG_ASSET));
        }

        // Not the MPT issuer
        {
            auto jv = tiCreate(alice, "USD");
            jv[sfMaximumAmount.jsonName] = "100000";
            jv[sfTokenScale.jsonName] = 2;
            jv[sfMPTokenIssuanceID.jsonName] = to_string(mpt.issuanceID());
            env(jv, Ter(tecNO_PERMISSION));
        }

        // Valid binding
        {
            auto jv = tiCreate(gw, "USD");
            jv[sfMaximumAmount.jsonName] = "100000";
            jv[sfTokenScale.jsonName] = 2;
            jv[sfMPTokenIssuanceID.jsonName] = to_string(mpt.issuanceID());
            env(jv);
            env.close();
        }

        {
            auto const sle = issuanceSle(env, gw, gw["USD"]);
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT((*sle)[~sfMPTokenIssuanceID] == mpt.issuanceID());

            // The MPT issuance carries the back-pointer
            auto const sleMpt = env.le(keylet::mptokenIssuance(mpt.issuanceID()));
            if (!BEAST_EXPECT(sleMpt))
                return;
            BEAST_EXPECT((*sleMpt)[~sfTokenIssuanceID] == sle->key());
        }

        // A second issuance cannot bind the same MPT
        {
            auto jv = tiCreate(gw, "EUR");
            jv[sfMaximumAmount.jsonName] = "100000";
            jv[sfTokenScale.jsonName] = 2;
            jv[sfMPTokenIssuanceID.jsonName] = to_string(mpt.issuanceID());
            env(jv, Ter(tecDUPLICATE));
        }

        // The binding is write-once
        {
            auto jv = tiSet(gw, "USD");
            jv[sfMPTokenIssuanceID.jsonName] = to_string(mpt.issuanceID());
            env(jv, Ter(tecNO_PERMISSION));
        }

        // Destroy unbinds: back-pointer is cleared
        env(tiDestroy(gw, "USD"));
        env.close();
        {
            auto const sleMpt = env.le(keylet::mptokenIssuance(mpt.issuanceID()));
            if (!BEAST_EXPECT(sleMpt))
                return;
            BEAST_EXPECT(!sleMpt->isFieldPresent(sfTokenIssuanceID));
        }
    }

    void
    testConvert(FeatureBitset features)
    {
        testcase("convert");
        using namespace test::jtx;

        Env env(*this, features);
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        auto const USD = gw["USD"];

        MPTTester mpt(env, gw, {.holders = {alice, bob, carol}});
        mpt.create({.maxAmt = 100'000, .assetScale = 2});
        MPTIssue const mptIssue{mpt.issuanceID()};

        env.trust(USD(10'000), alice, carol);
        env.close();

        // No TokenIssuance yet
        env(tiConvert(alice, USD(10)), Ter(tecOBJECT_NOT_FOUND));

        // Unbound issuance: conversion unavailable
        env(tiCreate(gw, "EUR"));
        env.close();
        env(tiConvert(alice, gw["EUR"](10)), Ter(tecNO_PERMISSION));

        {
            auto jv = tiCreate(gw, "USD");
            jv[sfMaximumAmount.jsonName] = "100000";
            jv[sfTokenScale.jsonName] = 2;
            jv[sfMPTokenIssuanceID.jsonName] = to_string(mpt.issuanceID());
            env(jv);
            env.close();
        }

        // Issue after the object exists so IssuedAmount tracks the float
        env(pay(gw, alice, USD(100.505)));
        env.close();

        // The issuer cannot convert its own token
        env(tiConvert(gw, USD(10)), Ter(temMALFORMED));

        // No MPToken for the holder yet
        env(tiConvert(alice, USD(10)), Ter(tecNO_ENTRY));

        mpt.authorize({.account = alice});
        mpt.authorize({.account = bob});
        env.close();

        // Sub-base-unit amount floors to zero units
        env(tiConvert(alice, USD(0.001)), Ter(tecPRECISION_LOSS));

        // Insufficient balance
        env(tiConvert(alice, USD(200)), Ter(tecINSUFFICIENT_FUNDS));

        // IOU -> MPT: debit exactly 10.50, mint 1050 units, dust stays
        env(tiConvert(alice, USD(10.505)));
        env.close();

        env.require(Balance(alice, USD(90.005)));
        BEAST_EXPECT(mpt.checkMPTokenAmount(alice, 1'050));
        BEAST_EXPECT(mpt.checkMPTokenOutstandingAmount(1'050));
        if (!expectIssued(env, gw, USD, Number(90'005, -3)))
            return;

        // MPT -> IOU: exact
        env(tiConvert(alice, STAmount{mptIssue, 1'000}));
        env.close();

        env.require(Balance(alice, USD(100.005)));
        BEAST_EXPECT(mpt.checkMPTokenAmount(alice, 50));
        BEAST_EXPECT(mpt.checkMPTokenOutstandingAmount(50));
        if (!expectIssued(env, gw, USD, Number(100'005, -3)))
            return;

        // More MPT than held
        env(tiConvert(alice, STAmount{mptIssue, 51}), Ter(tecINSUFFICIENT_FUNDS));

        // bob holds MPT (paid directly) but has no trust line
        mpt.pay(gw, bob, 500);
        env.close();
        env(tiConvert(bob, STAmount{mptIssue, 500}), Ter(tecNO_LINE));

        // A too-low limit blocks the credit
        env.trust(USD(1), bob);
        env.close();
        env(tiConvert(bob, STAmount{mptIssue, 500}), Ter(tecLIMIT_EXCEEDED));

        env.trust(USD(1'000), bob);
        env.close();
        env(tiConvert(bob, STAmount{mptIssue, 500}));
        env.close();
        env.require(Balance(bob, USD(5)));

        // carol has a line and IOU but no MPToken
        env(pay(gw, carol, USD(10)));
        env.close();
        env(tiConvert(carol, USD(10)), Ter(tecNO_ENTRY));

        // Locking the currency blocks conversion. Close between the failed
        // convert and the unlock so canonical-order replay cannot flip it.
        env(tiSet(gw, "USD"), Txflags(tfTokenLock));
        env.close();
        env(tiConvert(alice, USD(10)), Ter(tecLOCKED));
        env.close();
        env(tiSet(gw, "USD"), Txflags(tfTokenUnlock));
        env.close();

        // A frozen trust line blocks conversion
        env(trust(gw, USD(0), alice, tfSetFreeze));
        env.close();
        env(tiConvert(alice, USD(10)), Ter(tecFROZEN));
        env.close();
        env(trust(gw, USD(0), alice, tfClearFreeze));
        env.close();

        // Conversions are sum-neutral: they work even at a full cap.
        // Fill the cap: issued 105.01 (alice) + 0.05 (alice MPT->wait)
        // Just verify a round trip leaves the counters unchanged.
        auto const sleBefore = issuanceSle(env, gw, USD);
        if (!BEAST_EXPECT(sleBefore))
            return;
        Number const issuedBefore = (*sleBefore)[sfIssuedAmount];

        env(tiConvert(alice, USD(50)));
        env.close();
        BEAST_EXPECT(mpt.checkMPTokenAmount(alice, 5'050));
        env(tiConvert(alice, STAmount{mptIssue, 5'000}));
        env.close();
        BEAST_EXPECT(mpt.checkMPTokenAmount(alice, 50));

        auto const sleAfter = issuanceSle(env, gw, USD);
        if (!BEAST_EXPECT(sleAfter))
            return;
        Number const issuedAfter = (*sleAfter)[sfIssuedAmount];
        BEAST_EXPECT(issuedBefore == issuedAfter);
        BEAST_EXPECT(mpt.checkMPTokenAmount(alice, 50));
    }

    void
    testSharedCurrencyCode(FeatureBitset features)
    {
        testcase("shared currency code");
        using namespace test::jtx;

        // Two issuers of the same currency code: moving one issuer's token
        // must never touch the other issuer's counter.
        Env env(*this, features);
        Account const gwA{"gwA"};
        Account const gwB{"gwB"};
        Account const alice{"alice"};
        auto const aUSD = gwA["USD"];
        auto const bUSD = gwB["USD"];
        env.fund(XRP(10'000), gwA, gwB, alice);
        env.close();
        env.trust(aUSD(10'000), gwB, alice);
        env.trust(bUSD(10'000), alice);
        env.close();

        env(tiCreate(gwA, "USD"));
        {
            auto jv = tiCreate(gwB, "USD");
            jv[sfMaximumAmount.jsonName] = "1000";
            jv[sfTokenScale.jsonName] = 0;
            env(jv);
        }
        env.close();

        // gwB receiving gwA's USD affects only gwA's counter
        env(pay(gwA, gwB, aUSD(100)));
        env.close();
        if (!expectIssued(env, gwA, aUSD, Number(100)))
            return;
        if (!expectIssued(env, gwB, bUSD, Number(0)))
            return;

        // gwB issuing its own USD affects only gwB's counter
        env(pay(gwB, alice, bUSD(500)));
        env.close();
        if (!expectIssued(env, gwA, aUSD, Number(100)))
            return;
        if (!expectIssued(env, gwB, bUSD, Number(500)))
            return;

        // gwB's cap is intact: 501 more would breach it
        env(pay(gwB, alice, bUSD(501)), Ter(tecPATH_PARTIAL));
        env.close();
        env(pay(gwB, alice, bUSD(500)));
        env.close();
        if (!expectIssued(env, gwB, bUSD, Number(1000)))
            return;
    }

    void
    testBoundMptDestroy(FeatureBitset features)
    {
        testcase("bound MPT destroy");
        using namespace test::jtx;

        Env env(*this, features);
        Account const gw{"gw"};

        MPTTester mpt(env, gw, MPTInit{});
        mpt.create({.maxAmt = 100'000, .assetScale = 2});

        {
            auto jv = tiCreate(gw, "USD");
            jv[sfMaximumAmount.jsonName] = "100000";
            jv[sfTokenScale.jsonName] = 2;
            jv[sfMPTokenIssuanceID.jsonName] = to_string(mpt.issuanceID());
            env(jv);
            env.close();
        }

        // A bound MPT issuance cannot be destroyed out from under the
        // TokenIssuance.
        mpt.destroy({.err = tecHAS_OBLIGATIONS});
        env.close();

        // Destroying the TokenIssuance unbinds; then the MPT can go.
        env(tiDestroy(gw, "USD"));
        env.close();
        mpt.destroy({});
    }

    void
    testWithoutMptAmendment(FeatureBitset features)
    {
        testcase("without MPT amendment");
        using namespace test::jtx;

        Env env(*this, features - featureMPTokensV1);
        Account const gw{"gw"};
        auto const USD = gw["USD"];
        env.fund(XRP(10'000), gw);
        env.close();

        // Plain issuances work without the MPT amendment
        {
            auto jv = tiCreate(gw, "USD");
            jv[sfMaximumAmount.jsonName] = "1000";
            jv[sfTokenScale.jsonName] = 0;
            env(jv);
            env.close();
        }
        BEAST_EXPECT(issuanceSle(env, gw, USD) != nullptr);

        // Binding and conversion require it
        {
            auto jv = tiCreate(gw, "EUR");
            jv[sfMaximumAmount.jsonName] = "1000";
            jv[sfTokenScale.jsonName] = 0;
            jv[sfMPTokenIssuanceID.jsonName] = to_string(makeMptID(env.seq(gw), gw.id()));
            env(jv, Ter(temDISABLED));
        }
        env(tiConvert(gw, USD(10)), Ter(temDISABLED));
    }

    void
    testLedgerEntryRPC(FeatureBitset features)
    {
        testcase("ledger_entry");
        using namespace test::jtx;

        Env env(*this, features);
        Account const gw{"gw"};
        env.fund(XRP(10'000), gw);
        env.close();

        env(tiCreate(gw, "USD"));
        env.close();

        json::Value params;
        params[jss::token_issuance][jss::issuer] = gw.human();
        params[jss::token_issuance][jss::currency] = "USD";
        auto const jrr = env.rpc("json", "ledger_entry", to_string(params))[jss::result];
        BEAST_EXPECTS(jrr[jss::node][sfIssuer.jsonName] == gw.human(), to_string(jrr));
    }

public:
    void
    run() override
    {
        testDisabled(all_);
        testCreatePreflight(all_);
        testCreate(all_);
        testSetAndLock(all_);
        testDestroy(all_);
        testSupplyCap(all_);
        testBinding(all_);
        testConvert(all_);
        testSharedCurrencyCode(all_);
        testBoundMptDestroy(all_);
        testWithoutMptAmendment(all_);
        testLedgerEntryRPC(all_);
    }
};

BEAST_DEFINE_TESTSUITE(TokenIssuance, app, xrpl);

}  // namespace xrpl
