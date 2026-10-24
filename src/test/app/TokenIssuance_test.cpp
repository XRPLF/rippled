#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>  // IWYU pragma: keep
#include <test/jtx/check.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/offer.h>
#include <test/jtx/pay.h>
#include <test/jtx/rate.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpld/app/main/Application.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ApplyViewImpl.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/ledger/helpers/TokenIssuanceHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STCurrency.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

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
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw);
        env.close();

        BEAST_EXPECT(env.ownerCount(gw) == 0);

        // Uncapped, metadata-only issuance
        env(tiCreate(gw, "USD"));
        env.close();

        {
            auto const sle = issuanceSle(env, gw, usd);
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
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        env.trust(usd(1'000), alice, bob);
        env.close();

        // Set on a missing issuance
        env(tiSet(gw, "USD"), Ter(tecNO_ENTRY));

        env(tiCreate(gw, "USD"));
        env.close();

        env(pay(gw, alice, usd(100)));
        env.close();

        // Contradictory flags
        env(tiSet(gw, "USD"), Txflags(tfTokenLock | tfTokenUnlock), Ter(temINVALID_FLAG));
        env(tiSet(gw, "USD"), Txflags(tfTokenLock | tfTokenCannotLock), Ter(temINVALID_FLAG));

        // Lock the currency: holder-to-holder transfers stop, redemption
        // to the issuer still works (GlobalFreeze semantics, scoped).
        env(tiSet(gw, "USD"), Txflags(tfTokenLock));
        env.close();
        {
            auto const sle = issuanceSle(env, gw, usd);
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT(sle->isFlag(lsfTokenLocked));
        }

        env(pay(alice, bob, usd(10)), Ter(tecPATH_DRY));
        env.close();
        env(pay(alice, gw, usd(10)));
        env.close();
        env.require(Balance(alice, usd(90)));

        // Unlock restores transfers
        env(tiSet(gw, "USD"), Txflags(tfTokenUnlock));
        env.close();
        env(pay(alice, bob, usd(10)));
        env.close();
        env.require(Balance(bob, usd(10)));

        // Metadata and fee updates
        {
            auto jv = tiSet(gw, "USD");
            jv[sfTransferFee.jsonName] = 100;
            jv[sfMPTokenMetadata.jsonName] = strHex(std::string{"meta"});
            env(jv);
            env.close();
            auto const sle = issuanceSle(env, gw, usd);
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
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw, alice);
        env.close();
        env.trust(usd(1'000), alice);
        env.close();

        env(tiDestroy(gw, "USD"), Ter(tecNO_ENTRY));

        env(tiCreate(gw, "USD"));
        env.close();

        env(pay(gw, alice, usd(100)));
        env.close();

        if (!expectIssued(env, gw, usd, Number(100)))
            return;

        // Outstanding tokens block destruction. Close before the redemption
        // so the failed destroy is not replayed after it in canonical order.
        env(tiDestroy(gw, "USD"), Ter(tecHAS_OBLIGATIONS));
        env.close();

        env(pay(alice, gw, usd(100)));
        env.close();

        if (!expectIssued(env, gw, usd, Number(0)))
            return;

        env(tiDestroy(gw, "USD"));
        env.close();
        BEAST_EXPECT(!issuanceSle(env, gw, usd));
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
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob, carol);
        env.close();
        env.trust(usd(10'000), alice, bob, carol);
        env.close();

        {
            auto jv = tiCreate(gw, "USD");
            jv[sfMaximumAmount.jsonName] = "1000";
            jv[sfTokenScale.jsonName] = 0;
            env(jv);
            env.close();
        }

        // Issue up to 600
        env(pay(gw, alice, usd(600)));
        env.close();
        if (!expectIssued(env, gw, usd, Number(600)))
            return;

        // 500 more would breach the cap
        env(pay(gw, alice, usd(500)), Ter(tecPATH_PARTIAL));
        env.close();
        expectIssued(env, gw, usd, Number(600));

        // Redemption frees headroom
        env(pay(alice, gw, usd(100)));
        env.close();
        if (!expectIssued(env, gw, usd, Number(500)))
            return;

        // Fill the cap exactly
        env(pay(gw, alice, usd(500)));
        env.close();
        if (!expectIssued(env, gw, usd, Number(1000)))
            return;

        env(pay(gw, bob, usd(1)), Ter(tecPATH_PARTIAL));
        env.close();

        // Holder-to-holder transfers are unaffected by the cap
        env(pay(alice, bob, usd(250)));
        expectIssued(env, gw, usd, Number(1000));
        env.close();
        env.require(Balance(bob, usd(250)));
        env.require(Balance(alice, usd(750)));
        if (!expectIssued(env, gw, usd, Number(1000)))
            return;

        // An issuer's offer within the remaining headroom crosses in full
        env(pay(alice, gw, usd(400)));
        env.close();
        if (!expectIssued(env, gw, usd, Number(600)))
            return;

        env(offer(gw, XRP(300), usd(300)));
        env.close();
        env(offer(bob, usd(300), XRP(300)));
        env.close();
        env.require(Balance(bob, usd(550)));
        if (!expectIssued(env, gw, usd, Number(900)))
            return;

        // An issuer's offer beyond the headroom crosses only up to the cap
        env(offer(gw, XRP(600), usd(600)));
        env.close();
        env(offer(carol, usd(600), XRP(600)));
        env.close();
        env.require(Balance(carol, usd(100)));
        if (!expectIssued(env, gw, usd, Number(1'000)))
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
        auto const usd = gw["USD"];

        MPTTester mpt(env, gw, {.holders = {alice, bob, carol}});
        mpt.create({.maxAmt = 100'000, .assetScale = 2});
        MPTIssue const mptIssue{mpt.issuanceID()};

        env.trust(usd(10'000), alice, carol);
        env.close();

        // No TokenIssuance yet
        env(tiConvert(alice, usd(10)), Ter(tecOBJECT_NOT_FOUND));

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
        env(pay(gw, alice, usd(100.505)));
        env.close();

        // The issuer cannot convert its own token
        env(tiConvert(gw, usd(10)), Ter(temMALFORMED));

        // No MPToken for the holder yet
        env(tiConvert(alice, usd(10)), Ter(tecNO_ENTRY));

        mpt.authorize({.account = alice});
        mpt.authorize({.account = bob});
        env.close();

        // Sub-base-unit amount floors to zero units
        env(tiConvert(alice, usd(0.001)), Ter(tecPRECISION_LOSS));

        // Insufficient balance
        env(tiConvert(alice, usd(200)), Ter(tecINSUFFICIENT_FUNDS));

        // IOU -> MPT: debit exactly 10.50, mint 1050 units, dust stays
        env(tiConvert(alice, usd(10.505)));
        env.close();

        env.require(Balance(alice, usd(90.005)));
        BEAST_EXPECT(mpt.checkMPTokenAmount(alice, 1'050));
        BEAST_EXPECT(mpt.checkMPTokenOutstandingAmount(1'050));
        if (!expectIssued(env, gw, usd, Number(90'005, -3)))
            return;

        // MPT -> IOU: exact
        env(tiConvert(alice, STAmount{mptIssue, 1'000}));
        env.close();

        env.require(Balance(alice, usd(100.005)));
        BEAST_EXPECT(mpt.checkMPTokenAmount(alice, 50));
        BEAST_EXPECT(mpt.checkMPTokenOutstandingAmount(50));
        if (!expectIssued(env, gw, usd, Number(100'005, -3)))
            return;

        // More MPT than held
        env(tiConvert(alice, STAmount{mptIssue, 51}), Ter(tecINSUFFICIENT_FUNDS));

        // bob holds MPT (paid directly) but has no trust line
        mpt.pay(gw, bob, 500);
        env.close();
        env(tiConvert(bob, STAmount{mptIssue, 500}), Ter(tecNO_LINE));

        // A too-low limit blocks the credit
        env.trust(usd(1), bob);
        env.close();
        env(tiConvert(bob, STAmount{mptIssue, 500}), Ter(tecLIMIT_EXCEEDED));

        env.trust(usd(1'000), bob);
        env.close();
        env(tiConvert(bob, STAmount{mptIssue, 500}));
        env.close();
        env.require(Balance(bob, usd(5)));

        // carol has a line and IOU but no MPToken
        env(pay(gw, carol, usd(10)));
        env.close();
        env(tiConvert(carol, usd(10)), Ter(tecNO_ENTRY));

        // Locking the currency blocks conversion. Close between the failed
        // convert and the unlock so canonical-order replay cannot flip it.
        env(tiSet(gw, "USD"), Txflags(tfTokenLock));
        env.close();
        env(tiConvert(alice, usd(10)), Ter(tecLOCKED));
        env.close();
        env(tiSet(gw, "USD"), Txflags(tfTokenUnlock));
        env.close();

        // A frozen trust line blocks conversion
        env(trust(gw, usd(0), alice, tfSetFreeze));
        env.close();
        env(tiConvert(alice, usd(10)), Ter(tecFROZEN));
        env.close();
        env(trust(gw, usd(0), alice, tfClearFreeze));
        env.close();

        // Conversions are sum-neutral: they work even at a full cap.
        // Fill the cap: issued 105.01 (alice) + 0.05 (alice MPT->wait)
        // Just verify a round trip leaves the counters unchanged.
        auto const sleBefore = issuanceSle(env, gw, usd);
        if (!BEAST_EXPECT(sleBefore))
            return;
        Number const issuedBefore = (*sleBefore)[sfIssuedAmount];

        env(tiConvert(alice, usd(50)));
        env.close();
        BEAST_EXPECT(mpt.checkMPTokenAmount(alice, 5'050));
        env(tiConvert(alice, STAmount{mptIssue, 5'000}));
        env.close();
        BEAST_EXPECT(mpt.checkMPTokenAmount(alice, 50));

        auto const sleAfter = issuanceSle(env, gw, usd);
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
        auto const usd = gw["USD"];
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
        BEAST_EXPECT(issuanceSle(env, gw, usd) != nullptr);

        // Binding and conversion require it
        {
            auto jv = tiCreate(gw, "EUR");
            jv[sfMaximumAmount.jsonName] = "1000";
            jv[sfTokenScale.jsonName] = 0;
            jv[sfMPTokenIssuanceID.jsonName] = to_string(makeMptID(env.seq(gw), gw.id()));
            env(jv, Ter(temDISABLED));
        }
        {
            auto jv = tiSet(gw, "USD");
            jv[sfMPTokenIssuanceID.jsonName] = to_string(makeMptID(env.seq(gw), gw.id()));
            env(jv, Ter(temDISABLED));
        }
        env(tiConvert(gw, usd(10)), Ter(temDISABLED));
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

        // The same entry by its index
        {
            json::Value byIndex;
            byIndex[jss::token_issuance] =
                to_string(keylet::tokenIssuance(gw.id(), gw["USD"].currency).key);
            auto const result = env.rpc("json", "ledger_entry", to_string(byIndex))[jss::result];
            BEAST_EXPECTS(result[jss::node][sfIssuer.jsonName] == gw.human(), to_string(result));
        }

        auto const expectError = [&](json::Value const& field, std::string const& error) {
            json::Value request;
            request[jss::token_issuance] = field;
            auto const result = env.rpc("json", "ledger_entry", to_string(request))[jss::result];
            BEAST_EXPECTS(result[jss::error] == error, to_string(result));
        };

        {
            json::Value field;
            field[jss::issuer] = gw.human();
            expectError(field, "malformedRequest");
        }
        {
            json::Value field;
            field[jss::issuer] = "not an account";
            field[jss::currency] = "USD";
            expectError(field, "malformedAddress");
        }
        {
            json::Value field;
            field[jss::issuer] = gw.human();
            field[jss::currency] = 5;
            expectError(field, "malformedCurrency");
        }
        {
            json::Value field;
            field[jss::issuer] = gw.human();
            field[jss::currency] = "";
            expectError(field, "malformedCurrency");
        }
        {
            json::Value field;
            field[jss::issuer] = gw.human();
            field[jss::currency] = "not a currency";
            expectError(field, "malformedCurrency");
        }
    }

    void
    testCreateFields(FeatureBitset features)
    {
        testcase("create fields");
        using namespace test::jtx;

        Env env(*this, features);
        Account const gw{"gw"};
        Account const poor{"poor"};
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw);
        env.fund(env.current()->fees().accountReserve(0, 1), poor);
        env.close();

        // The reserved non-standard XRP code is not an issuance currency. JSON
        // refuses the code, so the transaction goes in signed binary form.
        {
            auto const jt = env.jt(tiCreate(gw, "USD"));
            STTx tx{STObject{*jt.stx}};
            tx.setFieldCurrency(sfCurrency, STCurrency{sfCurrency, badCurrency()});
            tx.sign(gw.pk(), gw.sk());
            Serializer s;
            tx.add(s);
            auto const jrr = env.rpc("submit", strHex(s.slice()))[jss::result];
            BEAST_EXPECTS(jrr[jss::engine_result] == "temBAD_CURRENCY", to_string(jrr));
        }

        // Metadata over the length limit
        {
            auto jv = tiCreate(gw, "USD");
            jv[sfMPTokenMetadata.jsonName] =
                strHex(std::string(kMaxMpTokenMetadataLength + 1, 'a'));
            env(jv, Ter(temMALFORMED));
        }

        // A binding counts in base units, so it needs the scale
        {
            auto jv = tiCreate(gw, "USD");
            jv[sfMPTokenIssuanceID.jsonName] = to_string(makeMptID(1, gw.id()));
            env(jv, Ter(temMALFORMED));
        }

        // The issuance takes an owner reserve
        env(tiCreate(poor, "USD"), Ter(tecINSUFFICIENT_RESERVE));
        env.close();
        BEAST_EXPECT(!issuanceSle(env, poor, poor["USD"]));

        // The fee and metadata given at creation are stored
        {
            auto jv = tiCreate(gw, "USD");
            jv[sfTransferFee.jsonName] = 1'000;
            jv[sfMPTokenMetadata.jsonName] = strHex(std::string{"meta"});
            env(jv);
            env.close();
        }
        auto const sle = issuanceSle(env, gw, usd);
        if (!BEAST_EXPECT(sle))
            return;
        BEAST_EXPECT((*sle)[sfTransferFee] == 1'000);
        BEAST_EXPECT(strHex((*sle)[sfMPTokenMetadata]) == strHex(std::string{"meta"}));
    }

    void
    testSetFields(FeatureBitset features)
    {
        testcase("set fields and late binding");
        using namespace test::jtx;

        Env env(*this, features);
        Account const gw{"gw"};
        auto const usd = gw["USD"];

        MPTTester mpt(env, gw, MPTInit{});
        mpt.create({.maxAmt = 100'000, .assetScale = 2});

        {
            auto jv = tiCreate(gw, "USD");
            jv[sfMaximumAmount.jsonName] = "100000";
            jv[sfTokenScale.jsonName] = 2;
            env(jv);
            env.close();
        }

        {
            auto jv = tiSet(gw, "USD");
            jv[sfTransferFee.jsonName] = 50'001;
            env(jv, Ter(temBAD_TRANSFER_FEE));
        }
        {
            auto jv = tiSet(gw, "USD");
            jv[sfMPTokenMetadata.jsonName] = "";
            env(jv, Ter(temMALFORMED));
        }
        {
            auto jv = tiSet(gw, "USD");
            jv[sfMPTokenMetadata.jsonName] =
                strHex(std::string(kMaxMpTokenMetadataLength + 1, 'a'));
            env(jv, Ter(temMALFORMED));
        }

        // An unbound issuance can be bound later, once
        {
            auto jv = tiSet(gw, "USD");
            jv[sfMPTokenIssuanceID.jsonName] = to_string(mpt.issuanceID());
            env(jv);
            env.close();
        }
        {
            auto const sle = issuanceSle(env, gw, usd);
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT((*sle)[~sfMPTokenIssuanceID] == mpt.issuanceID());
            auto const sleMpt = env.le(keylet::mptokenIssuance(mpt.issuanceID()));
            if (!BEAST_EXPECT(sleMpt))
                return;
            BEAST_EXPECT((*sleMpt)[~sfTokenIssuanceID] == sle->key());
        }

        // A bound issuance keeps its mutable fields mutable
        {
            auto jv = tiSet(gw, "USD");
            jv[sfTransferFee.jsonName] = 10;
            env(jv);
            env.close();
        }
        auto const sle = issuanceSle(env, gw, usd);
        if (!BEAST_EXPECT(sle))
            return;
        BEAST_EXPECT((*sle)[sfTransferFee] == 10);
        BEAST_EXPECT((*sle)[~sfMPTokenIssuanceID] == mpt.issuanceID());
    }

    void
    testDestroyBoundOutstanding(FeatureBitset features)
    {
        testcase("destroy with bound MPT outstanding");
        using namespace test::jtx;

        Env env(*this, features);
        Account const gw{"gw"};
        Account const alice{"alice"};

        MPTTester mpt(env, gw, {.holders = {alice}});
        mpt.create({.maxAmt = 100'000, .assetScale = 2});
        {
            auto jv = tiCreate(gw, "USD");
            jv[sfMaximumAmount.jsonName] = "100000";
            jv[sfTokenScale.jsonName] = 2;
            jv[sfMPTokenIssuanceID.jsonName] = to_string(mpt.issuanceID());
            env(jv);
            env.close();
        }
        mpt.authorize({.account = alice});
        mpt.pay(gw, alice, 100);
        env.close();

        // The MPT side of the shared supply is outstanding
        env(tiDestroy(gw, "USD"), Ter(tecHAS_OBLIGATIONS));
        env.close();

        mpt.pay(alice, gw, 100);
        env.close();
        env(tiDestroy(gw, "USD"));
        env.close();
        BEAST_EXPECT(!issuanceSle(env, gw, gw["USD"]));
    }

    void
    testConvertErrors(FeatureBitset features)
    {
        testcase("convert errors");
        using namespace test::jtx;

        Env env(*this, features);
        Account const gw{"gw"};
        Account const alice{"alice"};
        env.fund(XRP(10'000), gw, alice);
        env.close();

        auto const bind = [&](std::string const& code, MPTTester const& mpt, std::uint8_t scale) {
            auto jv = tiCreate(gw, code);
            jv[sfMaximumAmount.jsonName] = "100000";
            jv[sfTokenScale.jsonName] = scale;
            jv[sfMPTokenIssuanceID.jsonName] = to_string(mpt.issuanceID());
            env(jv);
            env.close();
        };
        auto const holdIou = [&](IOU const& iou) {
            env.trust(iou(1'000), alice);
            env(pay(gw, alice, iou(100)));
            env.close();
        };

        env(tiConvert(alice, XRP(10)), Ter(temBAD_AMOUNT));
        env(tiConvert(alice, gw["USD"](-10)), Ter(temBAD_AMOUNT));

        // An MPT issuance that does not exist
        env(tiConvert(alice, STAmount{MPTIssue{makeMptID(env.seq(gw) + 100, gw.id())}, 10}),
            Ter(tecOBJECT_NOT_FOUND));

        // An MPT issuance no TokenIssuance is bound to
        MPTTester unbound(env, gw, {.holders = {alice}, .fund = false});
        unbound.create({.maxAmt = 100'000, .assetScale = 2});
        env(tiConvert(alice, STAmount{MPTIssue{unbound.issuanceID()}, 10}), Ter(tecNO_PERMISSION));

        // A holder whose MPToken is locked
        MPTTester lockable(env, gw, {.holders = {alice}, .fund = false});
        lockable.create({.maxAmt = 100'000, .assetScale = 2, .flags = tfMPTCanLock});
        bind("EUR", lockable, 2);
        holdIou(gw["EUR"]);
        lockable.authorize({.account = alice});
        lockable.set({.holder = alice, .flags = tfMPTLock});
        env(tiConvert(alice, gw["EUR"](1)), Ter(tecLOCKED));

        // A holder the issuer has not authorized
        MPTTester gated(env, gw, {.holders = {alice}, .fund = false});
        gated.create({.maxAmt = 100'000, .assetScale = 2, .flags = tfMPTRequireAuth});
        bind("GBP", gated, 2);
        holdIou(gw["GBP"]);
        gated.authorize({.account = alice});
        env(tiConvert(alice, gw["GBP"](1)), Ter(tecNO_AUTH));
    }

    void
    testConvertUncapped(FeatureBitset features)
    {
        testcase("convert uncapped binding");
        using namespace test::jtx;

        Env env(*this, features);
        Account const gw{"gw"};
        auto const jpy = gw["JPY"];
        env.fund(XRP(10'000), gw);
        env.close();

        // One holder on each side of the issuer in trust-line order
        auto const pick = [&](bool low) {
            for (auto const* name : {"alice", "bob", "carol", "dan", "eve", "frank", "grace"})
            {
                Account const candidate{name};
                if ((candidate.id() < gw.id()) == low)
                    return candidate;
            }
            return Account{"alice"};
        };
        Account const lowHolder = pick(true);
        Account const highHolder = pick(false);
        BEAST_EXPECT(lowHolder.id() < gw.id());
        BEAST_EXPECT(gw.id() < highHolder.id());
        env.fund(XRP(10'000), lowHolder, highHolder);
        env.close();

        MPTTester mpt(env, gw, {.holders = {lowHolder, highHolder}, .fund = false});
        mpt.create({.assetScale = 18});
        MPTIssue const mptIssue{mpt.issuanceID()};
        {
            auto jv = tiCreate(gw, "JPY");
            jv[sfTokenScale.jsonName] = 18;
            jv[sfMPTokenIssuanceID.jsonName] = to_string(mpt.issuanceID());
            env(jv);
            env.close();
        }

        for (auto const& holder : {lowHolder, highHolder})
        {
            env.trust(jpy(1'000), holder);
            env(pay(gw, holder, jpy(100)));
            mpt.authorize({.account = holder});
            env.close();

            // 10 JPY is 10^19 base units, past what the conversion represents
            env(tiConvert(holder, jpy(10)), Ter(tecPRECISION_LOSS));

            // No maximum on either side: the conversion is unbounded
            env(tiConvert(holder, jpy(0.5)));
            env.close();
            BEAST_EXPECT(mpt.checkMPTokenAmount(holder, 500'000'000'000'000'000));
            env.require(Balance(holder, jpy(99.5)));

            env(tiConvert(holder, STAmount{mptIssue, std::int64_t{250'000'000'000'000'000}}));
            env.close();
            BEAST_EXPECT(mpt.checkMPTokenAmount(holder, 250'000'000'000'000'000));
            env.require(Balance(holder, jpy(99.75)));
        }
    }

    void
    testTransferFee(FeatureBitset features)
    {
        testcase("transfer fee");
        using namespace test::jtx;

        Env env(*this, features);
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        env.fund(XRP(10'000), gw, alice, bob, carol);
        env.close();
        env.trust(usd(1'000), alice, bob, carol);
        env.trust(eur(1'000), alice, bob);
        env.close();

        // 1% on USD through the issuance, 2% account-wide
        {
            auto jv = tiCreate(gw, "USD");
            jv[sfTransferFee.jsonName] = 1'000;
            env(jv);
        }
        env(tiCreate(gw, "EUR"));
        env(rate(gw, 1.02));
        env.close();

        env(pay(gw, alice, usd(200)));
        env(pay(gw, alice, eur(200)));
        env.close();

        // The issuance's fee overrides the account-wide rate
        env(pay(alice, bob, usd(100)), Sendmax(usd(200)));
        env.close();
        env.require(Balance(alice, usd(99)));
        env.require(Balance(bob, usd(100)));

        // Without a fee on the issuance the account-wide rate applies
        env(pay(alice, bob, eur(100)), Sendmax(eur(200)));
        env.close();
        env.require(Balance(alice, eur(98)));
        env.require(Balance(bob, eur(100)));

        // A multi-recipient send charges the issuance's fee too
        ApplyViewImpl av(&*env.current(), TapNone);
        auto const j = env.app().getJournal("View");
        BEAST_EXPECT(
            accountSendMulti(
                av,
                alice.id(),
                Asset{usd.issue()},
                {{bob.id(), Number(10)}, {carol.id(), Number(10)}},
                j) == tesSUCCESS);
        BEAST_EXPECT(
            accountHolds(av, alice.id(), usd.currency, gw.id(), FreezeHandling::IgnoreFreeze, j) ==
            usd(78.8).value());
    }

    void
    testDisabledPaths(FeatureBitset features)
    {
        testcase("payments without the amendment");
        using namespace test::jtx;

        Env env(*this, features - featureTokenIssuance);
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        env.trust(usd(1'000), alice, bob);
        env(rate(gw, 1.01));
        env.close();

        env(pay(gw, alice, usd(200)));
        env(pay(alice, bob, usd(100)), Sendmax(usd(200)));
        env.close();
        env.require(Balance(alice, usd(99)));
        env.require(Balance(bob, usd(100)));

        MPTTester mpt(env, gw, {.holders = {alice}, .fund = false});
        mpt.create({.maxAmt = 1'000});
        mpt.authorize({.account = alice});
        mpt.pay(gw, alice, 100);
        env.close();
        BEAST_EXPECT(mpt.checkMPTokenAmount(alice, 100));

        // A holder-to-holder send outside the payment engine charges the rate
        ApplyViewImpl av(&*env.current(), TapNone);
        auto const j = env.app().getJournal("View");
        BEAST_EXPECT(accountSend(av, alice.id(), bob.id(), usd(10), j) == tesSUCCESS);
        BEAST_EXPECT(
            accountHolds(av, alice.id(), usd.currency, gw.id(), FreezeHandling::IgnoreFreeze, j) ==
            usd(88.9).value());
    }

    void
    testLockedSurfaces(FeatureBitset features)
    {
        testcase("lock in offers and order books");
        using namespace test::jtx;

        Env env(*this, features);
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        env.trust(usd(1'000), alice, bob);
        env(tiCreate(gw, "USD"));
        env.close();
        env(pay(gw, alice, usd(100)));
        env(pay(gw, bob, usd(100)));
        env(offer(bob, XRP(10), usd(10)));
        auto const checkId = keylet::check(gw.id(), SeqProxy::rawSequence(env.seq(gw))).key;
        env(check::create(gw, alice, usd(10)));
        env.close();

        auto const bookOffers = [&] {
            json::Value params;
            params[jss::taker_gets][jss::currency] = "USD";
            params[jss::taker_gets][jss::issuer] = gw.human();
            params[jss::taker_pays][jss::currency] = "XRP";
            return env.rpc("json", "book_offers", to_string(params))[jss::result];
        };

        {
            auto const jrr = bookOffers();
            if (BEAST_EXPECTS(jrr[jss::offers].size() == 1, to_string(jrr)))
                BEAST_EXPECT(!jrr[jss::offers][0u].isMember(jss::taker_gets_funded));
        }

        env(tiSet(gw, "USD"), Txflags(tfTokenLock));
        env.close();

        // A locked currency backs no offer in the book
        {
            auto const jrr = bookOffers();
            if (BEAST_EXPECTS(jrr[jss::offers].size() == 1, to_string(jrr)))
            {
                BEAST_EXPECTS(
                    jrr[jss::offers][0u][jss::taker_gets_funded][jss::value] == "0",
                    to_string(jrr));
            }
        }

        // and a holder cannot offer it
        env(offer(alice, XRP(10), usd(10)), Ter(tecFROZEN));

        // nor take it in from the issuer's check
        env(check::cash(alice, checkId, usd(10)), Ter(tecFROZEN));
    }

    void
    testRedeemUnderFullCap(FeatureBitset features)
    {
        testcase("redeem under a full cap");
        using namespace test::jtx;

        Env env(*this, features);
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const carol{"carol"};
        auto const usd = gw["USD"];
        auto const aliceUsd = alice["USD"];
        env.fund(XRP(10'000), gw, alice, carol);
        env.close();
        env.trust(usd(1'000), carol);
        env.trust(aliceUsd(100), gw);
        env.close();
        {
            auto jv = tiCreate(gw, "USD");
            jv[sfMaximumAmount.jsonName] = "100";
            jv[sfTokenScale.jsonName] = 0;
            env(jv);
            env.close();
        }

        // The cap is full, and gw holds 50 of alice's own USD
        env(pay(gw, carol, usd(100)));
        env(pay(alice, gw, aliceUsd(50)));
        env.close();
        if (!expectIssued(env, gw, usd, Number(100)))
            return;

        // Returning alice's USD to her redeems her obligation and issues none
        // of gw's, so the cap does not bound it
        env(pay(gw, alice, aliceUsd(30)));
        env.close();
        env.require(Balance(gw, aliceUsd(20)));
        expectIssued(env, gw, usd, Number(100));
    }

    void
    testBoundMptCapDirect(FeatureBitset features)
    {
        testcase("bound MPT cap on a direct MPT payment");
        using namespace test::jtx;

        // Without MPTokensV2 an MPT payment moves the MPT directly
        Env env(*this, features - featureMPTokensV2);
        Account const gw{"gw"};
        Account const alice{"alice"};
        auto const usd = gw["USD"];

        MPTTester mpt(env, gw, {.holders = {alice}});
        mpt.create({.maxAmt = 1'000, .assetScale = 0});
        {
            auto jv = tiCreate(gw, "USD");
            jv[sfMaximumAmount.jsonName] = "1000";
            jv[sfTokenScale.jsonName] = 0;
            jv[sfMPTokenIssuanceID.jsonName] = to_string(mpt.issuanceID());
            env(jv);
            env.close();
        }
        env.trust(usd(1'000), alice);
        env(pay(gw, alice, usd(600)));
        mpt.authorize({.account = alice});
        env.close();

        // 400 of the shared 1000 remain
        mpt.pay(gw, alice, 500, tecSUPPLY_EXCEEDED);
        env.close();
        mpt.pay(gw, alice, 400);
        env.close();
        BEAST_EXPECT(mpt.checkMPTokenOutstandingAmount(400));
    }

    void
    testHelpers(FeatureBitset features)
    {
        testcase("supply helpers");
        using namespace test::jtx;

        Env env(*this, features);
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        env.trust(usd(10'000), alice, bob);
        env.close();
        {
            auto jv = tiCreate(gw, "USD");
            jv[sfMaximumAmount.jsonName] = "1000";
            jv[sfTokenScale.jsonName] = 0;
            env(jv);
            env.close();
        }
        env(pay(gw, alice, usd(100)));
        env.close();

        MPTTester mpt(env, gw, {.holders = {alice}, .fund = false});
        mpt.create({.maxAmt = 1'000, .assetScale = 0});
        {
            auto jv = tiCreate(gw, "EUR");
            jv[sfMaximumAmount.jsonName] = "1000";
            jv[sfTokenScale.jsonName] = 0;
            jv[sfMPTokenIssuanceID.jsonName] = to_string(mpt.issuanceID());
            env(jv);
            env.close();
        }

        auto const j = env.app().getJournal("View");
        auto const usdKey = keylet::tokenIssuance(gw.id(), usd.currency);
        auto const eurKey = keylet::tokenIssuance(gw.id(), eur.currency);
        auto const issued = [&](ReadView const& view) {
            auto const sle = view.read(usdKey);
            return sle ? Number{(*sle)[sfIssuedAmount]} : Number{-1};
        };

        // Base units
        BEAST_EXPECT(!tokenBaseUnits(Number(-1), 0));
        BEAST_EXPECT(!tokenBaseUnits(Number(1), 19));
        BEAST_EXPECT(tokenBaseUnits(Number(15, -1), 1) == 15);

        // Only an IOU move that involves its issuer changes the supply
        {
            ApplyViewImpl av(&*env.current(), TapNone);
            BEAST_EXPECT(
                adjustTokenIssuance(av, gw.id(), alice.id(), XRP(10), EnforceSupplyCap::Yes, j) ==
                tesSUCCESS);
            BEAST_EXPECT(
                adjustTokenIssuance(
                    av,
                    gw.id(),
                    alice.id(),
                    STAmount{MPTIssue{mpt.issuanceID()}, 10},
                    EnforceSupplyCap::Yes,
                    j) == tesSUCCESS);
            BEAST_EXPECT(
                adjustTokenIssuance(av, gw.id(), alice.id(), usd(0), EnforceSupplyCap::Yes, j) ==
                tesSUCCESS);
            BEAST_EXPECT(
                adjustTokenIssuance(av, alice.id(), bob.id(), usd(10), EnforceSupplyCap::Yes, j) ==
                tesSUCCESS);
            BEAST_EXPECT(issued(av) == Number(100));
        }

        // A negative move is the reverse move
        {
            ApplyViewImpl av(&*env.current(), TapNone);
            BEAST_EXPECT(
                adjustTokenIssuance(av, alice.id(), gw.id(), usd(-10), EnforceSupplyCap::Yes, j) ==
                tesSUCCESS);
            BEAST_EXPECT(issued(av) == Number(110));
        }

        // The cap holds unless the caller defers it
        {
            ApplyViewImpl av(&*env.current(), TapNone);
            BEAST_EXPECT(
                adjustTokenIssuance(av, gw.id(), alice.id(), usd(901), EnforceSupplyCap::Yes, j) ==
                tecSUPPLY_EXCEEDED);
            BEAST_EXPECT(issued(av) == Number(100));
            BEAST_EXPECT(
                adjustTokenIssuance(av, gw.id(), alice.id(), usd(901), EnforceSupplyCap::No, j) ==
                tesSUCCESS);
            BEAST_EXPECT(issued(av) == Number(1'001));
            BEAST_EXPECT(tokenSupplyExceeded(av, av.read(usdKey)));
        }

        // Every issuer-side credit enforces the cap; redemption never fails
        {
            ApplyViewImpl av(&*env.current(), TapNone);
            BEAST_EXPECT(accountSend(av, gw.id(), alice.id(), usd(901), j) == tecSUPPLY_EXCEEDED);
            BEAST_EXPECT(
                issueIOU(av, alice.id(), usd(901), usd.issue(), {}, j) == tecSUPPLY_EXCEEDED);
            BEAST_EXPECT(redeemIOU(av, alice.id(), usd(50), usd.issue(), j) == tesSUCCESS);
            BEAST_EXPECT(issued(av) == Number(50));
        }

        // Bound issuance: the IOU side can fill the shared cap
        {
            ApplyViewImpl av(&*env.current(), TapNone);
            auto const sleMpt = av.read(keylet::mptokenIssuance(mpt.issuanceID()));
            if (!BEAST_EXPECT(sleMpt))
                return;
            BEAST_EXPECT(mptBoundHeadroom(av, sleMpt) == 1'000);
            BEAST_EXPECT(
                adjustTokenIssuance(av, gw.id(), alice.id(), eur(1'000), EnforceSupplyCap::No, j) ==
                tesSUCCESS);
            BEAST_EXPECT(mptBoundHeadroom(av, sleMpt) == 0);
        }

        // Bound issuance: the MPT side at and past the shared cap
        {
            ApplyViewImpl av(&*env.current(), TapNone);
            auto const sleMpt = av.peek(keylet::mptokenIssuance(mpt.issuanceID()));
            if (!BEAST_EXPECT(sleMpt))
                return;
            (*sleMpt)[sfOutstandingAmount] = 1'000;
            av.update(sleMpt);
            BEAST_EXPECT(mptBoundHeadroom(av, sleMpt) == 0);

            (*sleMpt)[sfOutstandingAmount] = 1'001;
            av.update(sleMpt);
            BEAST_EXPECT(tokenSupplyExceeded(av, av.read(eurKey)));
            auto const headroom = tokenIssuanceHeadroom(av, eur.issue());
            BEAST_EXPECT(headroom && *headroom == STAmount{eur.issue()});
        }

        // Dangling references count nothing on the missing side
        {
            ApplyViewImpl av(&*env.current(), TapNone);
            auto const sleMpt = av.peek(keylet::mptokenIssuance(mpt.issuanceID()));
            auto const sleTi = av.peek(eurKey);
            if (!BEAST_EXPECT(sleMpt && sleTi))
                return;
            (*sleMpt)[sfTokenIssuanceID] = keylet::tokenIssuance(gw.id(), gw["GBP"].currency).key;
            av.update(sleMpt);
            BEAST_EXPECT(!mptBoundHeadroom(av, sleMpt));

            (*sleTi)[sfMPTokenIssuanceID] = makeMptID(env.seq(gw) + 100, gw.id());
            av.update(sleTi);
            BEAST_EXPECT(!tokenSupplyExceeded(av, av.read(eurKey)));
            auto const headroom = tokenIssuanceHeadroom(av, eur.issue());
            BEAST_EXPECT(headroom && *headroom == eur(1'000).value());
        }
    }

    void
    testConvertHonoursRequireAuth(FeatureBitset features)
    {
        testcase("convert honours RequireAuth on the trust line");
        using namespace test::jtx;

        Env env(*this, features);
        Account const gw{"gw"};
        Account const alice{"alice"};
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw, alice);
        env.close();
        env(fset(gw, asfRequireAuth));
        env.close();

        MPTTester mpt(env, gw, {.holders = {alice}, .fund = false});
        mpt.create({.maxAmt = 100'000, .assetScale = 2});
        MPTIssue const mptIssue{mpt.issuanceID()};
        {
            auto jv = tiCreate(gw, "USD");
            jv[sfMaximumAmount.jsonName] = "100000";
            jv[sfTokenScale.jsonName] = 2;
            jv[sfMPTokenIssuanceID.jsonName] = to_string(mpt.issuanceID());
            env(jv);
            env.close();
        }
        mpt.authorize({.account = alice});
        mpt.pay(gw, alice, 500);
        env(trust(alice, usd(1'000)));
        env.close();

        // The issuer has not authorized alice's line
        env(tiConvert(alice, STAmount{mptIssue, 500}), Ter(tecNO_AUTH));
        env.close();
        env.require(Balance(alice, usd(0)));
        BEAST_EXPECT(mpt.checkMPTokenAmount(alice, 500));

        env(trust(gw, alice["USD"](0), tfSetfAuth));
        env.close();
        env(tiConvert(alice, STAmount{mptIssue, 500}));
        env.close();
        env.require(Balance(alice, usd(5)));
        BEAST_EXPECT(mpt.checkMPTokenAmount(alice, 0));
    }

    void
    testNoFreezeIssuerCannotLock(FeatureBitset features)
    {
        testcase("an issuer that renounced freezing cannot lock");
        using namespace test::jtx;

        Env env(*this, features);
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        auto const usd = gw["USD"];
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        env(fset(gw, asfNoFreeze));
        env.trust(usd(1'000), alice, bob);
        env.close();
        env(tiCreate(gw, "USD"));
        env.close();
        env(pay(gw, alice, usd(100)));
        env.close();

        env(tiSet(gw, "USD"), Txflags(tfTokenLock), Ter(tecNO_PERMISSION));
        env.close();
        auto const sle = issuanceSle(env, gw, usd);
        if (!BEAST_EXPECT(sle))
            return;
        BEAST_EXPECT(!sle->isFlag(lsfTokenLocked));

        env(pay(alice, bob, usd(10)));
        env.close();
        env.require(Balance(bob, usd(10)));
    }

    void
    testConvertSettlingOwnDebt(FeatureBitset features)
    {
        testcase("convert that settles the holder's own token issues nothing");
        using namespace test::jtx;

        Env env(*this, features);
        Account const gw{"gw"};
        Account const alice{"alice"};
        auto const usd = gw["USD"];

        MPTTester mpt(env, gw, {.holders = {alice}});
        mpt.create({.maxAmt = 100'000, .assetScale = 2});
        MPTIssue const mptIssue{mpt.issuanceID()};
        {
            auto jv = tiCreate(gw, "USD");
            jv[sfMaximumAmount.jsonName] = "100000";
            jv[sfTokenScale.jsonName] = 2;
            jv[sfMPTokenIssuanceID.jsonName] = to_string(mpt.issuanceID());
            env(jv);
            env.close();
        }
        env.trust(usd(1'000), alice);
        env(trust(gw, alice["USD"](1'000)));
        env.close();
        mpt.authorize({.account = alice});
        mpt.pay(gw, alice, 500);
        env.close();

        // gw holds 5 of alice's own USD on their line
        env(pay(alice, gw, alice["USD"](5)));
        env.close();
        if (!expectIssued(env, gw, usd, Number{}))
            return;

        // The 5 USD credited only settles that, so none of gw's USD is issued
        env(tiConvert(alice, STAmount{mptIssue, 500}));
        env.close();
        env.require(Balance(alice, usd(0)));
        expectIssued(env, gw, usd, Number{});

        // Past zero the credit is issuance again
        mpt.pay(gw, alice, 300);
        env.close();
        env(tiConvert(alice, STAmount{mptIssue, 300}));
        env.close();
        env.require(Balance(alice, usd(3)));
        expectIssued(env, gw, usd, Number(3));
    }

    void
    testConvertExactness(FeatureBitset features)
    {
        testcase("convert moves exactly the units it burns or mints");
        using namespace test::jtx;

        Env env(*this, features);
        Account const gw{"gw"};
        Account const alice{"alice"};
        auto const usd = gw["USD"];
        auto const jpy = gw["JPY"];

        // Scale 0: MPT units past 16 significant digits
        MPTTester wide(env, gw, {.holders = {alice}});
        wide.create();
        MPTIssue const wideIssue{wide.issuanceID()};
        {
            auto jv = tiCreate(gw, "USD");
            jv[sfTokenScale.jsonName] = 0;
            jv[sfMPTokenIssuanceID.jsonName] = to_string(wide.issuanceID());
            env(jv);
            env.close();
        }
        env.trust(usd(std::int64_t{100'000'000'000'000'000}), alice);
        wide.authorize({.account = alice});
        env.close();
        env(pay(gw, alice, usd(std::int64_t{10'000'000'000'000'000})));
        env.close();
        env(tiConvert(alice, usd(std::int64_t{10'000'000'000'000'000})));
        env(pay(gw, alice, usd(std::int64_t{6})));
        env.close();
        env(tiConvert(alice, usd(std::int64_t{6})));
        env.close();
        BEAST_EXPECT(wide.getBalance(alice) == 10'000'000'000'000'006);

        // An IOU amount that cannot carry every unit is refused
        env(tiConvert(alice, STAmount{wideIssue, std::int64_t{10'000'000'000'000'006}}),
            Ter(tecPRECISION_LOSS));
        env.close();
        BEAST_EXPECT(wide.getBalance(alice) == 10'000'000'000'000'006);
        env.require(Balance(alice, usd(0)));

        // Scale 18: a debit below the last place of a large balance is refused
        MPTTester fine(env, gw, {.holders = {alice}, .fund = false});
        fine.create({.assetScale = 18});
        {
            auto jv = tiCreate(gw, "JPY");
            jv[sfTokenScale.jsonName] = 18;
            jv[sfMPTokenIssuanceID.jsonName] = to_string(fine.issuanceID());
            env(jv);
            env.close();
        }
        env.trust(jpy(1'000), alice);
        env(pay(gw, alice, jpy(100)));
        fine.authorize({.account = alice});
        env.close();

        env(tiConvert(alice, STAmount{jpy.issue(), std::uint64_t{1}, -18}), Ter(tecPRECISION_LOSS));
        env.close();
        env.require(Balance(alice, jpy(100)));
        BEAST_EXPECT(fine.getBalance(alice) == 0);
    }

    void
    testFieldsBeforeAmendment(FeatureBitset features)
    {
        testcase("amendment fields are refused before the amendment");
        using namespace test::jtx;

        Env env(*this, features - featureTokenIssuance);
        Account const alice{"alice"};
        env.fund(XRP(10'000), alice);
        env.close();
        env.close(env.timeKeeper().now() + std::chrono::seconds(10'000));

        // An OracleSet whose price data element has no inner template
        auto const oracleSet = [&](bool withScaleField) {
            json::Value element;
            element[sfMemo.jsonName][jss::BaseAsset] = "XRP";
            element[sfMemo.jsonName][jss::QuoteAsset] = "USD";
            element[sfMemo.jsonName][jss::AssetPrice] = 740;
            element[sfMemo.jsonName][jss::Scale] = 1;
            if (withScaleField)
                element[sfMemo.jsonName][sfTokenScale.jsonName] = 2;
            json::Value jv;
            jv[jss::TransactionType] = jss::OracleSet;
            jv[jss::Account] = alice.human();
            jv[jss::OracleDocumentID] = 1;
            jv[jss::Provider] = strHex(std::string("provider"));
            jv[jss::AssetClass] = strHex(std::string("currency"));
            jv[jss::LastUpdateTime] = std::to_string(
                std::chrono::duration_cast<std::chrono::seconds>(
                    env.current()->header().closeTime.time_since_epoch())
                    .count() +
                kEpochOffset.count());
            jv[jss::PriceDataSeries] = json::Value(json::ValueType::Array);
            jv[jss::PriceDataSeries].append(element);
            return jv;
        };

        env(oracleSet(true), Ter(temDISABLED));
        env.close();
        BEAST_EXPECT(!env.le(keylet::oracle(alice.id(), 1)));
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
        testCreateFields(all_);
        testSetFields(all_);
        testDestroyBoundOutstanding(all_);
        testConvertErrors(all_);
        testConvertUncapped(all_);
        testTransferFee(all_);
        testDisabledPaths(all_);
        testLockedSurfaces(all_);
        testRedeemUnderFullCap(all_);
        testBoundMptCapDirect(all_);
        testHelpers(all_);
        testConvertHonoursRequireAuth(all_);
        testNoFreezeIssuerCannotLock(all_);
        testConvertSettlingOwnDebt(all_);
        testConvertExactness(all_);
        testFieldsBeforeAmendment(all_);
    }
};

BEAST_DEFINE_TESTSUITE(TokenIssuance, app, xrpl);

}  // namespace xrpl
