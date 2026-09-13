#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/ApplyContext.h>

namespace xrpl::test {

class InvariantsCoupon_test : public InvariantsBase
{
    FeatureBitset const all_{test::jtx::testableAmendments()};

    void
    testValidCouponSchedule()
    {
        using namespace test::jtx;
        testcase << "coupon schedule invariants";

        // Build an issuance with a coupon schedule, then corrupt it.
        auto setup = [&](Env& env, Account const& issuer, Account const& holder) {
            MPTTester mpt(
                {.env = env, .issuer = issuer, .holders = {holder}, .pay = 100, .maxAmt = 100});
            auto const id = mpt.issuanceID();
            json::Value jv;
            jv[jss::TransactionType] = jss::CouponScheduleCreate;
            jv[jss::Account] = issuer.human();
            jv[sfMPTokenIssuanceID] = to_string(id);
            jv[sfCouponAsset] = toJson(xrpIssue());
            env(jv);
            env.close();
            return id;
        };

        {
            Env env(*this, all_);
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            env.fund(XRP(10'000), issuer, holder);
            auto const id = setup(env, issuer, holder);

            doInvariantCheck(
                std::move(env),
                issuer,
                holder,
                {{"Invariant failed: coupon accumulator decreased"}},
                [id](Account const&, Account const&, ApplyContext& ac) {
                    auto sle = ac.view().peek(keylet::couponSchedule(id));
                    if (!sle)
                        return false;
                    sle->at(sfAccruedPerUnit) = XRP(-1).value();
                    ac.view().update(sle);
                    return true;
                },
                XRPAmount{},
                STTx{ttCOUPON_PAY, [](STObject&) {}},
                {tecINVARIANT_FAILED, tecINVARIANT_FAILED});
        }

        {
            Env env(*this, all_);
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            env.fund(XRP(10'000), issuer, holder);
            auto const id = setup(env, issuer, holder);

            doInvariantCheck(
                std::move(env),
                issuer,
                holder,
                {{"Invariant failed: coupon accumulator moved outside CouponPay"}},
                [id](Account const&, Account const&, ApplyContext& ac) {
                    auto sle = ac.view().peek(keylet::couponSchedule(id));
                    if (!sle)
                        return false;
                    sle->at(sfAccruedPerUnit) = XRP(5).value();
                    ac.view().update(sle);
                    return true;
                },
                XRPAmount{},
                STTx{ttACCOUNT_SET, [](STObject&) {}},
                {tecINVARIANT_FAILED, tecINVARIANT_FAILED});
        }

        {
            Env env(*this, all_);
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            env.fund(XRP(10'000), issuer, holder);
            auto const id = setup(env, issuer, holder);

            doInvariantCheck(
                std::move(env),
                issuer,
                holder,
                {{"Invariant failed: coupon pool grew outside CouponPay"}},
                [id](Account const&, Account const&, ApplyContext& ac) {
                    auto sle = ac.view().peek(keylet::couponSchedule(id));
                    if (!sle)
                        return false;
                    sle->at(sfPoolAmount) = XRP(10).value();
                    ac.view().update(sle);
                    return true;
                },
                XRPAmount{},
                STTx{ttACCOUNT_SET, [](STObject&) {}},
                {tecINVARIANT_FAILED, tecINVARIANT_FAILED});
        }
    }

public:
    void
    run() override
    {
        testValidCouponSchedule();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsCoupon, app, xrpl);

}  // namespace xrpl::test
