#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/utility.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/jss.h>

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
        auto const USD = gw["USD"];
        env.trust(USD(10'000), alice, bob);
        env(pay(gw, alice, USD(1'000)));
        env.close();

        MPTTester mpt{env, alice, {.holders = {bob}, .fund = false}};
        mpt.create({.flags = tfMPTCanTransfer});
        mpt.authorize({.account = bob});
        env.close();

        auto const id = mpt.issuanceID();

        // A stranger cannot create a schedule on someone else's issuance.
        env(scheduleCreate(bob, id, USD), Ter(tecNO_PERMISSION));
        env.close();

        // A bond paying coupons in itself is rebasing, not interest.
        env(scheduleCreate(alice, id, mpt), Ter(temMALFORMED));
        env.close();

        env(scheduleCreate(alice, id, USD), Ter(tesSUCCESS));
        env.close();

        auto const sle = scheduleOf(env, id);
        BEAST_EXPECT(sle);
        if (sle)
        {
            BEAST_EXPECT(sle->getAccountID(sfOwner) == alice.id());
            BEAST_EXPECT(sle->getAccountID(sfAccount) == alice.id());
            BEAST_EXPECT(sle->getFieldAmount(sfPoolAmount) == USD(0));
            BEAST_EXPECT(sle->getFieldAmount(sfAccruedPerUnit) == USD(0));
            BEAST_EXPECT(sle->getFieldU32(sfClaimantCount) == 0);
            BEAST_EXPECT(sle->getFieldU32(sfCouponCount) == 0);
        }

        // The issuance is flagged so settlement can skip issuances without one.
        auto const issuance = env.le(keylet::mptokenIssuance(id));
        BEAST_EXPECT(issuance && issuance->isFlag(lsfMPTCouponSchedule));

        // One schedule per issuance.
        env(scheduleCreate(alice, id, USD), Ter(tecDUPLICATE));
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
        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];
        env.trust(USD(10'000), alice, bob);
        env.trust(EUR(10'000), alice);
        env(pay(gw, alice, USD(1'000)));
        env.close();

        MPTTester mpt{env, alice, {.holders = {bob}, .fund = false}};
        mpt.create({.flags = tfMPTCanTransfer});
        mpt.authorize({.account = bob});
        mpt.pay(alice, bob, 100);
        env.close();

        auto const id = mpt.issuanceID();
        env(scheduleCreate(alice, id, USD), Ter(tesSUCCESS));
        env.close();

        // Only the payer named on the schedule may declare a coupon.
        env(couponPay(bob, id, USD(1)), Ter(tecNO_PERMISSION));
        env.close();

        // The coupon must be denominated in the schedule's asset.
        env(couponPay(alice, id, EUR(1)), Ter(tecWRONG_ASSET));
        env.close();

        auto const before = env.balance(alice, USD);
        env(couponPay(alice, id, USD(2)), Ter(tesSUCCESS));
        env.close();

        // 100 units outstanding at 2 USD per unit funds 200 USD.
        auto const sle = scheduleOf(env, id);
        BEAST_EXPECT(sle);
        if (sle)
        {
            BEAST_EXPECT(sle->getFieldAmount(sfPoolAmount) == USD(200));
            BEAST_EXPECT(sle->getFieldAmount(sfAccruedPerUnit) == USD(2));
            BEAST_EXPECT(sle->getFieldU32(sfCouponCount) == 1);
            BEAST_EXPECT(sle->isFieldPresent(sfLastCouponTime));
        }
        BEAST_EXPECT(env.balance(alice, USD) == before - USD(200));
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
        auto const USD = gw["USD"];
        env.trust(USD(10'000), alice, bob);
        env(pay(gw, alice, USD(1'000)));
        env.close();

        MPTTester mpt{env, alice, {.holders = {bob}, .fund = false}};
        mpt.create({.flags = tfMPTCanTransfer});
        mpt.authorize({.account = bob});
        mpt.pay(alice, bob, 100);
        env.close();

        auto const id = mpt.issuanceID();
        env(scheduleCreate(alice, id, USD), Ter(tesSUCCESS));
        env.close();

        // Nothing declared yet: nothing to claim.
        env(couponClaim(bob, id), Ter(tecNO_PERMISSION));
        env.close();

        env(couponPay(alice, id, USD(2)), Ter(tesSUCCESS));
        env.close();

        auto const bobBefore = env.balance(bob, USD);
        env(couponClaim(bob, id), Ter(tesSUCCESS));
        env.close();

        // 100 units at 2 USD per unit.
        BEAST_EXPECT(env.balance(bob, USD) == bobBefore + USD(200));
        BEAST_EXPECT(accruedOf(env, id, bob) == STAmount{});

        auto const sle = scheduleOf(env, id);
        BEAST_EXPECT(sle);
        if (sle)
        {
            BEAST_EXPECT(sle->getFieldAmount(sfPoolAmount) == USD(0));
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
        auto const USD = gw["USD"];
        env.trust(USD(10'000), alice, bob);
        env(pay(gw, alice, USD(1'000)));
        env.close();

        MPTTester mpt{env, alice, {.holders = {bob}, .fund = false}};
        mpt.create({.flags = tfMPTCanTransfer});
        mpt.authorize({.account = bob});
        mpt.pay(alice, bob, 10);
        env.close();

        auto const id = mpt.issuanceID();
        env(scheduleCreate(alice, id, USD), Ter(tesSUCCESS));
        env.close();

        for (int i = 0; i < 3; ++i)
        {
            env(couponPay(alice, id, USD(1)), Ter(tesSUCCESS));
            env.close();
        }

        auto const bobBefore = env.balance(bob, USD);
        env(couponClaim(bob, id), Ter(tesSUCCESS));
        env.close();

        // Three coupons of 1 USD on 10 units.
        BEAST_EXPECT(env.balance(bob, USD) == bobBefore + USD(30));
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
        auto const USD = gw["USD"];
        env.trust(USD(10'000), alice, bob, carol);
        env(pay(gw, alice, USD(1'000)));
        env.close();

        MPTTester mpt{env, alice, {.holders = {bob, carol}, .fund = false}};
        mpt.create({.flags = tfMPTCanTransfer});
        mpt.authorize({.account = bob});
        mpt.authorize({.account = carol});
        mpt.pay(alice, bob, 100);
        env.close();

        auto const id = mpt.issuanceID();
        env(scheduleCreate(alice, id, USD), Ter(tesSUCCESS));
        env.close();

        env(couponPay(alice, id, USD(1)), Ter(tesSUCCESS));
        env.close();

        // bob sells the whole position to carol after the coupon is declared.
        mpt.pay(bob, carol, 100);
        env.close();

        // The coupon belongs to bob, who held the units when it was declared.
        BEAST_EXPECT(accruedOf(env, id, bob) == USD(100));
        BEAST_EXPECT(accruedOf(env, id, carol) == STAmount{});

        // A second coupon now belongs entirely to carol.
        env(couponPay(alice, id, USD(1)), Ter(tesSUCCESS));
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
        auto const USD = gw["USD"];
        env.trust(USD(10'000), alice, bob, dave);
        env(pay(gw, alice, USD(500)));
        env.close();

        MPTTester mpt{env, alice, {.holders = {bob}, .fund = false}};
        mpt.create({.flags = tfMPTCanTransfer});
        mpt.authorize({.account = bob});
        mpt.pay(alice, bob, 100);
        env.close();

        auto const id = mpt.issuanceID();
        env(scheduleCreate(alice, id, USD), Ter(tesSUCCESS));
        env.close();

        env(couponPay(alice, id, USD(2)), Ter(tesSUCCESS));
        env.close();

        // The payer spends everything else she has.
        env(pay(alice, dave, env.balance(alice, USD)), Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(env.balance(alice, USD) == USD(0));

        // The claim is still honored: the pool holds it, not the payer.
        auto const bobBefore = env.balance(bob, USD);
        env(couponClaim(bob, id), Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(env.balance(bob, USD) == bobBefore + USD(200));
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
        auto const USD = gw["USD"];
        env.trust(USD(10'000), alice, bob);
        env(pay(gw, alice, USD(1'000)));
        env.close();

        MPTTester mpt{env, alice, {.holders = {bob}, .fund = false}};
        mpt.create({.flags = tfMPTCanTransfer});
        mpt.authorize({.account = bob});
        mpt.pay(alice, bob, 100);
        env.close();

        auto const id = mpt.issuanceID();
        env(scheduleCreate(alice, id, USD), Ter(tesSUCCESS));
        env.close();

        // Units are outstanding.
        env(scheduleDelete(alice, id), Ter(tecHAS_OBLIGATIONS));
        env.close();

        env(couponPay(alice, id, USD(1)), Ter(tesSUCCESS));
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
        coupon.checkMPTokenAmount(bob, 200);
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
    }
};

BEAST_DEFINE_TESTSUITE(Coupon, app, xrpl);

}  // namespace xrpl
