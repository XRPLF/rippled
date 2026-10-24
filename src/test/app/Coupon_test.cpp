#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/delegate.h>
#include <test/jtx/escrow.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace xrpl {

class Coupon_test : public beast::unit_test::Suite
{
    using MPTTester = test::jtx::MPTTester;

    static json::Value
    scheduleCreate(
        test::jtx::Account const& admin,
        MPTID const& issuanceID,
        Asset const& couponAsset)
    {
        json::Value jv;
        jv[jss::TransactionType] = jss::CouponScheduleCreate;
        jv[jss::Account] = admin.human();
        jv[sfMPTokenIssuanceID] = to_string(issuanceID);
        jv[sfCouponAsset] = toJson(couponAsset);
        return jv;
    }

    static json::Value
    couponPay(test::jtx::Account const& payer, MPTID const& issuanceID, STAmount const& perUnit)
    {
        json::Value jv;
        jv[jss::TransactionType] = jss::CouponPay;
        jv[jss::Account] = payer.human();
        jv[sfMPTokenIssuanceID] = to_string(issuanceID);
        jv[sfAmount] = toJson(perUnit);
        return jv;
    }

    static json::Value
    couponClaim(
        test::jtx::Account const& holder,
        MPTID const& issuanceID,
        std::optional<STAmount> const& amount = std::nullopt)
    {
        json::Value jv;
        jv[jss::TransactionType] = jss::CouponClaim;
        jv[jss::Account] = holder.human();
        jv[sfMPTokenIssuanceID] = to_string(issuanceID);
        if (amount)
            jv[sfAmount] = toJson(*amount);
        return jv;
    }

    static json::Value
    scheduleDelete(test::jtx::Account const& admin, MPTID const& issuanceID)
    {
        json::Value jv;
        jv[jss::TransactionType] = jss::CouponScheduleDelete;
        jv[jss::Account] = admin.human();
        jv[sfMPTokenIssuanceID] = to_string(issuanceID);
        return jv;
    }

    // The schedule entry, or nullptr when no schedule exists.
    static std::shared_ptr<SLE const>
    scheduleOf(test::jtx::Env& env, MPTID const& issuanceID)
    {
        return env.le(keylet::couponSchedule(issuanceID));
    }

    static STAmount
    accruedOf(test::jtx::Env& env, MPTID const& issuanceID, test::jtx::Account const& holder)
    {
        auto const sle = env.le(keylet::mptoken(issuanceID, holder.id()));
        if (!sle || !sle->isFieldPresent(sfCouponAccrued))
            return STAmount{};
        return sle->getFieldAmount(sfCouponAccrued);
    }

