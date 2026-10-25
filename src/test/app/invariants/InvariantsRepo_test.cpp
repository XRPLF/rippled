#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/flags.h>
#include <test/jtx/pay.h>
#include <test/jtx/trust.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/ApplyContext.h>

#include <functional>
#include <initializer_list>
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
            auto const usd = gw["USD"];
            env.fund(XRP(10000), gw);
            env.close();
            env.trust(usd(100000), a1, a2);
            env.close();

            auto const now = env.now().time_since_epoch().count();
            repoKey = keylet::repo(a1.id(), SeqProxy::rawSequence(env.seq(a1))).key;

            json::Value jv;
            jv[sfTransactionType] = jss::RepoCreate;
            jv[sfAccount] = a1.human();
            jv[sfCounterparty] = a2.human();
            jv[sfCollateralAmount] = XRP(10).value().getJson(JsonOptions::Values::None);
            jv[sfPurchasePrice] = usd(9).value().getJson(JsonOptions::Values::None);
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
            {
                .expected = "a repo's seller is its counterparty",
                .func = [](SLE::pointer& sle) { (*sle)[sfCounterparty] = (*sle)[sfAccount]; },
            },
            {
                .expected = "a repo's collateral or price is not positive",
                .func =
                    [](SLE::pointer& sle) { (*sle)[sfCollateralAmount] = STAmount{XRPAmount{0}}; },
            },
            {
                .expected = "a repo matures before it expires",
                .func =
                    [](SLE::pointer& sle) { (*sle)[sfMaturityDate] = (*sle)[sfExpiration] - 1; },
            },
            {
                .expected = "a repo's terms changed after creation",
                .func =
                    [](SLE::pointer& sle) { (*sle)[sfInterestRate] = (*sle)[sfInterestRate] + 1; },
            },
            {
                .expected = "a repo's terms changed after creation",
                .func =
                    [](SLE::pointer& sle) { (*sle)[sfCollateralAmount] = STAmount{XRPAmount{1}}; },
            },
            {
                .expected = "a repo's collateral or price is not positive",
                .func =
                    [&](SLE::pointer& sle) {
                        (*sle)[sfPurchasePrice] = STAmount{gw["USD"].issue()};
                    },
            },
            {
                .expected = "a repo's collateral and cash are the same asset",
                .func = [](SLE::pointer& sle) { (*sle)[sfPurchasePrice] = STAmount{XRPAmount{9}}; },
            },
            {
                .expected = "a repo's terms changed after creation",
                .func = [&](SLE::pointer& sle) { (*sle)[sfAccount] = gw.id(); },
            },
            {
                .expected = "a repo's terms changed after creation",
                .func = [&](SLE::pointer& sle) { (*sle)[sfCounterparty] = gw.id(); },
            },
            {
                .expected = "a repo's terms changed after creation",
                .func = [&](SLE::pointer& sle) { (*sle)[sfPurchasePrice] = gw["USD"](8).value(); },
            },
            {
                .expected = "a repo's terms changed after creation",
                .func =
                    [&](SLE::pointer& sle) { (*sle)[sfCollateralAmount] = gw["EUR"](10).value(); },
            },
            {
                .expected = "a repo's terms changed after creation",
                .func = [&](SLE::pointer& sle) { (*sle)[sfPurchasePrice] = gw["EUR"](9).value(); },
            },
            {
                .expected = "a repo's terms changed after creation",
                .func =
                    [](SLE::pointer& sle) { (*sle)[sfMaturityDate] = (*sle)[sfMaturityDate] + 1; },
            },
            {
                .expected = "a repo's terms changed after creation",
                .func = [](SLE::pointer& sle) { (*sle)[sfExpiration] = (*sle)[sfExpiration] - 1; },
            },
            {
                .expected = "a repo's terms changed after creation",
                .func =
                    [](SLE::pointer& sle) { (*sle)[sfGracePeriod] = (*sle)[sfGracePeriod] + 1; },
            },
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
            auto const usd = gw["USD"];
            env.fund(XRP(10000), gw);
            env.close();
            env.trust(usd(100000), a1, a2);
            env.close();
            env(pay(gw, a2, usd(5000)));
            env.close();

            auto const now = env.now().time_since_epoch().count();
            repoKey = keylet::repo(a1.id(), SeqProxy::rawSequence(env.seq(a1))).key;

            json::Value jv;
            jv[sfTransactionType] = jss::RepoCreate;
            jv[sfAccount] = a1.human();
            jv[sfCounterparty] = a2.human();
            jv[sfCollateralAmount] = XRP(10).value().getJson(JsonOptions::Values::None);
            jv[sfPurchasePrice] = usd(9).value().getJson(JsonOptions::Values::None);
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
                auto const sle = ac.view().peek(keylet::repo(repoKey));
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

        // Touching an active repo without restating its terms or its start
        // date is allowed.
        doInvariantCheck(
            {},
            [&](Account const&, Account const&, ApplyContext& ac) {
                auto const sle = ac.view().peek(keylet::repo(repoKey));
                if (!sle || !sle->isFieldPresent(sfStartDate))
                    return false;
                sle->setFieldVL(sfData, Blob{1, 2, 3});
                ac.view().update(sle);
                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_SET, [](STObject&) {}},
            {tesSUCCESS, tesSUCCESS},
            createActiveRepo);

        doInvariantCheck(
            {{"a repo's start date changed"}},
            [&](Account const&, Account const&, ApplyContext& ac) {
                auto const sle = ac.view().peek(keylet::repo(repoKey));
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
            auto const usd = gw["USD"];
            env.fund(XRP(10000), gw);
            env.close();
            env.trust(usd(100000), a1, a2);
            env.close();

            auto const now = env.now().time_since_epoch().count();
            repoKey = keylet::repo(a1.id(), SeqProxy::rawSequence(env.seq(a1))).key;

            json::Value jv;
            jv[sfTransactionType] = jss::RepoCreate;
            jv[sfAccount] = a1.human();
            jv[sfCounterparty] = a2.human();
            jv[sfCollateralAmount] = XRP(10).value().getJson(JsonOptions::Values::None);
            jv[sfPurchasePrice] = usd(9).value().getJson(JsonOptions::Values::None);
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
                auto const sle = ac.view().peek(keylet::repo(repoKey));
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

    // A closed repo holds the repurchase amount for the buyer: in the cash
    // asset, at least the price, fixed once set, and never removed short of
    // deleting the entry.
    void
    testClosedRepoHoldsItsAmount()
    {
        testcase("a closed repo holds its amount");
        using namespace jtx;

        uint256 repoKey;
        Account const gw{"gw"};
        auto const usd = gw["USD"];

        auto const repoTx = [&](Account const& seller, Account const& buyer, Env& env) {
            auto const now = env.now().time_since_epoch().count();
            repoKey = keylet::repo(seller.id(), SeqProxy::rawSequence(env.seq(seller))).key;
            json::Value jv;
            jv[sfTransactionType] = jss::RepoCreate;
            jv[sfAccount] = seller.human();
            jv[sfCounterparty] = buyer.human();
            jv[sfCollateralAmount] = XRP(10).value().getJson(JsonOptions::Values::None);
            jv[sfPurchasePrice] = usd(9).value().getJson(JsonOptions::Values::None);
            jv[sfInterestRate] = 0;
            jv[sfExpiration] = now + 100;
            jv[sfMaturityDate] = now + 1000;
            jv[sfGracePeriod] = 60;
            env(jv);
            env.close();
        };
        auto const action = [&](json::StaticString const type, Account const& account) {
            json::Value jv;
            jv[sfTransactionType] = type;
            jv[sfAccount] = account.human();
            jv[sfRepoID] = to_string(repoKey);
            return jv;
        };

        // An active repo with USD cash under an issuer requiring authorization.
        auto const createActiveRepo = [&](Account const& a1, Account const& a2, Env& env) {
            env.fund(XRP(10000), gw);
            env.close();
            env(fset(gw, asfRequireAuth));
            env.close();
            env.trust(usd(100000), a1, a2);
            env.close();
            env(trust(gw, usd(0), a1, tfSetfAuth));
            env(trust(gw, usd(0), a2, tfSetfAuth));
            env.close();
            env(pay(gw, a2, usd(9)));
            env.close();
            repoTx(a1, a2, env);
            env(action(jss::RepoAccept, a2));
            env.close();
            return true;
        };

        // The buyer then deletes its line, so the seller's close holds the cash.
        auto const createClosedRepo = [&](Account const& a1, Account const& a2, Env& env) {
            createActiveRepo(a1, a2, env);
            env(trust(a2, usd(0)));
            env.close();
            env(action(jss::RepoClose, a1));
            env.close();
            return true;
        };

        auto const check = [&](std::vector<std::string> const& expected,
                               std::function<void(SLE::pointer&)> const& mod,
                               std::initializer_list<TER> ters,
                               auto const& preclose) {
            doInvariantCheck(
                expected,
                [&](Account const&, Account const&, ApplyContext& ac) {
                    auto sle = ac.view().peek(keylet::repo(repoKey));
                    if (!sle)
                        return false;
                    mod(sle);
                    ac.view().update(sle);
                    return true;
                },
                XRPAmount{},
                STTx{ttACCOUNT_SET, [](STObject&) {}},
                ters,
                preclose);
        };
        std::initializer_list<TER> const failed{tecINVARIANT_FAILED, tefINVARIANT_FAILED};

        check(
            {{"a closed repo holds another asset or less than the price"}},
            [&](SLE::pointer& sle) { (*sle)[sfAmount] = STAmount{XRPAmount{9}}; },
            failed,
            createActiveRepo);
        check(
            {{"a closed repo holds another asset or less than the price"}},
            [&](SLE::pointer& sle) { (*sle)[sfAmount] = usd(8).value(); },
            failed,
            createActiveRepo);
        check(
            {{"a closed repo's held amount changed"}},
            [&](SLE::pointer& sle) { (*sle)[sfAmount] = usd(10).value(); },
            failed,
            createClosedRepo);
        check(
            {{"a closed repo reopened"}},
            [&](SLE::pointer& sle) { sle->makeFieldAbsent(sfAmount); },
            failed,
            createClosedRepo);
        check(
            {},
            [&](SLE::pointer& sle) { sle->setFieldVL(sfData, Blob{1, 2, 3}); },
            {tesSUCCESS, tesSUCCESS},
            createClosedRepo);
    }

public:
    void
    run() override
    {
        testRepoTermsAreImmutable();
        testRepoOnlyMovesForward();
        testRepoDeletedByWrongTransaction();
        testClosedRepoHoldsItsAmount();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsRepo, app, xrpl);

}  // namespace xrpl::test
