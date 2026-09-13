#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/jss.h>

namespace xrpl::test {

class InvariantsBeneficiary_test : public InvariantsBase
{
    // Put a designation on a1 so the checks have something to corrupt.
    static bool
    designate(jtx::Account const& a1, jtx::Account const& a2, jtx::Env& env)
    {
        json::Value jv;
        jv[sfTransactionType] = jss::BeneficiarySet;
        jv[sfAccount] = a1.human();
        jv[sfBeneficiary] = a2.human();
        jv[sfTimeLock] = 3600;
        env(jv);
        env.close();
        return true;
    }

    void
    testEntryAndStampTravelTogether()
    {
        testcase("a designation and its timestamp travel together");
        using namespace jtx;

        // The entry appears with no timestamp beside it.
        doInvariantCheck(
            {{"a beneficiary designation was created without its LastInteraction"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto sle = std::make_shared<SLE>(keylet::beneficiary(a1.id()));
                (*sle)[sfAccount] = a1.id();
                (*sle)[sfBeneficiary] = a2.id();
                (*sle)[sfTimeLock] = 3600;
                (*sle)[sfOwnerNode] = 0;
                ac.view().insert(sle);
                return true;
            });

        // The timestamp appears with no entry beside it.
        doInvariantCheck(
            {{"a beneficiary designation was created without its LastInteraction"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                auto sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;
                sle->setFieldU32(sfLastInteraction, 1);
                ac.view().update(sle);
                return true;
            });

        // The entry goes and the timestamp stays behind.
        doInvariantCheck(
            {{"a beneficiary designation was removed without its LastInteraction"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                auto sle = ac.view().peek(keylet::beneficiary(a1.id()));
                if (!sle)
                    return false;
                ac.view().erase(sle);
                return true;
            },
            XRPAmount{},
            STTx{ttBENEFICIARY_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            designate);
    }

    void
    testFieldBounds()
    {
        testcase("the designation's fields are bounded");
        using namespace jtx;

        doInvariantCheck(
            {{"an account is its own beneficiary"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                auto sle = ac.view().peek(keylet::beneficiary(a1.id()));
                if (!sle)
                    return false;
                (*sle)[sfBeneficiary] = a1.id();
                ac.view().update(sle);
                return true;
            },
            XRPAmount{},
            STTx{ttBENEFICIARY_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            designate);

        doInvariantCheck(
            {{"the beneficiary time lock is out of range"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                auto sle = ac.view().peek(keylet::beneficiary(a1.id()));
                if (!sle)
                    return false;
                (*sle)[sfTimeLock] = 0;
                ac.view().update(sle);
                return true;
            },
            XRPAmount{},
            STTx{ttBENEFICIARY_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            designate);

        doInvariantCheck(
            {{"the beneficiary time lock is out of range"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                auto sle = ac.view().peek(keylet::beneficiary(a1.id()));
                if (!sle)
                    return false;
                (*sle)[sfTimeLock] = kMaxBeneficiaryTimeLock + 1;
                ac.view().update(sle);
                return true;
            },
            XRPAmount{},
            STTx{ttBENEFICIARY_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            designate);

        doInvariantCheck(
            {{"the beneficiary entry changed account"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                auto sle = ac.view().peek(keylet::beneficiary(a1.id()));
                if (!sle)
                    return false;
                (*sle)[sfAccount] = Account{"someone else"}.id();
                ac.view().update(sle);
                return true;
            },
            XRPAmount{},
            STTx{ttBENEFICIARY_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            designate);
    }

    void
    testTimerNeverGoesBackwards()
    {
        testcase("the timestamp never moves backwards");
        using namespace jtx;

        doInvariantCheck(
            {{"LastInteraction moved backwards"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                auto sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle || !sle->isFieldPresent(sfLastInteraction) ||
                    (*sle)[sfLastInteraction] == 0)
                    return false;
                sle->setFieldU32(sfLastInteraction, 1);
                ac.view().update(sle);
                return true;
            },
            XRPAmount{},
            STTx{ttBENEFICIARY_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [](Account const& a1, Account const& a2, jtx::Env& env) {
                // The ledger clock starts at zero, so it is moved forward
                // before the designation is made; otherwise there is no
                // earlier value for the timestamp to move back to.
                env.close(std::chrono::seconds(100000));
                return designate(a1, a2, env);
            });
    }

    void
    testDeletedByWrongTransaction()
    {
        testcase("a designation is deleted by the wrong transaction");
        using namespace jtx;

        doInvariantCheck(
            {{"a beneficiary designation was deleted by transaction type"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                auto sle = ac.view().peek(keylet::beneficiary(a1.id()));
                auto sleAcct = ac.view().peek(keylet::account(a1.id()));
                if (!sle || !sleAcct)
                    return false;
                ac.view().erase(sle);
                sleAcct->makeFieldAbsent(sfLastInteraction);
                ac.view().update(sleAcct);
                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            designate);
    }

public:
    void
    run() override
    {
        testEntryAndStampTravelTogether();
        testFieldBounds();
        testTimerNeverGoesBackwards();
        testDeletedByWrongTransaction();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsBeneficiary, app, xrpl);

}  // namespace xrpl::test
