#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STCurrency.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/ApplyContext.h>

#include <functional>
#include <memory>
#include <string>

namespace xrpl::test {

class InvariantsTokenIssuance_test : public InvariantsBase
{
    static json::Value
    tiCreate(jtx::Account const& account, std::string const& currency)
    {
        json::Value jv;
        jv[jss::TransactionType] = jss::TokenIssuanceCreate;
        jv[jss::Account] = account.human();
        jv[sfCurrency.jsonName] = currency;
        return jv;
    }

    // Inserts a TokenIssuance as TokenIssuanceCreate's doApply would.
    static bool
    insertIssuance(jtx::Account const& issuer, ApplyContext& ac)
    {
        Currency const currency = issuer["USD"].currency;
        auto const sle = std::make_shared<SLE>(keylet::tokenIssuance(issuer.id(), currency));
        (*sle)[sfIssuer] = issuer.id();
        sle->setFieldCurrency(sfCurrency, STCurrency{sfCurrency, currency});
        (*sle)[sfOwnerNode] = 0;
        ac.view().insert(sle);
        return true;
    }

    static SLE::pointer
    peekIssuance(jtx::Account const& issuer, ApplyContext& ac)
    {
        return ac.view().peek(keylet::tokenIssuance(issuer.id(), issuer["USD"].currency));
    }

    void
    testWithoutAmendment()
    {
        using namespace jtx;

        testcase("TokenIssuance created without the amendment");
        doInvariantCheck(
            makeEnv(testableAmendments() - featureTokenIssuance),
            {{"TokenIssuance changed without amendment"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                return insertIssuance(a1, ac);
            });
    }

    void
    testPrivileges()
    {
        using namespace jtx;

        Preclose const createIssuance = [](Account const& a1, Account const&, Env& env) {
            env(tiCreate(a1, "USD"));
            return true;
        };

        testcase("TokenIssuance created without privilege");
        doInvariantCheck(
            {{"TokenIssuance created without privilege"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                return insertIssuance(a1, ac);
            });

        testcase("TokenIssuance deleted without privilege");
        doInvariantCheck(
            {{"TokenIssuance deleted without privilege"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                auto const sle = peekIssuance(a1, ac);
                if (!sle)
                    return false;
                ac.view().erase(sle);
                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            createIssuance);

        // These checks judge only a successful result, so the second pass, which sees the
        // first pass's tec, leaves it as tec.
        testcase("TokenIssuanceCreate that creates nothing");
        doInvariantCheck(
            {{"TokenIssuanceCreate succeeded without creating exactly one issuance"}},
            [](Account const&, Account const&, ApplyContext&) { return true; },
            XRPAmount{},
            STTx{ttTOKEN_ISSUANCE_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        testcase("TokenIssuanceDestroy that deletes nothing");
        doInvariantCheck(
            {{"TokenIssuanceDestroy succeeded without deleting exactly one issuance"}},
            [](Account const&, Account const&, ApplyContext&) { return true; },
            XRPAmount{},
            STTx{ttTOKEN_ISSUANCE_DESTROY, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});
    }

    void
    testDeletedWithSupply()
    {
        using namespace jtx;

        testcase("TokenIssuance deleted with outstanding supply");
        doInvariantCheck(
            {{"TokenIssuance deleted with outstanding IssuedAmount"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                auto const sle = peekIssuance(a1, ac);
                if (!sle)
                    return false;
                ac.view().erase(sle);
                return true;
            },
            XRPAmount{},
            STTx{ttTOKEN_ISSUANCE_DESTROY, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [](Account const& a1, Account const& a2, Env& env) {
                env(tiCreate(a1, "USD"));
                env.trust(a1["USD"](100), a2);
                env(pay(a1, a2, a1["USD"](10)));
                return true;
            });
    }

    void
    testImmutableFields()
    {
        using namespace jtx;

        Preclose const createCapped = [](Account const& a1, Account const&, Env& env) {
            auto jv = tiCreate(a1, "USD");
            jv[sfMaximumAmount.jsonName] = "1000";
            jv[sfTokenScale.jsonName] = 0;
            env(jv);
            return true;
        };

        auto const expectImmutable = [&](std::string const& name,
                                         std::function<void(Account const&, SLE&)> const& change,
                                         Preclose const& preclose) {
            testcase("TokenIssuance immutable " + name);
            doInvariantCheck(
                {{"TokenIssuance immutable field changed"}},
                [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                    auto const sle = peekIssuance(a1, ac);
                    if (!sle)
                        return false;
                    change(a2, *sle);
                    ac.view().update(sle);
                    return true;
                },
                XRPAmount{},
                STTx{ttTOKEN_ISSUANCE_SET, [](STObject&) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                preclose);
        };

        expectImmutable(
            "MaximumAmount",
            [](Account const&, SLE& sle) { sle[sfMaximumAmount] = 2'000; },
            createCapped);
        expectImmutable(
            "TokenScale", [](Account const&, SLE& sle) { sle[sfTokenScale] = 2; }, createCapped);
        expectImmutable(
            "Issuer", [](Account const& a2, SLE& sle) { sle[sfIssuer] = a2.id(); }, createCapped);
        expectImmutable(
            "Currency",
            [](Account const& a2, SLE& sle) {
                sle.setFieldCurrency(sfCurrency, STCurrency{sfCurrency, a2["EUR"].currency});
            },
            createCapped);

        expectImmutable(
            "MPTokenIssuanceID",
            [](Account const& a2, SLE& sle) { sle[sfMPTokenIssuanceID] = makeMptID(1, a2.id()); },
            [](Account const& a1, Account const&, Env& env) {
                MPTTester mpt(env, a1, {.fund = false});
                mpt.create({.maxAmt = 1'000, .assetScale = 0});
                auto jv = tiCreate(a1, "USD");
                jv[sfMaximumAmount.jsonName] = "1000";
                jv[sfTokenScale.jsonName] = 0;
                jv[sfMPTokenIssuanceID.jsonName] = to_string(mpt.issuanceID());
                env(jv);
                return true;
            });
    }

public:
    void
    run() override
    {
        testWithoutAmendment();
        testPrivileges();
        testDeletedWithSupply();
        testImmutableFields();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsTokenIssuance, app, xrpl);

}  // namespace xrpl::test
