#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/mpt.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/ApplyContext.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

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
        auto const setup = [&](Env& env, Account const& issuer, Account const& holder) {
            MPTTester const mpt(
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
                    auto const sle = ac.view().peek(keylet::couponSchedule(id));
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
                    auto const sle = ac.view().peek(keylet::couponSchedule(id));
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
                    auto const sle = ac.view().peek(keylet::couponSchedule(id));
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

        // A schedule with one XRP coupon paid and the holder settled at index 1 XRP.
        auto const setupSettled = [&](Env& env, Account const& issuer, Account const& holder) {
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
            json::Value pay;
            pay[jss::TransactionType] = jss::CouponPay;
            pay[jss::Account] = issuer.human();
            pay[sfMPTokenIssuanceID] = to_string(id);
            pay[sfAmount] = toJson(STAmount{XRP(1)});
            env(pay);
            env.close();
            mpt.pay(holder, issuer, 1);
            env.close();
            return id;
        };

        // Each case corrupts one field of the settled state under an unrelated transaction.
        struct Corruption
        {
            std::string log;
            std::function<bool(MPTID const&, AccountID const&, ApplyContext&)> apply;
        };
        std::vector<Corruption> const corruptions{
            {
                .log = "Invariant failed: coupon pool is negative",
                .apply =
                    [](MPTID const& id, AccountID const&, ApplyContext& ac) {
                        auto const sle = ac.view().peek(keylet::couponSchedule(id));
                        if (!sle)
                            return false;
                        sle->at(sfPoolAmount) = XRP(-1).value();
                        ac.view().update(sle);
                        return true;
                    },
            },
            {
                .log = "Invariant failed: coupon pool shrank outside a claim or delete",
                .apply =
                    [](MPTID const& id, AccountID const&, ApplyContext& ac) {
                        auto const sle = ac.view().peek(keylet::couponSchedule(id));
                        if (!sle)
                            return false;
                        sle->at(sfPoolAmount) = XRP(50).value();
                        ac.view().update(sle);
                        return true;
                    },
            },
            {
                .log = "Invariant failed: holder coupon index moved backwards",
                .apply =
                    [](MPTID const& id, AccountID const& holder, ApplyContext& ac) {
                        auto const sle = ac.view().peek(keylet::mptoken(id, holder));
                        if (!sle)
                            return false;
                        sle->at(sfCouponIndex) = XRP(0).value();
                        ac.view().update(sle);
                        return true;
                    },
            },
            {
                .log = "Invariant failed: holder coupon index removed while owed",
                .apply =
                    [](MPTID const& id, AccountID const& holder, ApplyContext& ac) {
                        auto const sle = ac.view().peek(keylet::mptoken(id, holder));
                        if (!sle)
                            return false;
                        sle->makeFieldAbsent(sfCouponIndex);
                        ac.view().update(sle);
                        return true;
                    },
            },
        };

        for (auto const& corruption : corruptions)
        {
            Env env(*this, all_);
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            env.fund(XRP(10'000), issuer, holder);
            auto const id = setupSettled(env, issuer, holder);

            doInvariantCheck(
                std::move(env),
                holder,
                issuer,
                {corruption.log},
                [id, &corruption](Account const& a1, Account const&, ApplyContext& ac) {
                    return corruption.apply(id, a1.id(), ac);
                },
                XRPAmount{},
                STTx{ttACCOUNT_SET, [](STObject&) {}},
                {tecINVARIANT_FAILED, tecINVARIANT_FAILED});
        }

        // Only CouponScheduleDelete may clear lsfMPTCouponSchedule.
        auto const clearScheduleFlag = [](MPTID const& id) {
            return [id](Account const&, Account const&, ApplyContext& ac) {
                auto const sle = ac.view().peek(keylet::mptokenIssuance(id));
                if (!sle)
                    return false;
                sle->clearFlag(lsfMPTCouponSchedule);
                ac.view().update(sle);
                return true;
            };
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
                {{"Invariant failed: immutable MPTokenIssuance flag cleared"}},
                clearScheduleFlag(id),
                XRPAmount{},
                STTx{ttACCOUNT_SET, [](STObject&) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
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
                {},
                clearScheduleFlag(id),
                XRPAmount{},
                STTx{ttCOUPON_SCHEDULE_DELETE, [](STObject&) {}},
                {tesSUCCESS, tesSUCCESS});
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
