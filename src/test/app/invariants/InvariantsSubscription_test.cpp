#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/subscription.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>

#include <optional>

namespace xrpl::test {

class InvariantsSubscription_test : public InvariantsBase
{
    void
    testSubscriptionInvariants()
    {
        using namespace jtx;
        using namespace std::chrono_literals;

        // Creates a real subscription from the first account to the second, starting after now so
        // NextClaimTime is above zero, and in the closed ledger before the precheck modifies it.
        std::optional<Keylet> subKeylet;
        Preclose const precloseSubscription =
            [&](Account const& owner, Account const& destination, Env& env) {
                subKeylet = keylet::subscription(owner.id(), destination.id(), env.seq(owner));
                env(subscription::create(owner, destination, XRP(10), 100s),
                    subscription::StartTime(env.now() + 1000s));
                return BEAST_EXPECT(env.le(*subKeylet));
            };

        auto const peekSubscription = [&](ApplyContext& ac) -> SLE::pointer {
            if (!subKeylet)
                return nullptr;
            return ac.view().peek(*subKeylet);
        };

        auto const features = testableAmendments() | featureSubscription;

        testcase("Subscription balance is negative");
        doInvariantCheck(
            makeEnv(features),
            {{"subscription balance is negative"}},
            [&](Account const&, Account const&, ApplyContext& ac) {
                auto const sle = peekSubscription(ac);
                if (!sle)
                    return false;
                sle->setFieldAmount(sfBalance, XRPAmount{-1});
                ac.view().update(sle);
                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseSubscription);

        testcase("Subscription balance and amount assets differ");
        doInvariantCheck(
            makeEnv(features),
            {{"subscription balance and amount are denominated in different assets"}},
            [&](Account const& owner, Account const&, ApplyContext& ac) {
                auto const sle = peekSubscription(ac);
                if (!sle)
                    return false;
                sle->setFieldAmount(sfBalance, owner["USD"](10));
                ac.view().update(sle);
                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseSubscription);

        testcase("Subscription account and destination are the same");
        doInvariantCheck(
            makeEnv(features),
            {{"subscription account and destination are the same"}},
            [&](Account const& owner, Account const&, ApplyContext& ac) {
                auto const sle = peekSubscription(ac);
                if (!sle)
                    return false;
                sle->setAccountID(sfDestination, owner.id());
                ac.view().update(sle);
                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseSubscription);

        testcase("Subscription claim moves NextClaimTime backwards");
        doInvariantCheck(
            makeEnv(features),
            {{"subscription claim moved NextClaimTime backwards"}},
            [&](Account const&, Account const&, ApplyContext& ac) {
                auto const sle = peekSubscription(ac);
                if (!sle)
                    return false;
                sle->setFieldU32(sfNextClaimTime, sle->getFieldU32(sfNextClaimTime) - 1);
                ac.view().update(sle);
                return true;
            },
            XRPAmount{},
            STTx{ttSUBSCRIPTION_CLAIM, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseSubscription);
    }

public:
    void
    run() override
    {
        testSubscriptionInvariants();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsSubscription, app, xrpl);

}  // namespace xrpl::test
