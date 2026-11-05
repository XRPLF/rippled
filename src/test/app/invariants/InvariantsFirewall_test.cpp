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
#include <xrpl/ledger/helpers/FirewallLimitHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
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
#include <string>
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

    // Whether the owner has a limit on the asset.
    static bool
    hasLimit(jtx::Env const& env, AccountID const& owner, Asset const& asset)
    {
        auto const location = firewallLimitField(owner, asset);
        if (!location)
            return false;
        auto const sle = env.le(location->entry);
        return sle && sle->isFieldPresent(*location->field);
    }

    // The entry holding the owner's limit on the asset, peeked in the view, and
    // the limit's field; the entry is null when the limit does not exist.
    static std::pair<SLE::pointer, SField const*>
    peekLimit(ApplyContext& ac, AccountID const& owner, Asset const& asset)
    {
        auto const location = firewallLimitField(owner, asset);
        if (!location)
            return {nullptr, nullptr};
        auto const sle = ac.view().peek(location->entry);
        if (!sle || !sle->isFieldPresent(*location->field))
            return {nullptr, location->field};
        return {sle, location->field};
    }

    // A firewall for the first account, with the second as counterparty and a
    // separate backup, and no limit.
    static bool
    createBareFirewall(jtx::Account const& a1, jtx::Account const& a2, jtx::Env& env)
    {
        using namespace jtx;
        Account const backup{"backup"};
        env.fund(XRP(1000), backup);
        env(firewall::set(a1.id(), a2.id(), backup.id()));
        return env.le(keylet::firewall(a1.id())) != nullptr;
    }

    // A firewall for the first account, with the second as counterparty and a
    // separate backup, and an XRP limit of 100 XRP a day.
    static bool
    createLimitedFirewall(jtx::Account const& a1, jtx::Account const& a2, jtx::Env& env)
    {
        using namespace jtx;
        if (!createBareFirewall(a1, a2, env))
            return false;
        env(firewall::limitSet(a1.id(), XRP(100), kDay));
        return hasLimit(env, a1.id(), xrpIssue());
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
    setSpent(ApplyContext& ac, AccountID const& owner, Asset const& asset, STAmount const& spent)
    {
        auto const [sle, field] = peekLimit(ac, owner, asset);
        if (!sle)
            return false;
        sle->peekFieldObject(*field).setFieldAmount(sfSpent, spent);
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
                auto const [limit, field] = peekLimit(ac, a1.id(), xrpIssue());
                if (!limit)
                    return false;
                limit->peekFieldObject(*field).setFieldAmount(sfLimitAmount, XRP(0));
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
                return hasLimit(env, a1.id(), usd.issue());
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
                return hasLimit(env, a1.id(), MPTIssue{*mptID});
            });
    }

    void
    testOutflowPerAsset()
    {
        testcase("firewall outflow counted per asset");
        using namespace jtx;

        Account const gw1{"gw1"};
        Account const gw2{"gw2"};
        auto const usd1 = gw1["USD"];
        auto const usd2 = gw2["USD"];
        auto const eur1 = gw1["EUR"];
        std::optional<MPTID> limitedMpt;
        std::optional<MPTID> otherMpt;

        // Moves amount of an issue from one holder's line to another's.
        auto const moveLine =
            [](ApplyContext& ac, Issue const& issue, AccountID const& holder, Number delta) {
                auto const line =
                    ac.view().peek(keylet::trustLine(holder, issue.account, issue.currency));
                STAmount const balance = line->getFieldAmount(sfBalance);
                bool const holderIsLow = line->getFieldAmount(sfLowLimit).getIssuer() == holder;
                line->setFieldAmount(
                    sfBalance,
                    STAmount{balance.asset(), Number(balance) + (holderIsLow ? delta : -delta)});
                ac.view().update(line);
            };
        auto const moveMpt =
            [](ApplyContext& ac, MPTID const& id, AccountID const& holder, std::int64_t delta) {
                auto const sle = ac.view().peek(keylet::mptoken(id, holder));
                sle->setFieldU64(
                    sfMPTAmount,
                    static_cast<std::uint64_t>(
                        static_cast<std::int64_t>(sle->getFieldU64(sfMPTAmount)) + delta));
                ac.view().update(sle);
            };

        // A1 holds USD of two issuers, EUR and two MPTs, with limits on USD of
        // gw1 and on one MPT; A2 holds the same.
        auto const setup = [&](Account const& a1, Account const& a2, Env& env) {
            Account const backup{"backup"};
            env.fund(XRP(1000), gw1, gw2, backup);
            for (auto const& holder : {a1, a2})
            {
                for (auto const& amount : {usd1(1000), usd2(1000), eur1(1000)})
                    env(trust(holder, amount, tfSetNoRipple));
            }
            env(pay(gw1, a1, usd1(100)));
            env(pay(gw2, a1, usd2(100)));
            env(pay(gw1, a1, eur1(100)));
            MPTTester limited(env, gw1, {.holders = {a1, a2}, .fund = false});
            limited.create({.flags = tfMPTCanTransfer});
            MPTTester other(env, gw1, {.holders = {a1, a2}, .fund = false});
            other.create({.flags = tfMPTCanTransfer});
            for (auto* mpt : {&limited, &other})
            {
                mpt->authorize({.account = a1});
                mpt->authorize({.account = a2});
                env(pay(gw1, a1, (*mpt)["MPT"](100)));
            }
            limitedMpt = limited.issuanceID();
            otherMpt = other.issuanceID();
            env(firewall::set(a1.id(), a2.id(), backup.id()));
            env(firewall::limitSet(a1.id(), usd1(15), kDay));
            env(firewall::limitSet(a1.id(), limited["MPT"](5), kDay));
            return hasLimit(env, a1.id(), usd1.issue()) &&
                hasLimit(env, a1.id(), MPTIssue{*limitedMpt});
        };

        // A1 sends 10 USD of gw1, 7 USD of gw2, 5 EUR and 3 and 2 of the MPTs
        // to A2, and writes usdSpent on its USD limit and 3 on its MPT limit.
        auto const outflow = [&](STAmount const& usdSpent) {
            return [&, usdSpent](Account const& a1, Account const& a2, ApplyContext& ac) {
                if (!limitedMpt || !otherMpt)
                    return false;
                for (auto const& [issue, amount] : {
                         std::pair{usd1.issue(), 10},
                         std::pair{usd2.issue(), 7},
                         std::pair{eur1.issue(), 5},
                     })
                {
                    moveLine(ac, issue, a1.id(), Number{-amount});
                    moveLine(ac, issue, a2.id(), Number{amount});
                }
                for (auto const& [id, amount] :
                     {std::pair{*limitedMpt, 3}, std::pair{*otherMpt, 2}})
                {
                    moveMpt(ac, id, a1.id(), -amount);
                    moveMpt(ac, id, a2.id(), amount);
                }
                return setSpent(ac, a1.id(), usd1.issue(), usdSpent) &&
                    setSpent(
                           ac,
                           a1.id(),
                           MPTIssue{*limitedMpt},
                           STAmount{MPTIssue{*limitedMpt}, std::uint64_t{3}});
            };
        };

        // Each limit is charged only its own asset: USD of another issuer,
        // another currency and another MPT are not counted.
        doInvariantCheck(
            {},
            outflow(usd1(10)),
            XRPAmount{},
            ownerTx(ttPAYMENT),
            {tesSUCCESS, tesSUCCESS},
            setup);
        doInvariantCheck(
            {"Invariant failed: a firewall limit's spent does not match its charge"},
            outflow(usd1(17)),
            XRPAmount{},
            ownerTx(ttPAYMENT),
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            setup);
    }

    void
    testLimitDeletedByCharge()
    {
        testcase("firewall limit deleted by a charge");
        using namespace jtx;

        doInvariantCheck(
            {
                "Invariant failed: a firewall limit was removed other than by its owner's "
                "FirewallLimitSet",
            },
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                moveXrp(ac, a1.id(), a2.id(), XRP(5));
                auto const [limit, field] = peekLimit(ac, a1.id(), xrpIssue());
                if (!limit)
                    return false;
                limit->makeFieldAbsent(*field);
                ac.view().update(limit);
                return true;
            },
            XRPAmount{},
            ownerTx(ttPAYMENT),
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            createLimitedFirewall);
    }

    void
    testLimitFieldRules()
    {
        testcase("firewall limit fields changed only by their owner");
        using namespace jtx;

        auto const limitSet = ownerTx(ttFIREWALL_LIMIT_SET);
        auto const limitDelete = ownerTx(
            ttFIREWALL_LIMIT_SET, [](STObject& tx) { tx.setFieldU32(sfFlags, tfLimitDelete); });

        // Sets an XRP limit on the owner's AccountRoot and counts it.
        auto const addLimit = [](ApplyContext& ac, AccountID const& owner) {
            auto const account = ac.view().peek(keylet::account(owner));
            STObject limit = STObject::makeInnerObject(sfFirewallLimit);
            limit.setFieldAmount(sfLimitAmount, XRP(10));
            limit.setFieldU32(sfPeriod, kDay);
            limit.setFieldAmount(sfSpent, XRP(0));
            limit.setFieldU32(sfStartDate, 0);
            account->setFieldObject(sfFirewallLimit, limit);
            ac.view().update(account);
            auto const firewall = ac.view().peek(keylet::firewall(owner));
            firewall->setFieldU32(sfLimitCount, (*firewall)[~sfLimitCount].valueOr(0) + 1);
            ac.view().update(firewall);
            return true;
        };

        // Removes the owner's XRP limit and its count.
        auto const removeLimit = [](ApplyContext& ac, AccountID const& owner) {
            auto const [sle, field] = peekLimit(ac, owner, xrpIssue());
            if (!sle)
                return false;
            sle->makeFieldAbsent(*field);
            ac.view().update(sle);
            auto const firewall = ac.view().peek(keylet::firewall(owner));
            firewall->makeFieldAbsent(sfLimitCount);
            ac.view().update(firewall);
            return true;
        };

        // A Payment by the owner removes its limit.
        doInvariantCheck(
            {
                "Invariant failed: a firewall limit was removed other than by its owner's "
                "FirewallLimitSet",
            },
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                return removeLimit(ac, a1.id());
            },
            XRPAmount{},
            ownerTx(ttPAYMENT),
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            createLimitedFirewall);

        // A Payment by the owner adds one.
        doInvariantCheck(
            {
                "Invariant failed: a firewall limit was added other than by its owner's "
                "FirewallLimitSet",
            },
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                return addLimit(ac, a1.id());
            },
            XRPAmount{},
            ownerTx(ttPAYMENT),
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            createBareFirewall);

        // A Payment by A2 changes A1's MPT limit.
        Account const gw{"gw"};
        std::optional<MPTID> mptID;
        doInvariantCheck(
            {
                "Invariant failed: a firewall limit changed other than by a successful "
                "transaction of its owner",
            },
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                return mptID &&
                    setSpent(ac, a1.id(), MPTIssue{*mptID}, STAmount{MPTIssue{*mptID}, 1});
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject& tx) { tx.setAccountID(sfAccount, Account{"A2"}.id()); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                env.fund(XRP(1000), gw);
                MPTTester mpt(env, gw, {.holders = {a1}, .fund = false});
                mpt.create();
                mpt.authorize({.account = a1});
                mptID = mpt.issuanceID();
                if (!createBareFirewall(a1, a2, env))
                    return false;
                env(firewall::limitSet(a1.id(), mpt["MPT"](10), kDay));
                return hasLimit(env, a1.id(), MPTIssue{*mptID});
            });

        // A FirewallLimitSet by A1 changes A2's side of a line.
        auto const usd = gw["USD"];
        doInvariantCheck(
            {
                "Invariant failed: a firewall limit changed other than by a successful "
                "transaction of its owner",
            },
            [&](Account const&, Account const& a2, ApplyContext& ac) {
                auto const [sle, field] = peekLimit(ac, a2.id(), usd.issue());
                if (!sle)
                    return false;
                sle->peekFieldObject(*field).setFieldAmount(sfLimitAmount, usd(5));
                ac.view().update(sle);
                return true;
            },
            XRPAmount{},
            limitSet,
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&](Account const& a1, Account const& a2, Env& env) {
                env.fund(XRP(1000), gw);
                env.close();
                env(trust(a2, usd(1000), tfSetNoRipple));
                env.close();
                env(firewall::set(a2.id(), a1.id(), gw.id()));
                env(firewall::limitSet(a2.id(), usd(10), kDay));
                return hasLimit(env, a2.id(), usd.issue());
            });

        // A failed transaction of the owner changes its limit.
        doInvariantCheck(
            {
                "Invariant failed: a firewall limit changed other than by a successful "
                "transaction of its owner",
            },
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

        // The owner's FirewallLimitSet adds and deletes a limit.
        doInvariantCheck(
            {},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                return addLimit(ac, a1.id());
            },
            XRPAmount{},
            limitSet,
            {tesSUCCESS, tesSUCCESS},
            createBareFirewall);
        doInvariantCheck(
            {},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                return removeLimit(ac, a1.id());
            },
            XRPAmount{},
            limitDelete,
            {tesSUCCESS, tesSUCCESS},
            createLimitedFirewall);
    }

    void
    testLimitSetRules()
    {
        testcase("FirewallLimitSet rules");
        using namespace jtx;

        auto const tx = ownerTx(ttFIREWALL_LIMIT_SET);
        auto const deleteTx = ownerTx(
            ttFIREWALL_LIMIT_SET, [](STObject& tx) { tx.setFieldU32(sfFlags, tfLimitDelete); });

        // Sets an XRP limit with the given Spent on the owner's AccountRoot, and
        // adds the given amount to the firewall's LimitCount.
        auto const create = [](ApplyContext& ac,
                               AccountID const& owner,
                               STAmount const& spent,
                               std::uint32_t limitCountDelta) {
            auto const account = ac.view().peek(keylet::account(owner));
            STObject limit = STObject::makeInnerObject(sfFirewallLimit);
            limit.setFieldAmount(sfLimitAmount, XRP(10));
            limit.setFieldU32(sfPeriod, kDay);
            limit.setFieldAmount(sfSpent, spent);
            limit.setFieldU32(sfStartDate, 0);
            account->setFieldObject(sfFirewallLimit, limit);
            ac.view().update(account);
            auto const firewall = ac.view().peek(keylet::firewall(owner));
            firewall->setFieldU32(
                sfLimitCount, (*firewall)[~sfLimitCount].valueOr(0) + limitCountDelta);
            ac.view().update(firewall);
            return true;
        };

        // A created limit with nothing spent and a count that follows it.
        doInvariantCheck(
            {},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                return create(ac, a1.id(), XRP(0), 1);
            },
            XRPAmount{},
            tx,
            {tesSUCCESS, tesSUCCESS},
            createBareFirewall);

        doInvariantCheck(
            {"Invariant failed: a created firewall limit has spent"},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                return create(ac, a1.id(), XRP(1), 1);
            },
            XRPAmount{},
            tx,
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            createBareFirewall);

        for (auto const limitDelta : {0u, 2u})
        {
            doInvariantCheck(
                {"Invariant failed: a firewall's limit count does not match its limits"},
                [&](Account const& a1, Account const&, ApplyContext& ac) {
                    return create(ac, a1.id(), XRP(0), limitDelta);
                },
                XRPAmount{},
                tx,
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                createBareFirewall);
        }

        // An update that lowers or raises the amount keeps what was spent.
        for (auto const& amount : {XRP(50), XRP(500)})
        {
            doInvariantCheck(
                {},
                [amount](Account const& a1, Account const&, ApplyContext& ac) {
                    auto const [sle, field] = peekLimit(ac, a1.id(), xrpIssue());
                    if (!sle)
                        return false;
                    sle->peekFieldObject(*field).setFieldAmount(sfLimitAmount, amount);
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

        // Moving the period's start alone changes what was spent too.
        doInvariantCheck(
            {"Invariant failed: a firewall limit update changed what was spent"},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                auto const [sle, field] = peekLimit(ac, a1.id(), xrpIssue());
                if (!sle)
                    return false;
                STObject& limit = sle->peekFieldObject(*field);
                limit.setFieldU32(sfStartDate, limit.getFieldU32(sfStartDate) + 1);
                ac.view().update(sle);
                return true;
            },
            XRPAmount{},
            tx,
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            createLimitedFirewall);

        // A firewall that appears with a limit count but no limit; the reset
        // removes it, so the second pass does not escalate.
        doInvariantCheck(
            {"Invariant failed: a firewall's limit count does not match its limits"},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sle = std::make_shared<SLE>(keylet::firewall(a1.id()));
                sle->setAccountID(sfOwner, a1.id());
                sle->setAccountID(sfCounterparty, a2.id());
                sle->setAccountID(sfBackup, Account{"backup"}.id());
                sle->setFieldU32(sfLimitCount, 1);
                ac.view().insert(sle);
                return true;
            },
            XRPAmount{},
            tx,
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        // A delete removes the field and its count.
        auto const erase = [](ApplyContext& ac, AccountID const& owner, std::uint32_t count) {
            auto const [sle, field] = peekLimit(ac, owner, xrpIssue());
            if (!sle)
                return false;
            sle->makeFieldAbsent(*field);
            ac.view().update(sle);
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
            deleteTx,
            {tesSUCCESS, tesSUCCESS},
            createLimitedFirewall);
        doInvariantCheck(
            {"Invariant failed: a firewall's limit count does not match its limits"},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                return erase(ac, a1.id(), 1);
            },
            XRPAmount{},
            deleteTx,
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

    void
    testHoldingDeleteGuards()
    {
        testcase("holding with a firewall limit not deleted");
        using namespace jtx;

        // An issuer whose account is above A1's, so A1 is the line's low side.
        std::optional<Account> gw;
        for (int i = 0; i < 32 && !gw; ++i)
        {
            Account const candidate{"issuer" + std::to_string(i)};
            if (candidate.id() > Account{"A1"}.id())
                gw = candidate;
        }
        if (!gw)
        {
            fail("no issuer above A1");
            return;
        }
        auto const usd = (*gw)["USD"];

        // A1 holds 100 USD with no trust limit left, and a USD limit; both
        // sides ripple, as both accounts do, so a side is default once empty.
        auto const limitedLine = [&](Account const& a1, Account const& a2, Env& env) {
            env.fund(XRP(1000), *gw);
            env.close();
            env(trust(a1, usd(1000)));
            env.close();
            env(pay(*gw, a1, usd(100)));
            env.close();
            env(trust(a1, usd(0)));
            env.close();
            if (!createBareFirewall(a1, a2, env))
                return false;
            env(firewall::limitSet(a1.id(), usd(1000), kDay));
            return hasLimit(env, a1.id(), usd.issue());
        };

        // trustDelete refuses a line with a limit.
        doInvariantCheck(
            {},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                auto const line = ac.view().peek(keylet::trustLine(a1.id(), usd.issue()));
                if (!line || !line->isFieldPresent(sfLowFirewallLimit))
                    return false;
                return trustDelete(ac.view(), line, a1.id(), gw->id(), ac.journal) ==
                    tecHAS_OBLIGATIONS;
            },
            XRPAmount{},
            STTx{ttACCOUNT_SET, [](STObject&) {}},
            {tesSUCCESS, tesSUCCESS},
            limitedLine);

        // Sending the whole balance to the issuer keeps A1's side reserved.
        doInvariantCheck(
            {},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                if (!isTesSuccess(accountSend(ac.view(), a1.id(), gw->id(), usd(100), ac.journal)))
                    return false;
                auto const line = ac.view().read(keylet::trustLine(a1.id(), usd.issue()));
                return line && line->isFlag(lsfLowReserve);
            },
            XRPAmount{},
            STTx{ttACCOUNT_SET, [](STObject&) {}},
            {tesSUCCESS, tesSUCCESS},
            limitedLine);

        // removeEmptyHolding refuses an emptied line with a limit before it
        // changes an owner count or a reserve flag.
        {
            std::uint32_t ownersA1 = 0;
            std::uint32_t ownersGw = 0;
            doInvariantCheck(
                {},
                [&](Account const& a1, Account const&, ApplyContext& ac) {
                    if (!isTesSuccess(
                            accountSend(ac.view(), a1.id(), gw->id(), usd(100), ac.journal)))
                        return false;
                    auto const before = ac.view().read(keylet::trustLine(a1.id(), usd.issue()));
                    auto const a1Before = ac.view().read(keylet::account(a1.id()));
                    auto const gwBefore = ac.view().read(keylet::account(gw->id()));
                    if (!before || !a1Before || !gwBefore ||
                        !before->isFieldPresent(sfLowFirewallLimit))
                        return false;
                    ownersA1 = a1Before->at(sfOwnerCount);
                    ownersGw = gwBefore->at(sfOwnerCount);
                    bool const lowReserve = before->isFlag(lsfLowReserve);
                    bool const highReserve = before->isFlag(lsfHighReserve);
                    if (removeEmptyHolding(
                            ac.getApplyViewContext(), a1.id(), usd.issue(), ac.journal) !=
                        tecHAS_OBLIGATIONS)
                        return false;
                    auto const line = ac.view().read(keylet::trustLine(a1.id(), usd.issue()));
                    auto const a1After = ac.view().read(keylet::account(a1.id()));
                    auto const gwAfter = ac.view().read(keylet::account(gw->id()));
                    return line && a1After && gwAfter &&
                        line->isFlag(lsfLowReserve) == lowReserve &&
                        line->isFlag(lsfHighReserve) == highReserve &&
                        a1After->at(sfOwnerCount) == ownersA1 &&
                        gwAfter->at(sfOwnerCount) == ownersGw;
                },
                XRPAmount{},
                STTx{ttACCOUNT_SET, [](STObject&) {}},
                {tesSUCCESS, tesSUCCESS},
                limitedLine);
        }

        // removeEmptyHolding refuses an MPToken with a limit.
        std::optional<MPTID> mptID;
        doInvariantCheck(
            {},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                if (!mptID)
                    return false;
                return removeEmptyHolding(
                           ac.getApplyViewContext(), a1.id(), MPTIssue{*mptID}, ac.journal) ==
                    tecHAS_OBLIGATIONS;
            },
            XRPAmount{},
            STTx{ttACCOUNT_SET, [](STObject&) {}},
            {tesSUCCESS, tesSUCCESS},
            [&](Account const& a1, Account const& a2, Env& env) {
                env.fund(XRP(1000), *gw);
                MPTTester mpt(env, *gw, {.holders = {a1}, .fund = false});
                mpt.create();
                mpt.authorize({.account = a1});
                mptID = mpt.issuanceID();
                if (!createBareFirewall(a1, a2, env))
                    return false;
                env(firewall::limitSet(a1.id(), mpt["MPT"](10), kDay));
                return hasLimit(env, a1.id(), MPTIssue{*mptID});
            });
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
        testOutflowPerAsset();
        testLimitDeletedByCharge();
        testLimitFieldRules();
        testLimitSetRules();
        testOutflowPrincipal();
        testHoldingDeleteGuards();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsFirewall, app, xrpl);

}  // namespace xrpl::test
