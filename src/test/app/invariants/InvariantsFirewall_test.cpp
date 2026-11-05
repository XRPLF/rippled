#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/trust.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <source_location>
#include <utility>

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

    void
    testMissingField(SField const& field)
    {
        testcase("firewall missing " + field.getName());
        using namespace jtx;

        doInvariantCheck(
            {"Invariant failed: a firewall is missing its owner or its counterparty"},
            [&field](Account const& a1, Account const&, ApplyContext& ac) {
                auto const sle = ac.view().peek(keylet::firewall(a1.id()));
                if (!sle)
                    return false;
                sle->makeFieldAbsent(field);
                ac.view().update(sle);
                return true;
            },
            XRPAmount{},
            STTx{ttFIREWALL_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            createFirewall);
    }

    static constexpr std::uint32_t kDay = 86'400;

    // A firewall for the first account, with the second as counterparty and a
    // separate backup, and an account-wide XRP limit of 100 XRP a day.
    static bool
    createLimitedFirewall(jtx::Account const& a1, jtx::Account const& a2, jtx::Env& env)
    {
        using namespace jtx;
        Account const backup{"backup"};
        env.fund(XRP(1000), backup);
        env(firewall::set(a1.id(), a2.id(), backup.id()));
        env(firewall::limitSet(a1.id(), XRP(100), kDay));
        return env.le(keylet::firewallLimit(a1.id(), xrpIssue())) != nullptr;
    }

    // Moves XRP between two accounts' balances in the view.
    static void
    moveXrp(ApplyContext& ac, AccountID const& from, AccountID const& to, STAmount const& amount)
    {
        auto const sleFrom = ac.view().peek(keylet::account(from));
        auto const sleTo = ac.view().peek(keylet::account(to));
        sleFrom->setFieldAmount(sfBalance, sleFrom->getFieldAmount(sfBalance) - amount);
        sleTo->setFieldAmount(sfBalance, sleTo->getFieldAmount(sfBalance) + amount);
        ac.view().update(sleFrom);
        ac.view().update(sleTo);
    }

    // Writes a limit's Spent in the view.
    static bool
    setSpent(
        ApplyContext& ac,
        AccountID const& owner,
        Asset const& asset,
        STAmount const& spent,
        std::optional<AccountID> const& signer = std::nullopt)
    {
        auto const sle = ac.view().peek(keylet::firewallLimit(owner, asset, signer));
        if (!sle)
            return false;
        sle->setFieldAmount(sfSpent, spent);
        ac.view().update(sle);
        return true;
    }

    // A transaction of the given type sent by A1.
    static STTx
    ownerTx(TxType type, std::function<void(STObject&)> const& fields = {})
    {
        return STTx{type, [&fields](STObject& tx) {
                        tx.setAccountID(sfAccount, jtx::Account{"A1"}.id());
                        if (fields)
                            fields(tx);
                    }};
    }

    void
    testOutflowOverLimit()
    {
        testcase("firewall outflow over its limit");
        using namespace jtx;

        // The charge plus what was spent exceeds the limit.
        doInvariantCheck(
            {"Invariant failed: an outflow exceeded a firewall limit"},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                moveXrp(ac, a1.id(), a2.id(), XRP(150));
                return setSpent(ac, a1.id(), xrpIssue(), XRP(150));
            },
            XRPAmount{},
            ownerTx(ttPAYMENT),
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            createLimitedFirewall);

        // The written Spent is not the period's Spent plus the charge.
        doInvariantCheck(
            {"Invariant failed: a firewall limit's spent does not match its charge"},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                moveXrp(ac, a1.id(), a2.id(), XRP(50));
                return setSpent(ac, a1.id(), xrpIssue(), XRP(40));
            },
            XRPAmount{},
            ownerTx(ttPAYMENT),
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            createLimitedFirewall);

        // A charge left unwritten fails the same way. Outflow is checked only
        // on success, so a second pass fed a tec result does not escalate.
        doInvariantCheck(
            {"Invariant failed: a firewall limit's spent does not match its charge"},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                moveXrp(ac, a1.id(), a2.id(), XRP(50));
                return true;
            },
            XRPAmount{},
            ownerTx(ttPAYMENT),
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createLimitedFirewall);

        // A charge written as it should be passes.
        doInvariantCheck(
            {},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                moveXrp(ac, a1.id(), a2.id(), XRP(50));
                return setSpent(ac, a1.id(), xrpIssue(), XRP(50));
            },
            XRPAmount{},
            ownerTx(ttPAYMENT),
            {tesSUCCESS, tesSUCCESS},
            createLimitedFirewall);
    }

    void
    testOutflowFeeExcluded()
    {
        testcase("firewall outflow excludes the owner's fee");
        using namespace jtx;

        // A full stop on XRP: only the fee leaves, so nothing is charged.
        doInvariantCheck(
            {},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                sle->setFieldAmount(
                    sfBalance, sle->getFieldAmount(sfBalance) - STAmount{drops(10)});
                ac.view().update(sle);
                auto const limit = ac.view().peek(keylet::firewallLimit(a1.id(), xrpIssue()));
                if (!limit)
                    return false;
                limit->setFieldAmount(sfLimitAmount, XRP(0));
                ac.view().update(limit);
                return true;
            },
            XRPAmount{10},
            ownerTx(
                ttACCOUNT_SET,
                [](STObject& tx) { tx.setFieldAmount(sfFee, STAmount{XRPAmount{10}}); }),
            {tesSUCCESS, tesSUCCESS},
            createLimitedFirewall);
    }

    void
    testUnlistedOutflow()
    {
        testcase("firewall outflow of an unlisted asset");
        using namespace jtx;

        // An Allow-type outflow with no limit while unlisted assets are refused.
        auto const refuseUnlisted = [](Account const& a1, Account const& a2, Env& env) {
            Account const backup{"backup"};
            env.fund(XRP(1000), backup);
            auto jv = firewall::set(a1.id(), a2.id(), backup.id());
            jv[sfUnlistedAssets.jsonName] = 1;
            env(jv);
            return env.le(keylet::firewall(a1.id())) != nullptr;
        };
        doInvariantCheck(
            {
                "Invariant failed: an asset with no firewall limit left an account that refuses "
                "unlisted assets",
            },
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                moveXrp(ac, a1.id(), a2.id(), XRP(5));
                return true;
            },
            XRPAmount{},
            ownerTx(ttACCOUNT_SET),
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            refuseUnlisted);

        // Allowed by default.
        doInvariantCheck(
            {},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                moveXrp(ac, a1.id(), a2.id(), XRP(5));
                return true;
            },
            XRPAmount{},
            ownerTx(ttACCOUNT_SET),
            {tesSUCCESS, tesSUCCESS},
            [](Account const& a1, Account const& a2, Env& env) {
                Account const backup{"backup"};
                env.fund(XRP(1000), backup);
                env(firewall::set(a1.id(), a2.id(), backup.id()));
                return env.le(keylet::firewall(a1.id())) != nullptr;
            });
    }

    void
    testTokenOutflow()
    {
        testcase("firewall outflow of IOU and MPT");
        using namespace jtx;

        Account const gw{"gw"};
        auto const usd = gw["USD"];

        // Moves a holder's balance on its line with gw by delta.
        auto const adjustLine = [usd](ApplyContext& ac, AccountID const& holder, Number delta) {
            auto const line = ac.view().peek(
                keylet::trustLine(holder, usd.issue().account, usd.issue().currency));
            STAmount const balance = line->getFieldAmount(sfBalance);
            bool const holderIsLow = line->getFieldAmount(sfLowLimit).getIssuer() == holder;
            line->setFieldAmount(
                sfBalance,
                STAmount{balance.asset(), Number(balance) + (holderIsLow ? delta : -delta)});
            ac.view().update(line);
        };
        doInvariantCheck(
            {"Invariant failed: an outflow exceeded a firewall limit"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                adjustLine(ac, a1.id(), Number{-20});
                adjustLine(ac, a2.id(), Number{20});
                return setSpent(ac, a1.id(), usd.issue(), usd(20));
            },
            XRPAmount{},
            ownerTx(ttPAYMENT),
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Account const backup{"backup"};
                env.fund(XRP(1000), gw, backup);
                env(trust(a1, usd(1000), tfSetNoRipple));
                env(trust(a2, usd(1000), tfSetNoRipple));
                env(pay(gw, a1, usd(100)));
                env(firewall::set(a1.id(), a2.id(), backup.id()));
                env(firewall::limitSet(a1.id(), usd(10), kDay));
                return env.le(keylet::firewallLimit(a1.id(), usd.issue())) != nullptr;
            });

        std::optional<MPTID> mptID;
        doInvariantCheck(
            {"Invariant failed: an outflow exceeded a firewall limit"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                if (!mptID)
                    return false;
                auto const move = [&](AccountID const& holder, std::int64_t delta) {
                    auto const sle = ac.view().peek(keylet::mptoken(*mptID, holder));
                    sle->setFieldU64(
                        sfMPTAmount,
                        static_cast<std::uint64_t>(
                            static_cast<std::int64_t>(sle->getFieldU64(sfMPTAmount)) + delta));
                    ac.view().update(sle);
                };
                move(a1.id(), -20);
                move(a2.id(), 20);
                return setSpent(ac, a1.id(), MPTIssue{*mptID}, STAmount{MPTIssue{*mptID}, 20});
            },
            XRPAmount{},
            ownerTx(ttPAYMENT),
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                Account const backup{"backup"};
                env.fund(XRP(1000), gw, backup);
                MPTTester mpt(env, gw, {.holders = {a1, a2}, .fund = false});
                mpt.create({.flags = tfMPTCanTransfer});
                mpt.authorize({.account = a1});
                mpt.authorize({.account = a2});
                env(pay(gw, a1, mpt["MPT"](100)));
                mptID = mpt.issuanceID();
                env(firewall::set(a1.id(), a2.id(), backup.id()));
                env(firewall::limitSet(a1.id(), mpt["MPT"](10), kDay));
                return env.le(keylet::firewallLimit(a1.id(), MPTIssue{*mptID})) != nullptr;
            });
    }

    void
    testFailedChangesLimit()
    {
        testcase("firewall limit changed by a failed transaction");
        using namespace jtx;

        doInvariantCheck(
            {"Invariant failed: a firewall limit changed on a failed transaction"},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                return setSpent(ac, a1.id(), xrpIssue(), XRP(1));
            },
            XRPAmount{},
            ownerTx(ttPAYMENT),
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            createLimitedFirewall,
            TxAccount::None,
            std::source_location::current(),
            tecUNFUNDED_PAYMENT);
    }

    void
    testLimitSetRules()
    {
        testcase("FirewallLimitSet rules");
        using namespace jtx;

        auto const tx = ownerTx(ttFIREWALL_LIMIT_SET);

        // Inserts a limit scoped to A2 with the given Spent, and adds the
        // given amounts to the firewall's counts.
        auto const create = [](ApplyContext& ac,
                               AccountID const& owner,
                               AccountID const& signer,
                               STAmount const& spent,
                               std::uint32_t limitCountDelta,
                               std::uint32_t signerCountDelta) {
            auto const sle =
                std::make_shared<SLE>(keylet::firewallLimit(owner, xrpIssue(), signer));
            sle->setAccountID(sfOwner, owner);
            sle->setFieldIssue(sfAsset, STIssue{sfAsset, xrpIssue()});
            sle->setAccountID(sfLimitSigner, signer);
            sle->setFieldAmount(sfLimitAmount, XRP(10));
            sle->setFieldU32(sfPeriod, kDay);
            sle->setFieldAmount(sfSpent, spent);
            sle->setFieldU32(sfStartDate, 0);
            sle->setFieldU64(sfOwnerNode, 0);
            ac.view().insert(sle);
            auto const firewall = ac.view().peek(keylet::firewall(owner));
            firewall->setFieldU32(
                sfLimitCount, firewall->getFieldU32(sfLimitCount) + limitCountDelta);
            firewall->setFieldU32(
                sfSignerLimitCount,
                (firewall->isFieldPresent(sfSignerLimitCount)
                     ? firewall->getFieldU32(sfSignerLimitCount)
                     : 0) +
                    signerCountDelta);
            ac.view().update(firewall);
            return true;
        };

        // A created limit with nothing spent and counts that follow it.
        doInvariantCheck(
            {},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                return create(ac, a1.id(), a2.id(), XRP(0), 1, 1);
            },
            XRPAmount{},
            tx,
            {tesSUCCESS, tesSUCCESS},
            createLimitedFirewall);

        doInvariantCheck(
            {"Invariant failed: a created firewall limit has spent"},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                return create(ac, a1.id(), a2.id(), XRP(1), 1, 1);
            },
            XRPAmount{},
            tx,
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            createLimitedFirewall);

        for (auto const [limitDelta, signerDelta] :
             {std::pair{0u, 1u}, std::pair{1u, 0u}, std::pair{2u, 1u}})
        {
            doInvariantCheck(
                {"Invariant failed: a firewall's limit counts do not match its limits"},
                [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                    return create(ac, a1.id(), a2.id(), XRP(0), limitDelta, signerDelta);
                },
                XRPAmount{},
                tx,
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                createLimitedFirewall);
        }

        // An update that lowers or raises the amount keeps what was spent.
        for (auto const& amount : {XRP(50), XRP(500)})
        {
            doInvariantCheck(
                {},
                [amount](Account const& a1, Account const&, ApplyContext& ac) {
                    auto const sle = ac.view().peek(keylet::firewallLimit(a1.id(), xrpIssue()));
                    if (!sle)
                        return false;
                    sle->setFieldAmount(sfLimitAmount, amount);
                    ac.view().update(sle);
                    return true;
                },
                XRPAmount{},
                tx,
                {tesSUCCESS, tesSUCCESS},
                createLimitedFirewall);
        }

        doInvariantCheck(
            {"Invariant failed: a firewall limit update changed what was spent"},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                return setSpent(ac, a1.id(), xrpIssue(), XRP(3));
            },
            XRPAmount{},
            tx,
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            createLimitedFirewall);

        // A delete removes the entry and its count.
        auto const erase = [](ApplyContext& ac, AccountID const& owner, std::uint32_t count) {
            auto const sle = ac.view().peek(keylet::firewallLimit(owner, xrpIssue()));
            if (!sle)
                return false;
            ac.view().erase(sle);
            auto const firewall = ac.view().peek(keylet::firewall(owner));
            if (count == 0)
            {
                firewall->makeFieldAbsent(sfLimitCount);
            }
            else
            {
                firewall->setFieldU32(sfLimitCount, count);
            }
            ac.view().update(firewall);
            return true;
        };
        doInvariantCheck(
            {},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                return erase(ac, a1.id(), 0);
            },
            XRPAmount{},
            tx,
            {tesSUCCESS, tesSUCCESS},
            createLimitedFirewall);
        doInvariantCheck(
            {"Invariant failed: a firewall's limit counts do not match its limits"},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                return erase(ac, a1.id(), 1);
            },
            XRPAmount{},
            tx,
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            createLimitedFirewall);
    }

    void
    testOutflowPrincipal()
    {
        testcase("firewall outflow principal");
        using namespace jtx;

        auto const overLimit = [](Account const& a1, Account const& a2, ApplyContext& ac) {
            moveXrp(ac, a1.id(), a2.id(), XRP(150));
            return true;
        };

        // A transaction someone else sends is not the owner's outflow.
        for (TxType const type :
             {ttCHECK_CASH, ttNFTOKEN_ACCEPT_OFFER, ttOFFER_CREATE, ttCLAWBACK, ttESCROW_FINISH})
        {
            doInvariantCheck(
                {},
                overLimit,
                XRPAmount{},
                STTx{type, [](STObject& tx) { tx.setAccountID(sfAccount, Account{"A2"}.id()); }},
                {tesSUCCESS, tesSUCCESS},
                createLimitedFirewall);
        }

        // The owner acting through a delegate, or as a Batch inner transaction,
        // is checked.
        doInvariantCheck(
            {"Invariant failed: an outflow exceeded a firewall limit"},
            overLimit,
            XRPAmount{},
            ownerTx(
                ttPAYMENT, [](STObject& tx) { tx.setAccountID(sfDelegate, Account{"A2"}.id()); }),
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createLimitedFirewall);
        doInvariantCheck(
            {"Invariant failed: an outflow exceeded a firewall limit"},
            overLimit,
            XRPAmount{},
            ownerTx(ttPAYMENT, [](STObject& tx) { tx.setFieldU32(sfFlags, tfInnerBatchTxn); }),
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createLimitedFirewall);
    }

public:
    void
    run() override
    {
        testCounterpartyIsOwner();
        testOwnerChanged();
        testMissingField(sfOwner);
        testMissingField(sfCounterparty);
        testOutflowOverLimit();
        testOutflowFeeExcluded();
        testUnlistedOutflow();
        testTokenOutflow();
        testFailedChangesLimit();
        testLimitSetRules();
        testOutflowPrincipal();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsFirewall, app, xrpl);

}  // namespace xrpl::test
