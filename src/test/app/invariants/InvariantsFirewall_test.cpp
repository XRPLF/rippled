#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/tx/ApplyContext.h>

namespace xrpl::test {

class InvariantsFirewall_test : public InvariantsBase
{
    // Creates a firewall for the first account with the second as its
    // counterparty and backup, so the precheck can corrupt a real entry.
    static bool
    createFirewall(jtx::Account const& a1, jtx::Account const& a2, jtx::Env& env)
    {
        env(jtx::firewall::set(a1.id(), a2.id(), a2.id()));
        return env.le(keylet::firewall(a1.id())) != nullptr;
    }

    void
    testCounterpartyIsOwner()
    {
        testcase("firewall counterparty is its owner");
        using namespace jtx;

        doInvariantCheck(
            {"Invariant failed: a firewall's counterparty is its owner"},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                auto const sle = ac.view().peek(keylet::firewall(a1.id()));
                if (!sle)
                    return false;
                sle->setAccountID(sfCounterparty, a1.id());
                ac.view().update(sle);
                return true;
            },
            XRPAmount{},
            STTx{ttFIREWALL_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            createFirewall);
    }

    void
    testOwnerChanged()
    {
        testcase("firewall owner changed");
        using namespace jtx;

        doInvariantCheck(
            {"Invariant failed: a firewall's owner changed"},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                auto const sle = ac.view().peek(keylet::firewall(a1.id()));
                if (!sle)
                    return false;
                sle->setAccountID(sfOwner, Account{"carol"}.id());
                ac.view().update(sle);
                return true;
            },
            XRPAmount{},
            STTx{ttFIREWALL_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            createFirewall);
    }

public:
    void
    run() override
    {
        testCounterpartyIsOwner();
        testOwnerChanged();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsFirewall, app, xrpl);

}  // namespace xrpl::test
