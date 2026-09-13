#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/pay.h>
#include <test/jtx/trust.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/jss.h>

#include <functional>
#include <string>
#include <vector>

namespace xrpl::test {

class InvariantsRepo_test : public InvariantsBase
{
    // A repo's terms are struck once. Each case below restates one of them
    // after the fact, which the invariant must reject.
    void
    testRepoTermsAreImmutable()
    {
        testcase("repo terms are immutable");
        using namespace jtx;

        uint256 repoKey;
        Account const gw{"gw"};

        auto const createRepo = [&](Account const& a1, Account const& a2, Env& env) {
            auto const USD = gw["USD"];
            env.fund(XRP(10000), gw);
            env.close();
            env.trust(USD(100000), a1, a2);
            env.close();

            auto const now = env.now().time_since_epoch().count();
            repoKey = keylet::repo(a1.id(), SeqProxy::rawSequence(env.seq(a1))).key;

            json::Value jv;
            jv[sfTransactionType] = jss::RepoCreate;
            jv[sfAccount] = a1.human();
            jv[sfCounterparty] = a2.human();
            jv[sfCollateralAmount] = XRP(10).value().getJson(JsonOptions::Values::None);
            jv[sfPurchasePrice] = USD(9).value().getJson(JsonOptions::Values::None);
            jv[sfInterestRate] = 0;
            jv[sfExpiration] = now + 100;
            jv[sfMaturityDate] = now + 1000;
            jv[sfGracePeriod] = 60;
            env(jv);
            env.close();
            return true;
        };

        struct Mod
        {
            std::string expected;
            std::function<void(SLE::pointer&)> func;
        };

        std::vector<Mod> const mods{
            {"a repo's seller is its counterparty",
             [](SLE::pointer& sle) { (*sle)[sfCounterparty] = (*sle)[sfAccount]; }},
            {"a repo's collateral or price is not positive",
             [](SLE::pointer& sle) { (*sle)[sfCollateralAmount] = STAmount{XRPAmount{0}}; }},
            {"a repo matures before it expires",
             [](SLE::pointer& sle) { (*sle)[sfMaturityDate] = (*sle)[sfExpiration] - 1; }},
            {"a repo's terms changed after creation",
             [](SLE::pointer& sle) { (*sle)[sfInterestRate] = (*sle)[sfInterestRate] + 1; }},
            {"a repo's terms changed after creation",
             [](SLE::pointer& sle) { (*sle)[sfCollateralAmount] = STAmount{XRPAmount{1}}; }},
        };

        for (auto const& mod : mods)
        {
            doInvariantCheck(
                {{mod.expected}},
                [&](Account const&, Account const&, ApplyContext& ac) {
                    auto sle = ac.view().peek(keylet::repo(repoKey));
                    if (!sle)
                        return false;
                    mod.func(sle);
                    ac.view().update(sle);
                    return true;
                },
                XRPAmount{},
                STTx{ttACCOUNT_SET, [](STObject&) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                createRepo);
        }
    }

    // A repo moves from pending to active and never back, and the start date
    // it was stamped with is the one interest is computed from.
    void
    testRepoOnlyMovesForward()
    {
        testcase("repo only moves forward");
        using namespace jtx;

        uint256 repoKey;
        Account const gw{"gw"};

        auto const createActiveRepo = [&](Account const& a1, Account const& a2, Env& env) {
            auto const USD = gw["USD"];
            env.fund(XRP(10000), gw);
            env.close();
            env.trust(USD(100000), a1, a2);
            env.close();
            env(pay(gw, a2, USD(5000)));
            env.close();

            auto const now = env.now().time_since_epoch().count();
            repoKey = keylet::repo(a1.id(), SeqProxy::rawSequence(env.seq(a1))).key;

            json::Value jv;
            jv[sfTransactionType] = jss::RepoCreate;
            jv[sfAccount] = a1.human();
            jv[sfCounterparty] = a2.human();
            jv[sfCollateralAmount] = XRP(10).value().getJson(JsonOptions::Values::None);
            jv[sfPurchasePrice] = USD(9).value().getJson(JsonOptions::Values::None);
            jv[sfInterestRate] = 0;
            jv[sfExpiration] = now + 100;
            jv[sfMaturityDate] = now + 1000;
            jv[sfGracePeriod] = 60;
            env(jv);
            env.close();

            json::Value accept;
            accept[sfTransactionType] = jss::RepoAccept;
            accept[sfAccount] = a2.human();
            accept[sfRepoID] = to_string(repoKey);
            env(accept);
            env.close();
            return true;
        };

        doInvariantCheck(
            {{"an active repo reverted to pending"}},
            [&](Account const&, Account const&, ApplyContext& ac) {
                auto sle = ac.view().peek(keylet::repo(repoKey));
                if (!sle || !sle->isFieldPresent(sfStartDate))
                    return false;
                sle->makeFieldAbsent(sfStartDate);
                ac.view().update(sle);
                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            createActiveRepo);

        doInvariantCheck(
            {{"a repo's start date changed"}},
            [&](Account const&, Account const&, ApplyContext& ac) {
                auto sle = ac.view().peek(keylet::repo(repoKey));
                if (!sle || !sle->isFieldPresent(sfStartDate))
                    return false;
                (*sle)[sfStartDate] = (*sle)[sfStartDate] + 1;
                ac.view().update(sle);
                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            createActiveRepo);
    }

    // A repo may only be removed by cancel, close or default.
    void
    testRepoDeletedByWrongTransaction()
    {
        testcase("repo deleted by the wrong transaction");
        using namespace jtx;

        uint256 repoKey;
        Account const gw{"gw"};

        auto const createRepo = [&](Account const& a1, Account const& a2, Env& env) {
            auto const USD = gw["USD"];
            env.fund(XRP(10000), gw);
            env.close();
            env.trust(USD(100000), a1, a2);
            env.close();

            auto const now = env.now().time_since_epoch().count();
            repoKey = keylet::repo(a1.id(), SeqProxy::rawSequence(env.seq(a1))).key;

            json::Value jv;
            jv[sfTransactionType] = jss::RepoCreate;
            jv[sfAccount] = a1.human();
            jv[sfCounterparty] = a2.human();
            jv[sfCollateralAmount] = XRP(10).value().getJson(JsonOptions::Values::None);
            jv[sfPurchasePrice] = USD(9).value().getJson(JsonOptions::Values::None);
            jv[sfInterestRate] = 0;
            jv[sfExpiration] = now + 100;
            jv[sfMaturityDate] = now + 1000;
            jv[sfGracePeriod] = 60;
            env(jv);
            env.close();
            return true;
        };

        doInvariantCheck(
            {{"a repo was deleted by transaction type"}},
            [&](Account const&, Account const&, ApplyContext& ac) {
                auto sle = ac.view().peek(keylet::repo(repoKey));
                if (!sle)
                    return false;
                ac.view().erase(sle);
                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            createRepo);
    }

public:
    void
    run() override
    {
        testRepoTermsAreImmutable();
        testRepoOnlyMovesForward();
        testRepoDeletedByWrongTransaction();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsRepo, app, xrpl);

}  // namespace xrpl::test