    void
    testDisabled()
    {
        testcase("disabled");
        using namespace test::jtx;

        Env env{*this, testableAmendments() - featureCouponPayments};
        Account const alice{"alice"};
        Account const gw{"gw"};
        env.fund(XRP(10'000), alice, gw);
        env.close();

        MPTID const fake{};
        env(scheduleCreate(alice, fake, gw["USD"]), Ter(temDISABLED));
        env(couponPay(alice, fake, gw["USD"](1)), Ter(temDISABLED));
        env(couponClaim(alice, fake), Ter(temDISABLED));
        env(scheduleDelete(alice, fake), Ter(temDISABLED));
    }

    void
    testScheduleCreate()
    {
        testcase("schedule create");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        auto const usd = gw["USD"];
        env.trust(usd(10'000), alice, bob);
        env(pay(gw, alice, usd(1'000)));
        env.close();

        MPTTester mpt{env, alice, {.holders = {bob}, .fund = false}};
        mpt.create({.flags = tfMPTCanTransfer});
        mpt.authorize({.account = bob});
        env.close();

        auto const id = mpt.issuanceID();

        // A stranger cannot create a schedule on someone else's issuance.
        env(scheduleCreate(bob, id, usd), Ter(tecNO_PERMISSION));
        env.close();

        // A bond paying coupons in itself is rebasing, not interest.
        env(scheduleCreate(alice, id, mpt), Ter(temMALFORMED));
        env.close();

        // Call protection needs a callable schedule and a real call time.
        {
            auto jv = scheduleCreate(alice, id, usd);
            jv[sfEarliestCallTime] = 1'000'000;
            env(jv, Ter(temMALFORMED));
            jv[sfCallNoticePeriod] = 86'400;
            jv[sfEarliestCallTime] = 0;
            env(jv, Ter(temMALFORMED));
            env.close();
        }

        env(scheduleCreate(alice, id, usd), Ter(tesSUCCESS));
        env.close();

        auto const sle = scheduleOf(env, id);
        BEAST_EXPECT(sle);
        if (sle)
        {
            BEAST_EXPECT(sle->getAccountID(sfOwner) == alice.id());
            BEAST_EXPECT(sle->getAccountID(sfAccount) == alice.id());
            BEAST_EXPECT(sle->getFieldAmount(sfPoolAmount) == usd(0));
            BEAST_EXPECT(sle->getFieldAmount(sfAccruedPerUnit) == usd(0));
            BEAST_EXPECT(sle->getFieldU32(sfClaimantCount) == 0);
            BEAST_EXPECT(sle->getFieldU32(sfCouponCount) == 0);
        }

        // The issuance is flagged so settlement can skip issuances without one.
        auto const issuance = env.le(keylet::mptokenIssuance(id));
        BEAST_EXPECT(issuance && issuance->isFlag(lsfMPTCouponSchedule));

        // One schedule per issuance.
        env(scheduleCreate(alice, id, usd), Ter(tecDUPLICATE));
        env.close();
    }

    void
    testCouponPay()
    {
        testcase("coupon pay funds the pool");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        env.trust(usd(10'000), alice, bob);
        env.trust(eur(10'000), alice);
        env(pay(gw, alice, usd(1'000)));
        env.close();

        MPTTester mpt{env, alice, {.holders = {bob}, .fund = false}};
        mpt.create({.flags = tfMPTCanTransfer});
        mpt.authorize({.account = bob});
        mpt.pay(alice, bob, 100);
        env.close();

        auto const id = mpt.issuanceID();
        env(scheduleCreate(alice, id, usd), Ter(tesSUCCESS));
        env.close();

        // Only the payer named on the schedule may declare a coupon.
        env(couponPay(bob, id, usd(1)), Ter(tecNO_PERMISSION));
        env.close();

        // The coupon must be denominated in the schedule's asset.
        env(couponPay(alice, id, eur(1)), Ter(tecWRONG_ASSET));
        env.close();

        auto const before = env.balance(alice, usd);
        env(couponPay(alice, id, usd(2)), Ter(tesSUCCESS));
        env.close();

        // 100 units outstanding at 2 usd per unit funds 200 usd.
        auto const sle = scheduleOf(env, id);
        BEAST_EXPECT(sle);
        if (sle)
        {
            BEAST_EXPECT(sle->getFieldAmount(sfPoolAmount) == usd(200));
            BEAST_EXPECT(sle->getFieldAmount(sfAccruedPerUnit) == usd(2));
            BEAST_EXPECT(sle->getFieldU32(sfCouponCount) == 1);
            BEAST_EXPECT(sle->isFieldPresent(sfLastCouponTime));
        }
        BEAST_EXPECT(env.balance(alice, usd) == before - usd(200));
    }

    void
    testAccrualAndClaim()
    {
        testcase("accrual and claim from the pool");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        auto const usd = gw["USD"];
        env.trust(usd(10'000), alice, bob);
        env(pay(gw, alice, usd(1'000)));
        env.close();

        MPTTester mpt{env, alice, {.holders = {bob}, .fund = false}};
        mpt.create({.flags = tfMPTCanTransfer});
        mpt.authorize({.account = bob});
        mpt.pay(alice, bob, 100);
        env.close();

        auto const id = mpt.issuanceID();
        env(scheduleCreate(alice, id, usd), Ter(tesSUCCESS));
        env.close();

        // Nothing declared yet: nothing to claim.
        env(couponClaim(bob, id), Ter(tecNO_PERMISSION));
        env.close();

        env(couponPay(alice, id, usd(2)), Ter(tesSUCCESS));
        env.close();

        auto const bobBefore = env.balance(bob, usd);
        env(couponClaim(bob, id), Ter(tesSUCCESS));
        env.close();

        // 100 units at 2 usd per unit.
        BEAST_EXPECT(env.balance(bob, usd) == bobBefore + usd(200));
        BEAST_EXPECT(accruedOf(env, id, bob) == STAmount{});

        auto const sle = scheduleOf(env, id);
        BEAST_EXPECT(sle);
        if (sle)
        {
            BEAST_EXPECT(sle->getFieldAmount(sfPoolAmount) == usd(0));
            BEAST_EXPECT(sle->getFieldU32(sfClaimantCount) == 0);
        }

        // A second claim has nothing left.
        env(couponClaim(bob, id), Ter(tecNO_PERMISSION));
        env.close();
    }

    void
    testArrears()
    {
        testcase("arrears collect in one claim");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        auto const usd = gw["USD"];
        env.trust(usd(10'000), alice, bob);
        env(pay(gw, alice, usd(1'000)));
        env.close();

        MPTTester mpt{env, alice, {.holders = {bob}, .fund = false}};
        mpt.create({.flags = tfMPTCanTransfer});
        mpt.authorize({.account = bob});
        mpt.pay(alice, bob, 10);
        env.close();

        auto const id = mpt.issuanceID();
        env(scheduleCreate(alice, id, usd), Ter(tesSUCCESS));
        env.close();

        for (int i = 0; i < 3; ++i)
        {
            env(couponPay(alice, id, usd(1)), Ter(tesSUCCESS));
            env.close();
        }

        auto const bobBefore = env.balance(bob, usd);
        env(couponClaim(bob, id), Ter(tesSUCCESS));
        env.close();

        // Three coupons of 1 usd on 10 units.
        BEAST_EXPECT(env.balance(bob, usd) == bobBefore + usd(30));
    }

    void
    testSettlementOnTransfer()
    {
        testcase("units settle before they move");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        env.fund(XRP(10'000), gw, alice, bob, carol);
        env.close();
        auto const usd = gw["USD"];
        env.trust(usd(10'000), alice, bob, carol);
        env(pay(gw, alice, usd(1'000)));
        env.close();

        MPTTester mpt{env, alice, {.holders = {bob, carol}, .fund = false}};
        mpt.create({.flags = tfMPTCanTransfer});
        mpt.authorize({.account = bob});
        mpt.authorize({.account = carol});
        mpt.pay(alice, bob, 100);
        env.close();

        auto const id = mpt.issuanceID();
        env(scheduleCreate(alice, id, usd), Ter(tesSUCCESS));
        env.close();

        env(couponPay(alice, id, usd(1)), Ter(tesSUCCESS));
        env.close();

        // bob sells the whole position to carol after the coupon is declared.
        mpt.pay(bob, carol, 100);
        env.close();

        // The coupon belongs to bob, who held the units when it was declared.
        BEAST_EXPECT(accruedOf(env, id, bob) == usd(100));
        BEAST_EXPECT(accruedOf(env, id, carol) == STAmount{});

        // A second coupon now belongs entirely to carol.
        env(couponPay(alice, id, usd(1)), Ter(tesSUCCESS));
        env.close();
        env(couponClaim(carol, id), Ter(tesSUCCESS));
        env.close();

        // bob keeps his arrears on a zero-unit MPToken and collects later.
        env(couponClaim(bob, id), Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(accruedOf(env, id, bob) == STAmount{});
    }

    void
    testPoolAlwaysCovers()
    {
        testcase("a declared coupon cannot be spent away");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const dave{"dave"};
        env.fund(XRP(10'000), gw, alice, bob, dave);
        env.close();
        auto const usd = gw["USD"];
        env.trust(usd(10'000), alice, bob, dave);
        env(pay(gw, alice, usd(500)));
        env.close();

        MPTTester mpt{env, alice, {.holders = {bob}, .fund = false}};
        mpt.create({.flags = tfMPTCanTransfer});
        mpt.authorize({.account = bob});
        mpt.pay(alice, bob, 100);
        env.close();

        auto const id = mpt.issuanceID();
        env(scheduleCreate(alice, id, usd), Ter(tesSUCCESS));
        env.close();

        env(couponPay(alice, id, usd(2)), Ter(tesSUCCESS));
        env.close();

        // The payer spends everything else she has.
        env(pay(alice, dave, env.balance(alice, usd)), Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(env.balance(alice, usd) == usd(0));

        // The claim is still honored: the pool holds it, not the payer.
        auto const bobBefore = env.balance(bob, usd);
        env(couponClaim(bob, id), Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(env.balance(bob, usd) == bobBefore + usd(200));
    }

    void
    testScheduleDelete()
    {
        testcase("schedule delete requires no obligations");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        auto const usd = gw["USD"];
        env.trust(usd(10'000), alice, bob);
        env(pay(gw, alice, usd(1'000)));
        env.close();

        MPTTester mpt{env, alice, {.holders = {bob}, .fund = false}};
        mpt.create({.flags = tfMPTCanTransfer});
        mpt.authorize({.account = bob});
        mpt.pay(alice, bob, 100);
        env.close();

        auto const id = mpt.issuanceID();
        env(scheduleCreate(alice, id, usd), Ter(tesSUCCESS));
        env.close();

        // Units are outstanding.
        env(scheduleDelete(alice, id), Ter(tecHAS_OBLIGATIONS));
        env.close();

        env(couponPay(alice, id, usd(1)), Ter(tesSUCCESS));
        env.close();
        mpt.pay(bob, alice, 100);
        env.close();

        // Nothing outstanding, but bob is still owed his coupon.
        env(scheduleDelete(alice, id), Ter(tecHAS_OBLIGATIONS));
        env.close();

        env(couponClaim(bob, id), Ter(tesSUCCESS));
        env.close();

        env(scheduleDelete(alice, id), Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(!scheduleOf(env, id));

        auto const issuance = env.le(keylet::mptokenIssuance(id));
        BEAST_EXPECT(issuance && !issuance->isFlag(lsfMPTCouponSchedule));
    }

    void
    testMPTCouponAsset()
    {
        testcase("coupons paid in an MPT");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();

        // gw issues the coupon asset; alice holds it and pays coupons with it.
        MPTTester coupon{env, gw, {.holders = {alice, bob}, .fund = false}};
        coupon.create({.flags = tfMPTCanTransfer});
        coupon.authorize({.account = alice});
        coupon.authorize({.account = bob});
        coupon.pay(gw, alice, 10'000);
        env.close();

        // alice issues the bond.
        MPTTester bond{env, alice, {.holders = {bob}, .fund = false}};
        bond.create({.flags = tfMPTCanTransfer});
        bond.authorize({.account = bob});
        bond.pay(alice, bob, 100);
        env.close();

        auto const bondID = bond.issuanceID();
        env(scheduleCreate(alice, bondID, coupon), Ter(tesSUCCESS));
        env.close();

        env(couponPay(alice, bondID, STAmount{coupon, 2}), Ter(tesSUCCESS));
        env.close();

        auto const sle = scheduleOf(env, bondID);
        BEAST_EXPECT(sle);
        if (sle)
            BEAST_EXPECT(sle->getFieldAmount(sfPoolAmount) == STAmount(coupon, 200));

        env(couponClaim(bob, bondID), Ter(tesSUCCESS));
        env.close();

        // bob received 100 units at 2 of the coupon MPT each.
        BEAST_EXPECT(coupon.checkMPTokenAmount(bob, 200));
    }

    void
    testDelegatedClaim()
    {
        testcase("delegated claim");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const dave{"dave"};
        env.fund(XRP(10'000), gw, alice, bob, dave);
        env.close();
        auto const usd = gw["USD"];
        env.trust(usd(10'000), alice, bob);
        env(pay(gw, alice, usd(1'000)));
        env.close();

        MPTTester mpt{env, alice, {.holders = {bob}, .fund = false}};
        mpt.create({.flags = tfMPTCanTransfer});
        mpt.authorize({.account = bob});
        mpt.pay(alice, bob, 100);
        env.close();

        auto const id = mpt.issuanceID();
        env(scheduleCreate(alice, id, usd), Ter(tesSUCCESS));
        env(couponPay(alice, id, usd(2)), Ter(tesSUCCESS));
        env.close();

        // Without bob's grant, dave cannot claim for him.
        env(couponClaim(bob, id), delegate::As(dave), Ter(terNO_DELEGATE_PERMISSION));
        env.close();
        BEAST_EXPECT(env.balance(bob, usd) == usd(0));

        env(delegate::set(bob, dave, {"CouponClaim"}));
        env.close();

        // With the grant, dave claims for bob: bob receives, dave pays the fee.
        auto const bobXRP = env.balance(bob);
        auto const daveXRP = env.balance(dave);
        auto const fee = env.current()->fees().base;
        env(couponClaim(bob, id), delegate::As(dave), Ter(tesSUCCESS));
        env.close();

        BEAST_EXPECT(env.balance(bob, usd) == usd(200));
        BEAST_EXPECT(env.balance(dave, usd) == usd(0));
        BEAST_EXPECT(env.balance(bob) == bobXRP);
        BEAST_EXPECT(env.balance(dave) == daveXRP - fee);
        BEAST_EXPECT(accruedOf(env, id, bob) == STAmount{});

        // Revoking the grant refuses dave again.
        env(delegate::set(bob, dave, {}));
        env(couponPay(alice, id, usd(1)), Ter(tesSUCCESS));
        env.close();
        env(couponClaim(bob, id), delegate::As(dave), Ter(terNO_DELEGATE_PERMISSION));
        env.close();
        BEAST_EXPECT(env.balance(bob, usd) == usd(200));
    }

    static json::Value
    scheduleSet(test::jtx::Account const& admin, MPTID const& issuanceID, std::uint32_t expiration)
    {
        json::Value jv;
        jv[jss::TransactionType] = jss::CouponScheduleSet;
        jv[jss::Account] = admin.human();
        jv[sfMPTokenIssuanceID] = to_string(issuanceID);
        jv[sfExpiration] = expiration;
        return jv;
    }

    // The parent close time the next transaction's preclaim sees.
    static std::uint32_t
    nowOf(test::jtx::Env& env)
    {
        return env.current()->header().parentCloseTime.time_since_epoch().count();
    }

    void
    testMalformed()
    {
        testcase("malformed transactions");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const gw{"gw"};
        Account const alice{"alice"};
        env.fund(XRP(10'000), gw, alice);
        env.close();
        auto const usd = gw["USD"];

        MPTID const zero{};
        env(scheduleCreate(alice, zero, usd), Ter(temMALFORMED));
        env(scheduleSet(alice, zero, 1'000), Ter(temMALFORMED));
        env(scheduleDelete(alice, zero), Ter(temMALFORMED));
        env(couponPay(alice, zero, usd(1)), Ter(temMALFORMED));
        env(couponClaim(alice, zero), Ter(temMALFORMED));

        MPTID const id = makeMptID(1, alice.id());
        env(scheduleSet(alice, id, 0), Ter(temMALFORMED));
        env(couponPay(alice, id, usd(0)), Ter(temBAD_AMOUNT));
        env(couponClaim(alice, id, usd(0)), Ter(temBAD_AMOUNT));

        std::uint32_t const first = nowOf(env) + 1'000;
        auto const terms = [&] {
            auto jv = scheduleCreate(alice, id, usd);
            jv[sfCouponAmount] = toJson(STAmount{usd(5)});
            jv[sfCouponInterval] = 86'400;
            jv[sfFirstCouponTime] = first;
            return jv;
        };

        // The declared terms are all or nothing.
        {
            auto jv = terms();
            jv.removeMember(sfCouponInterval.jsonName);
            env(jv, Ter(temMALFORMED));
            jv = terms();
            jv.removeMember(sfFirstCouponTime.jsonName);
            env(jv, Ter(temMALFORMED));
        }
        {
            auto jv = terms();
            jv[sfCouponAmount] = toJson(STAmount{usd(0)});
            env(jv, Ter(temBAD_AMOUNT));
        }
        {
            auto jv = terms();
            jv[sfCouponInterval] = 0;
            env(jv, Ter(temMALFORMED));
            jv = terms();
            jv[sfFirstCouponTime] = 0;
            env(jv, Ter(temMALFORMED));
        }

        // The schedule cannot expire before its first coupon.
        {
            auto jv = terms();
            jv[sfExpiration] = first;
            env(jv, Ter(temBAD_EXPIRATION));
        }

        // A call notice of zero is no notice.
        {
            auto jv = scheduleCreate(alice, id, usd);
            jv[sfCallNoticePeriod] = 0;
            env(jv, Ter(temMALFORMED));
        }

        // Call protection must end before the schedule does.
        {
            auto jv = scheduleCreate(alice, id, usd);
            jv[sfCallNoticePeriod] = 100;
            jv[sfExpiration] = first;
            jv[sfEarliestCallTime] = first;
            env(jv, Ter(temBAD_EXPIRATION));
        }
    }

    void
    testCreateTerms()
    {
        testcase("schedule create stores the declared terms");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        auto const usd = gw["USD"];
        env.trust(usd(10'000), alice, bob);
        env(pay(gw, alice, usd(1'000)));
        env.close();

        MPTTester mpt{env, alice, {.holders = {bob}, .fund = false}};
        mpt.create({.flags = tfMPTCanTransfer});
        mpt.authorize({.account = bob});
        mpt.pay(alice, bob, 100);
        env.close();
        auto const id = mpt.issuanceID();

        std::uint32_t const now = nowOf(env);
        auto jv = scheduleCreate(alice, id, usd);
        jv[sfCouponAmount] = toJson(STAmount{usd(5)});
        jv[sfCouponInterval] = 86'400;
        jv[sfFirstCouponTime] = now + 1'000;
        jv[sfExpiration] = now + 100'000;
        jv[sfCallNoticePeriod] = 100;
        jv[sfEarliestCallTime] = now + 2'000;
        env(jv, Ter(tesSUCCESS));
        env.close();

        auto const sle = scheduleOf(env, id);
        BEAST_EXPECT(sle);
        if (sle)
        {
            BEAST_EXPECT(sle->getFieldAmount(sfCouponAmount) == usd(5));
            BEAST_EXPECT(sle->getFieldU32(sfCouponInterval) == 86'400);
            BEAST_EXPECT(sle->getFieldU32(sfFirstCouponTime) == now + 1'000);
            BEAST_EXPECT(sle->getFieldU32(sfExpiration) == now + 100'000);
            BEAST_EXPECT(sle->getFieldU32(sfCallNoticePeriod) == 100);
            BEAST_EXPECT(sle->getFieldU32(sfEarliestCallTime) == now + 2'000);
        }

        // The declared amount is informational: a coupon of another amount is paid.
        env(couponPay(alice, id, usd(3)), Ter(tesSUCCESS));
        env.close();
        auto const after = scheduleOf(env, id);
        BEAST_EXPECT(after && after->getFieldAmount(sfPoolAmount) == usd(300));
    }

    void
    testCreatePreclaim()
    {
        testcase("schedule create preclaim");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const ghost{"ghost"};
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        auto const usd = gw["USD"];

        // No such issuance.
        env(scheduleCreate(alice, makeMptID(99, alice.id()), usd), Ter(tecOBJECT_NOT_FOUND));
        env.close();

        MPTTester mpt{env, alice, {.fund = false}};
        mpt.create({.flags = tfMPTCanTransfer});
        auto const id = mpt.issuanceID();

        // The coupon asset's issuer must exist.
        env(scheduleCreate(alice, id, ghost["USD"]), Ter(tecNO_ISSUER));
        // An MPT coupon asset must exist.
        env(scheduleCreate(alice, id, MPTIssue{makeMptID(99, gw.id())}), Ter(tecOBJECT_NOT_FOUND));
        env.close();

        // Confidential balances hide the units a coupon accrues against.
        MPTTester conf{env, alice, {.fund = false}};
        conf.create({.flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance});
        env(scheduleCreate(alice, conf.issuanceID(), usd), Ter(tecNO_PERMISSION));
        env.close();

        // The schedule needs the owner's reserve.
        Account const carol{"carol"};
        auto const fees = env.current()->fees();
        env.fund(
            STAmount{fees.accountReserve(1, 1) + XRPAmount{fees.increment.drops() / 2}}, carol);
        env.close();
        MPTTester poor{env, carol, {.fund = false}};
        poor.create({.flags = tfMPTCanTransfer});
        env(scheduleCreate(carol, poor.issuanceID(), usd), Ter(tecINSUFFICIENT_RESERVE));
        env.close();
        BEAST_EXPECT(!scheduleOf(env, poor.issuanceID()));
    }

    void
    testVaultShares()
    {
        testcase("schedule on a vault share issuance");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        auto const usd = gw["USD"];

        Vault const vault{env};
        auto [tx, keylet] = vault.create({.owner = alice, .asset = xrpIssue()});
        env(tx, Ter(tesSUCCESS));
        env.close();

        auto const sleVault = env.le(keylet);
        BEAST_EXPECT(sleVault);
        if (!sleVault)
            return;
        MPTID const share = sleVault->at(sfShareMPTID);
        AccountID const pseudo = sleVault->at(sfAccount);

        // Only the vault owner administers a schedule on the shares.
        env(scheduleCreate(bob, share, usd), Ter(tecNO_PERMISSION));
        env(scheduleCreate(alice, share, usd), Ter(tesSUCCESS));
        env.close();

        auto const sle = scheduleOf(env, share);
        BEAST_EXPECT(sle);
        if (sle)
        {
            BEAST_EXPECT(sle->getAccountID(sfOwner) == alice.id());
            BEAST_EXPECT(sle->getAccountID(sfAccount) == pseudo);
        }

        // The schedule holds the share issuance, so the vault stays.
        env(vault.del({.owner = alice, .id = keylet.key}), Ter(tecHAS_OBLIGATIONS));
        env.close();

        env(scheduleDelete(alice, share), Ter(tesSUCCESS));
        env.close();
        env(vault.del({.owner = alice, .id = keylet.key}), Ter(tesSUCCESS));
        env.close();
    }

    void
    testCall()
    {
        testcase("call mechanics");
        using namespace test::jtx;
        using namespace std::chrono_literals;

        Env env{*this, testableAmendments()};
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        auto const usd = gw["USD"];
        env.trust(usd(10'000), alice, bob);
        env(pay(gw, alice, usd(1'000)));
        env.close();

        MPTTester callable{env, alice, {.holders = {bob}, .fund = false}};
        callable.create({.flags = tfMPTCanTransfer});
        MPTTester perpetual{env, alice, {.holders = {bob}, .fund = false}};
        perpetual.create({.flags = tfMPTCanTransfer});
        perpetual.authorize({.account = bob});
        perpetual.pay(alice, bob, 10);
        MPTTester fixed{env, alice, {.fund = false}};
        fixed.create({.flags = tfMPTCanTransfer});
        MPTTester none{env, alice, {.fund = false}};
        none.create({.flags = tfMPTCanTransfer});
        MPTTester unprotected{env, alice, {.fund = false}};
        unprotected.create({.flags = tfMPTCanTransfer});
        env.close();

        std::uint32_t const start = nowOf(env);
        {
            auto jv = scheduleCreate(alice, callable.issuanceID(), usd);
            jv[sfCallNoticePeriod] = 100;
            jv[sfEarliestCallTime] = start + 1'000;
            jv[sfExpiration] = start + 100'000;
            env(jv, Ter(tesSUCCESS));
        }
        {
            auto jv = scheduleCreate(alice, perpetual.issuanceID(), usd);
            jv[sfCallNoticePeriod] = 100;
            jv[sfEarliestCallTime] = start + 1;
            env(jv, Ter(tesSUCCESS));
        }
        env(scheduleCreate(alice, fixed.issuanceID(), usd), Ter(tesSUCCESS));
        {
            auto jv = scheduleCreate(alice, unprotected.issuanceID(), usd);
            jv[sfCallNoticePeriod] = 100;
            env(jv, Ter(tesSUCCESS));
        }
        env.close();

        std::uint32_t const now = nowOf(env);
        auto const id = callable.issuanceID();

        // No schedule, not the owner, not callable.
        env(scheduleSet(alice, none.issuanceID(), now + 2'000), Ter(tecNO_ENTRY));
        env(scheduleSet(bob, id, now + 2'000), Ter(tecNO_PERMISSION));
        env(scheduleSet(alice, fixed.issuanceID(), now + 2'000), Ter(tecNO_PERMISSION));

        // Less warning than the notice period.
        env(scheduleSet(alice, id, now + 50), Ter(tecNO_PERMISSION));
        // Before the call protection ends.
        env(scheduleSet(alice, id, now + 500), Ter(tecNO_PERMISSION));
        // A call never extends the schedule.
        env(scheduleSet(alice, id, now + 200'000), Ter(tecNO_PERMISSION));
        env(scheduleSet(alice, id, start + 100'000), Ter(tecNO_PERMISSION));

        env(scheduleSet(alice, id, now + 2'000), Ter(tesSUCCESS));

        // A perpetual callable schedule gains an Expiration at exactly the notice floor.
        auto const pid = perpetual.issuanceID();
        env(scheduleSet(alice, pid, now + 99), Ter(tecNO_PERMISSION));
        env(scheduleSet(alice, pid, now + 100), Ter(tesSUCCESS));

        // Without call protection only the notice floor applies.
        env(scheduleSet(alice, unprotected.issuanceID(), now + 100), Ter(tesSUCCESS));
        env.close();

        auto const sle = scheduleOf(env, id);
        BEAST_EXPECT(sle && sle->getFieldU32(sfExpiration) == now + 2'000);
        auto const psle = scheduleOf(env, pid);
        BEAST_EXPECT(psle && psle->getFieldU32(sfExpiration) == now + 100);

        // Once the called schedule expires, no further coupon is declared.
        env.close(200s);
        env(couponPay(alice, pid, usd(1)), Ter(tecEXPIRED));
        env.close();
    }

    void
    testPayPreclaim()
    {
        testcase("coupon pay preclaim");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        auto const usd = gw["USD"];
        env.trust(usd(10'000), alice, bob);
        env(pay(gw, alice, usd(1'000)));
        env.close();

        // No schedule.
        env(couponPay(alice, makeMptID(99, alice.id()), usd(1)), Ter(tecNO_ENTRY));

        MPTTester mpt{env, alice, {.holders = {bob}, .fund = false}};
        mpt.create({.flags = tfMPTCanTransfer});
        mpt.authorize({.account = bob});
        env.close();
        auto const id = mpt.issuanceID();
        env(scheduleCreate(alice, id, usd), Ter(tesSUCCESS));
        env.close();

        // No units outstanding: there is no one to pay.
        env(couponPay(alice, id, usd(1)), Ter(tecNO_PERMISSION));

        mpt.pay(alice, bob, 100);
        env.close();

        // 100 units at 20 usd is more than alice holds.
        env(couponPay(alice, id, usd(20)), Ter(tecINSUFFICIENT_FUNDS));
        env.close();

        // An IOU payer that issues the coupon asset holds none of it to pool.
        MPTTester gwBond{env, gw, {.holders = {bob}, .fund = false}};
        gwBond.create({.flags = tfMPTCanTransfer});
        gwBond.authorize({.account = bob});
        gwBond.pay(gw, bob, 10);
        env.close();
        env(scheduleCreate(gw, gwBond.issuanceID(), usd), Ter(tesSUCCESS));
        env.close();
        env(couponPay(gw, gwBond.issuanceID(), usd(1)), Ter(tecNO_PERMISSION));
        env.close();

        // Likewise for an MPT coupon asset its payer issues.
        MPTTester own{env, alice, {.fund = false}};
        own.create({.flags = tfMPTCanTransfer});
        MPTTester bond{env, alice, {.holders = {bob}, .fund = false}};
        bond.create({.flags = tfMPTCanTransfer});
        bond.authorize({.account = bob});
        bond.pay(alice, bob, 10);
        env.close();
        env(scheduleCreate(alice, bond.issuanceID(), own), Ter(tesSUCCESS));
        env.close();
        env(couponPay(alice, bond.issuanceID(), STAmount{own, 1}), Ter(tecNO_PERMISSION));
        env.close();
        auto const sle = scheduleOf(env, bond.issuanceID());
        BEAST_EXPECT(sle && sle->getFieldU32(sfCouponCount) == 0);
    }

    void
    testXRPCoupon()
    {
        testcase("coupons paid in XRP");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10'000), alice, bob);
        env.close();

        MPTTester mpt{env, alice, {.holders = {bob}, .fund = false}};
        mpt.create({.flags = tfMPTCanTransfer});
        mpt.authorize({.account = bob});
        mpt.pay(alice, bob, 100);
        env.close();
        auto const id = mpt.issuanceID();
        env(scheduleCreate(alice, id, xrpIssue()), Ter(tesSUCCESS));
        env.close();

        auto const fee = env.current()->fees().base;

        // 100 units at 1,000 XRP is more than alice has.
        env(couponPay(alice, id, XRP(1'000)), Ter(tecINSUFFICIENT_FUNDS));
        env.close();

        auto const aliceBefore = env.balance(alice);
        env(couponPay(alice, id, XRP(2)), Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(env.balance(alice) == aliceBefore - XRP(200) - fee);
        auto const sle = scheduleOf(env, id);
        BEAST_EXPECT(sle && sle->getFieldAmount(sfPoolAmount) == XRP(200));

        auto const bobBefore = env.balance(bob);
        env(couponClaim(bob, id), Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(env.balance(bob) == bobBefore + XRP(200) - fee);
        auto const drained = scheduleOf(env, id);
        BEAST_EXPECT(drained && drained->getFieldAmount(sfPoolAmount) == XRP(0));
    }

    void
    testMPTPayFunds()
    {
        testcase("coupon pay in an MPT needs the payer's units");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();

        MPTTester coupon{env, gw, {.holders = {alice, bob}, .fund = false}};
        coupon.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
        coupon.authorize({.account = alice});
        coupon.authorize({.account = bob});
        coupon.pay(gw, alice, 10'000);
        env.close();

        MPTTester bond{env, alice, {.holders = {bob, gw}, .fund = false}};
        bond.create({.flags = tfMPTCanTransfer});
        bond.authorize({.account = bob});
        bond.authorize({.account = gw});
        bond.pay(alice, bob, 100);
        bond.pay(alice, gw, 10);
        env.close();
        auto const id = bond.issuanceID();
        env(scheduleCreate(alice, id, coupon), Ter(tesSUCCESS));
        env.close();

        // 110 units at 100 is more than alice's 10,000.
        env(couponPay(alice, id, STAmount{coupon, 100}), Ter(tecINSUFFICIENT_FUNDS));
        env(couponPay(alice, id, STAmount{coupon, 2}), Ter(tesSUCCESS));
        env.close();

        // A locked holder cannot receive the coupon asset; the accrual waits.
        coupon.set({.account = gw, .holder = bob, .flags = tfMPTLock});
        env.close();
        env(couponClaim(bob, id), Ter(tecLOCKED));
        env.close();
        coupon.set({.account = gw, .holder = bob, .flags = tfMPTUnlock});
        env.close();
        env(couponClaim(bob, id), Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(coupon.checkMPTokenAmount(bob, 200));

        // The coupon asset's issuer holding bond units redeems its 20 out of the pool.
        auto const outstanding = coupon.getBalance(gw);
        env(couponClaim(gw, id), Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(coupon.getBalance(gw) == outstanding - 20);
        auto const sle = scheduleOf(env, id);
        BEAST_EXPECT(sle && sle->getFieldAmount(sfPoolAmount) == STAmount(coupon, 0));
    }

    void
    testClaimPreclaim()
    {
        testcase("coupon claim preclaim and partial claims");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        env.fund(XRP(10'000), gw, alice, bob, carol);
        env.close();
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        env(fset(gw, asfRequireAuth));
        env.close();
        env.trust(usd(10'000), alice, bob);
        env(trust(gw, alice["USD"](0), tfSetfAuth));
        env(pay(gw, alice, usd(1'000)));
        env.close();

        // No schedule.
        env(couponClaim(bob, makeMptID(99, alice.id())), Ter(tecNO_ENTRY));

        MPTTester mpt{env, alice, {.holders = {bob, gw}, .fund = false}};
        mpt.create({.flags = tfMPTCanTransfer});
        mpt.authorize({.account = bob});
        mpt.authorize({.account = gw});
        mpt.pay(alice, bob, 100);
        mpt.pay(alice, gw, 10);
        env.close();
        auto const id = mpt.issuanceID();
        env(scheduleCreate(alice, id, usd), Ter(tesSUCCESS));
        env(couponPay(alice, id, usd(2)), Ter(tesSUCCESS));
        env.close();

        // carol holds no units of this issuance.
        env(couponClaim(carol, id), Ter(tecNO_ENTRY));
        // The amount must be in the coupon asset.
        env(couponClaim(bob, id, eur(1)), Ter(tecNO_PERMISSION));
        // bob's usd line is not authorized by gw.
        env(couponClaim(bob, id), Ter(tecNO_AUTH));
        env.close();

        env(trust(gw, bob["USD"](0), tfSetfAuth));
        env(trust(gw, bob["USD"](0), tfSetFreeze));
        env.close();
        env(couponClaim(bob, id), Ter(tecFROZEN));
        env.close();
        env(trust(gw, bob["USD"](0), tfClearFreeze));
        env.close();

        // A partial claim leaves the rest accrued.
        env(couponClaim(bob, id, usd(50)), Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(env.balance(bob, usd) == usd(50));
        BEAST_EXPECT(accruedOf(env, id, bob) == usd(150));
        auto sle = scheduleOf(env, id);
        BEAST_EXPECT(sle && sle->getFieldU32(sfClaimantCount) == 1);

        // A further coupon adds to what bob still has accrued.
        env(couponPay(alice, id, usd(1)), Ter(tesSUCCESS));
        env.close();

        // Asking for more than is owed pays what is owed: 150 left plus 100 new.
        env(couponClaim(bob, id, usd(1'000)), Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(env.balance(bob, usd) == usd(300));
        BEAST_EXPECT(accruedOf(env, id, bob) == STAmount{});

        // The coupon asset's issuer is paid in its own IOU, which it does not hold.
        env(couponClaim(gw, id), Ter(tesSUCCESS));
        env.close();
        sle = scheduleOf(env, id);
        BEAST_EXPECT(sle && sle->getFieldAmount(sfPoolAmount) == usd(0));
        BEAST_EXPECT(sle && sle->getFieldU32(sfClaimantCount) == 0);
    }

    void
    testClaimCreatesLine()
    {
        testcase("coupon claim creates the holder's trust line");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        env.fund(XRP(10'000), gw, alice, bob);
        auto const fees = env.current()->fees();
        env.fund(
            STAmount{fees.accountReserve(1, 1) + XRPAmount{fees.increment.drops() / 2}}, carol);
        env.close();
        auto const usd = gw["USD"];
        env.trust(usd(10'000), alice);
        env(pay(gw, alice, usd(1'000)));
        env.close();

        MPTTester mpt{env, alice, {.holders = {bob, carol}, .fund = false}};
        mpt.create({.flags = tfMPTCanTransfer});
        mpt.authorize({.account = bob});
        mpt.authorize({.account = carol});
        mpt.pay(alice, bob, 100);
        mpt.pay(alice, carol, 100);
        env.close();
        auto const id = mpt.issuanceID();
        env(scheduleCreate(alice, id, usd), Ter(tesSUCCESS));
        env(couponPay(alice, id, usd(1)), Ter(tesSUCCESS));
        env.close();

        // bob has no usd line; the claim opens one.
        env(couponClaim(bob, id), Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(env.balance(bob, usd) == usd(100));

        // carol cannot cover the reserve of a new line; her coupon stays in the pool.
        env(couponClaim(carol, id), Ter(tecNO_LINE_INSUF_RESERVE));
        env.close();
        auto const sle = scheduleOf(env, id);
        BEAST_EXPECT(sle && sle->getFieldAmount(sfPoolAmount) == usd(100));

        // With the reserve covered she collects it.
        env(pay(env.master, carol, XRP(100)));
        env.close();
        env(couponClaim(carol, id), Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(env.balance(carol, usd) == usd(100));
    }

    void
    testHolderObligations()
    {
        testcase("unclaimed coupons and schedules are obligations");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        auto const usd = gw["USD"];
        env.trust(usd(10'000), alice, bob);
        env(pay(gw, alice, usd(1'000)));
        env.close();

        MPTTester mpt{env, alice, {.holders = {bob}, .fund = false}};
        mpt.create({.flags = tfMPTCanTransfer});
        mpt.authorize({.account = bob});
        mpt.pay(alice, bob, 100);
        env.close();
        auto const id = mpt.issuanceID();
        env(scheduleCreate(alice, id, usd), Ter(tesSUCCESS));
        env(couponPay(alice, id, usd(1)), Ter(tesSUCCESS));
        env.close();

        // bob returns every unit but is still owed his coupon.
        mpt.pay(bob, alice, 100);
        env.close();
        mpt.authorize({.account = bob, .flags = tfMPTUnauthorize, .err = tecHAS_OBLIGATIONS});
        env.close();

        // The schedule keeps the issuance alive.
        mpt.destroy({.err = tecHAS_OBLIGATIONS});
        env.close();

        env(couponClaim(bob, id), Ter(tesSUCCESS));
        env.close();
        mpt.authorize({.account = bob, .flags = tfMPTUnauthorize});
        env(scheduleDelete(alice, id), Ter(tesSUCCESS));
        env.close();
        mpt.destroy({});
    }

    void
    testDeleteErrors()
    {
        testcase("schedule delete errors");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        auto const usd = gw["USD"];

        MPTTester mpt{env, alice, {.fund = false}};
        mpt.create({.flags = tfMPTCanTransfer});
        env.close();
        auto const id = mpt.issuanceID();

        env(scheduleDelete(alice, id), Ter(tecNO_ENTRY));
        env(scheduleCreate(alice, id, usd), Ter(tesSUCCESS));
        env.close();
        env(scheduleDelete(bob, id), Ter(tecNO_PERMISSION));
        env.close();
        BEAST_EXPECT(scheduleOf(env, id));
    }

    void
    testEscrowedUnits()
    {
        testcase("escrowed units accrue and settle on release");
        using namespace test::jtx;
        using namespace std::chrono_literals;

        Env env{*this, testableAmendments()};
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        env.fund(XRP(10'000), gw, alice, bob, carol);
        env.close();
        auto const usd = gw["USD"];
        env.trust(usd(10'000), alice, bob, carol);
        env(pay(gw, alice, usd(1'000)));
        env.close();

        MPTTester mpt{env, alice, {.holders = {bob, carol}, .fund = false}};
        mpt.create({.flags = tfMPTCanTransfer | tfMPTCanEscrow});
        mpt.authorize({.account = bob});
        mpt.authorize({.account = carol});
        mpt.pay(alice, bob, 100);
        env.close();
        auto const id = mpt.issuanceID();
        env(scheduleCreate(alice, id, usd), Ter(tesSUCCESS));
        env.close();

        // bob escrows 40 units to carol, then a coupon is declared.
        auto const seq = env.seq(bob);
        env(escrow::create(bob, carol, mpt(40)), escrow::kFinishTime(env.now() + 1s));
        env.close();
        env(couponPay(alice, id, usd(1)), Ter(tesSUCCESS));
        env.close();

        // Releasing the escrow settles bob on all 100 units he held.
        env(escrow::finish(carol, bob, seq));
        env.close();
        BEAST_EXPECT(accruedOf(env, id, bob) == usd(100));
        BEAST_EXPECT(accruedOf(env, id, carol) == STAmount{});
    }

    void
    testPrecision()
    {
        testcase("accumulator and accrual precision");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        env.fund(XRP(10'000), gw, alice, bob, carol);
        env.close();
        auto const usd = gw["USD"];
        env.trust(usd(100'000'000'000), alice, bob, carol);
        env(pay(gw, alice, usd(30'000'000'000)));
        env.close();

        MPTTester mpt{env, alice, {.holders = {bob, carol}, .fund = false}};
        mpt.create({.flags = tfMPTCanTransfer});
        mpt.authorize({.account = bob});
        mpt.authorize({.account = carol});
        mpt.pay(alice, bob, 100'000'000);
        env.close();
        auto const id = mpt.issuanceID();
        env(scheduleCreate(alice, id, usd), Ter(tesSUCCESS));
        env.close();

        env(couponPay(alice, id, usd(100)), Ter(tesSUCCESS));
        env.close();

        // An increment the accumulator cannot hold is refused.
        env(couponPay(alice, id, STAmount{usd, 1, -20}), Ter(tecPRECISION_LOSS));
        env.close();

        // bob keeps one unit; his 10,000,000,000 usd accrual dwarfs what that unit earns next.
        mpt.pay(bob, carol, 99'999'999);
        env.close();
        BEAST_EXPECT(accruedOf(env, id, bob) == usd(10'000'000'000));
        env(couponPay(alice, id, STAmount{usd, 1, -7}), Ter(tesSUCCESS));
        env.close();

        // The sub-precision addition rounds away; bob still claims and still trades.
        env(couponClaim(bob, id), Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(env.balance(bob, usd) == usd(10'000'000'000));
        mpt.pay(bob, carol, 1);
        env.close();

        // carol is paid on all units, then returns all but one to the issuer.
        env(couponPay(alice, id, usd(100)), Ter(tesSUCCESS));
        env.close();
        mpt.pay(carol, alice, 99'999'999);
        env.close();

        // On one unit the coupon is too small for the pool still owed to carol.
        env(couponPay(alice, id, STAmount{usd, 1, -7}), Ter(tecPRECISION_LOSS));
        env.close();
    }

    void
    testLedgerEntry()
    {
        testcase("ledger_entry coupon_schedule");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const gw{"gw"};
        Account const alice{"alice"};
        env.fund(XRP(10'000), gw, alice);
        env.close();

        MPTTester mpt{env, alice, {.fund = false}};
        mpt.create({.flags = tfMPTCanTransfer});
        auto const id = mpt.issuanceID();
        env(scheduleCreate(alice, id, gw["USD"]), Ter(tesSUCCESS));
        env.close();

        auto const index = to_string(keylet::couponSchedule(id).key);
        {
            json::Value params;
            params[jss::coupon_schedule] = index;
            auto const jrr = env.rpc("json", "ledger_entry", to_string(params))[jss::result];
            BEAST_EXPECT(jrr[jss::index] == index);
            BEAST_EXPECT(jrr[jss::node][sfMPTokenIssuanceID.jsonName] == to_string(id));
        }
        {
            json::Value params;
            params[jss::coupon_schedule][jss::mpt_issuance_id] = to_string(id);
            auto const jrr = env.rpc("json", "ledger_entry", to_string(params))[jss::result];
            BEAST_EXPECT(jrr[jss::index] == index);
        }
        {
            json::Value params;
            params[jss::coupon_schedule] = json::Value(json::ValueType::Object);
            auto const jrr = env.rpc("json", "ledger_entry", to_string(params))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "malformedRequest");
        }
        {
            json::Value params;
            params[jss::coupon_schedule] = "not hex";
            auto const jrr = env.rpc("json", "ledger_entry", to_string(params))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "malformedRequest");
        }
    }

public:
    void
    run() override
    {
        testDisabled();
        testScheduleCreate();
        testCouponPay();
        testAccrualAndClaim();
        testArrears();
        testSettlementOnTransfer();
        testPoolAlwaysCovers();
        testScheduleDelete();
        testMPTCouponAsset();
        testDelegatedClaim();
        testMalformed();
        testCreateTerms();
        testCreatePreclaim();
        testVaultShares();
        testCall();
        testPayPreclaim();
        testXRPCoupon();
        testMPTPayFunds();
        testClaimPreclaim();
        testClaimCreatesLine();
        testHolderObligations();
        testDeleteErrors();
        testEscrowedUnits();
        testPrecision();
        testLedgerEntry();
    }
};

BEAST_DEFINE_TESTSUITE(Coupon, app, xrpl);

}  // namespace xrpl
