#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/batch.h>
#include <test/jtx/delegate.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/multisign.h>
#include <test/jtx/noop.h>
#include <test/jtx/regkey.h>
#include <test/jtx/sig.h>
#include <test/jtx/ter.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace xrpl::test {

class FirewallLimits_test : public beast::unit_test::Suite
{
    // A counterparty signature adds one base fee to the minimum.
    static jtx::Fee
    signedFee(jtx::Env& env)
    {
        return jtx::Fee{STAmount{env.current()->fees().base * 2}};
    }

    static UInt256
    firewallID(jtx::Account const& account)
    {
        return keylet::firewall(account.id()).key;
    }

    // The firewall's UnlistedAssets, 0 when absent.
    static std::uint8_t
    unlistedAssets(jtx::Env const& env, jtx::Account const& account)
    {
        auto const sle = env.le(keylet::firewall(account.id()));
        return sle ? (*sle)[~sfUnlistedAssets].value_or(0) : 0;
    }

    void
    testFirewallSetUnlistedAssets(FeatureBitset features)
    {
        testcase("FirewallSet UnlistedAssets");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const dave{"dave"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, bob, carol, dave, backup);
        env.close();

        // Only 0 and 1 are values.
        {
            auto jv = firewall::set(alice.id(), carol.id(), backup.id());
            jv[sfUnlistedAssets.jsonName] = 2;
            env(jv, Ter(temMALFORMED));
        }

        // A create may set it.
        {
            auto jv = firewall::set(bob.id(), carol.id(), backup.id());
            jv[sfUnlistedAssets.jsonName] = 1;
            env(jv);
        }
        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();
        BEAST_EXPECT(unlistedAssets(env, bob) == 1);
        BEAST_EXPECT(unlistedAssets(env, alice) == 0);
        auto const fwID = firewallID(alice);

        auto unlisted = [&](std::uint8_t value) {
            auto jv = firewall::set(alice.id(), fwID);
            jv[sfUnlistedAssets.jsonName] = value;
            return jv;
        };

        // Setting it to 1 alone tightens the firewall: the owner needs no
        // counterparty, and pays no counterparty fee.
        env(unlisted(1));
        env.close();
        BEAST_EXPECT(unlistedAssets(env, alice) == 1);

        // A counterparty signature that is present is still verified.
        env(unlisted(1), Sig(sfCounterpartySignature, dave), signedFee(env), Ter(tefBAD_AUTH));

        // Setting it back to 0 loosens it.
        env(unlisted(0), Ter(temBAD_SIGNER));
        env.close();
        BEAST_EXPECT(unlistedAssets(env, alice) == 1);
        env(unlisted(0), Sig(sfCounterpartySignature, carol), signedFee(env));
        env.close();
        BEAST_EXPECT(unlistedAssets(env, alice) == 0);
        auto const sle = env.le(keylet::firewall(alice.id()));
        BEAST_EXPECT(sle && !sle->isFieldPresent(sfUnlistedAssets));

        // With any other field the update needs the counterparty.
        {
            auto jv = unlisted(1);
            jv[sfMaxFee.jsonName] = to_string(XRP(1).value());
            env(jv, Ter(temBAD_SIGNER));
        }
        {
            auto jv = firewall::set(alice.id(), fwID);
            jv[sfMaxFee.jsonName] = to_string(XRP(1).value());
            env(jv, Ter(temBAD_SIGNER));
        }
        env(firewall::set(alice.id(), fwID), firewall::kCounterparty(dave), Ter(temBAD_SIGNER));
        env.close();
        auto const after = env.le(keylet::firewall(alice.id()));
        BEAST_EXPECT(after && !after->isFieldPresent(sfMaxFee));
        BEAST_EXPECT(after && after->getAccountID(sfCounterparty) == carol.id());
        BEAST_EXPECT(unlistedAssets(env, alice) == 0);
    }

    static constexpr std::uint32_t kDay = 86'400;

    static std::uint32_t
    closeTime(jtx::Env& env)
    {
        return env.current()->parentCloseTime().time_since_epoch().count();
    }

    static std::shared_ptr<SLE const>
    limitEntry(
        jtx::Env const& env,
        jtx::Account const& owner,
        Asset const& asset,
        std::optional<AccountID> const& signer = std::nullopt)
    {
        return env.le(keylet::firewallLimit(owner.id(), asset, signer));
    }

    // The firewall's LimitCount and SignerLimitCount, 0 when absent.
    static std::pair<std::uint32_t, std::uint32_t>
    limitCounts(jtx::Env const& env, jtx::Account const& account)
    {
        auto const sle = env.le(keylet::firewall(account.id()));
        if (!sle)
            return {0, 0};
        return {
            (*sle)[~sfLimitCount].value_or(0),
            (*sle)[~sfSignerLimitCount].value_or(0),
        };
    }

    void
    testLimitSetPreflight(FeatureBitset features)
    {
        testcase("FirewallLimitSet preflight");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const gw{"gw"};
        env.fund(XRP(10000), alice, carol, backup, gw);
        env.close();
        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();
        auto const usd = gw["USD"];

        // Only tfLimitDelete is a flag of its own.
        env(firewall::limitSet(alice.id(), XRP(100), kDay),
            Txflags(0x00000002),
            Ter(temINVALID_FLAG));

        // A delete names no amount or period, and a set names both.
        {
            auto jv = firewall::limitDelete(alice.id(), xrpIssue());
            jv[sfLimitAmount.jsonName] = "100";
            env(jv, Ter(temMALFORMED));
        }
        {
            auto jv = firewall::limitDelete(alice.id(), xrpIssue());
            jv[sfPeriod.jsonName] = kDay;
            env(jv, Ter(temMALFORMED));
        }
        {
            auto jv = firewall::limitSet(alice.id(), XRP(100), kDay);
            jv.removeMember(sfPeriod.jsonName);
            env(jv, Ter(temMALFORMED));
        }
        {
            auto jv = firewall::limitSet(alice.id(), XRP(100), kDay);
            jv.removeMember(sfLimitAmount.jsonName);
            env(jv, Ter(temMALFORMED));
        }

        // The amount is a non-negative amount of the asset.
        {
            auto jv = firewall::limitSet(alice.id(), XRP(100), kDay);
            jv[sfAsset.jsonName] = toJson(usd.issue());
            env(jv, Ter(temBAD_AMOUNT));
        }
        env(firewall::limitSet(alice.id(), usd(-1), kDay), Ter(temBAD_AMOUNT));

        // The period is one minute to one year.
        env(firewall::limitSet(alice.id(), XRP(100), 59), Ter(temMALFORMED));
        env(firewall::limitSet(alice.id(), XRP(100), 31'536'001), Ter(temMALFORMED));
        env.close();
        BEAST_EXPECT(!limitEntry(env, alice, xrpIssue()));

        env(firewall::limitSet(alice.id(), XRP(100), 60));
        env(firewall::limitSet(alice.id(), usd(100), 31'536'000));
        env.close();
        BEAST_EXPECT(limitEntry(env, alice, xrpIssue()));
        BEAST_EXPECT(limitEntry(env, alice, usd.issue()));
    }

    void
    testLimitSetCreate(FeatureBitset features)
    {
        testcase("FirewallLimitSet create");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const dave{"dave"};
        Account const backup{"backup"};
        Account const gw{"gw"};
        env.fund(XRP(10000), alice, bob, carol, dave, backup, gw);
        env.close();
        auto const usd = gw["USD"];

        // A limit needs a firewall.
        env(firewall::limitSet(bob.id(), XRP(100), kDay), Ter(tecNO_TARGET));
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();
        auto const owners = ownerCount(env, alice);

        // With unlisted assets allowed a new limit tightens the firewall, so the
        // owner alone creates it.
        auto const createdAt = closeTime(env);
        env(firewall::limitSet(alice.id(), XRP(100), kDay));
        env.close();
        auto const sle = limitEntry(env, alice, xrpIssue());
        if (!BEAST_EXPECT(sle))
            return;
        BEAST_EXPECT(sle->getAccountID(sfOwner) == alice.id());
        BEAST_EXPECT((*sle)[sfAsset] == Asset{xrpIssue()});
        BEAST_EXPECT(!sle->isFieldPresent(sfLimitSigner));
        BEAST_EXPECT(sle->getFieldAmount(sfLimitAmount) == XRP(100));
        BEAST_EXPECT(sle->getFieldU32(sfPeriod) == kDay);
        BEAST_EXPECT(sle->getFieldAmount(sfSpent) == XRP(0));
        BEAST_EXPECT(sle->getFieldU32(sfStartDate) == createdAt);
        BEAST_EXPECT(ownerCount(env, alice) == owners + 1);
        BEAST_EXPECT(limitCounts(env, alice) == std::make_pair(1u, 0u));

        // A limit scoped to one signer, and a zero limit that stops the asset.
        env(firewall::limitSet(alice.id(), XRP(10), kDay), firewall::kLimitSigner(dave));
        env(firewall::limitSet(alice.id(), usd(0), kDay));
        env.close();
        auto const signerSle = limitEntry(env, alice, xrpIssue(), dave.id());
        BEAST_EXPECT(signerSle && signerSle->getAccountID(sfLimitSigner) == dave.id());
        BEAST_EXPECT(signerSle && signerSle->getFieldAmount(sfLimitAmount) == XRP(10));
        auto const usdSle = limitEntry(env, alice, usd.issue());
        BEAST_EXPECT(usdSle && usdSle->getFieldAmount(sfLimitAmount) == usd(0));
        BEAST_EXPECT(usdSle && usdSle->getFieldAmount(sfSpent) == usd(0));
        BEAST_EXPECT(ownerCount(env, alice) == owners + 3);
        BEAST_EXPECT(limitCounts(env, alice) == std::make_pair(3u, 1u));
    }

    void
    testLimitSetUpdate(FeatureBitset features)
    {
        testcase("FirewallLimitSet update");
        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const dave{"dave"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, carol, dave, backup);
        env.close();
        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();
        env(firewall::limitSet(alice.id(), XRP(100), kDay));
        env.close();
        auto const created = limitEntry(env, alice, xrpIssue());
        if (!BEAST_EXPECT(created))
            return;
        auto const start = created->getFieldU32(sfStartDate);

        auto expectLimit = [&](STAmount const& amount, std::uint32_t period) {
            auto const sle = limitEntry(env, alice, xrpIssue());
            BEAST_EXPECT(sle && sle->getFieldAmount(sfLimitAmount) == amount);
            BEAST_EXPECT(sle && sle->getFieldU32(sfPeriod) == period);
        };

        // Raising the amount loosens the limit.
        env(firewall::limitSet(alice.id(), XRP(200), kDay), Ter(temBAD_SIGNER));
        env(firewall::limitSet(alice.id(), XRP(200), kDay),
            Sig(sfCounterpartySignature, dave),
            signedFee(env),
            Ter(tefBAD_AUTH));
        env.close();
        expectLimit(XRP(100), kDay);
        env(firewall::limitSet(alice.id(), XRP(200), kDay),
            Sig(sfCounterpartySignature, carol),
            signedFee(env));
        env.close();
        expectLimit(XRP(200), kDay);

        // Lowering it tightens the limit.
        env(firewall::limitSet(alice.id(), XRP(50), kDay));
        env.close();
        expectLimit(XRP(50), kDay);

        // Shortening the period loosens it, lengthening it tightens it.
        env(firewall::limitSet(alice.id(), XRP(50), kDay - 1), Ter(temBAD_SIGNER));
        env.close();
        expectLimit(XRP(50), kDay);
        env(firewall::limitSet(alice.id(), XRP(50), kDay - 1),
            Sig(sfCounterpartySignature, carol),
            signedFee(env));
        env.close();
        expectLimit(XRP(50), kDay - 1);
        env(firewall::limitSet(alice.id(), XRP(50), 2 * kDay));
        env.close();
        expectLimit(XRP(50), 2 * kDay);

        // Raising the amount while lengthening the period is still a loosening.
        env(firewall::limitSet(alice.id(), XRP(60), 3 * kDay), Ter(temBAD_SIGNER));
        env.close();
        expectLimit(XRP(50), 2 * kDay);

        // An update inside the period keeps the period's start.
        auto const kept = limitEntry(env, alice, xrpIssue());
        BEAST_EXPECT(kept && kept->getFieldU32(sfStartDate) == start);

        // Once the period has ended, an update brings the start forward by whole
        // periods of the old length.
        env.close(env.now() + std::chrono::seconds(5 * kDay));
        std::uint32_t const at = closeTime(env);
        env(firewall::limitSet(alice.id(), XRP(40), 3 * kDay));
        env.close();
        auto const sle = limitEntry(env, alice, xrpIssue());
        if (!BEAST_EXPECT(sle))
            return;
        BEAST_EXPECT(
            sle->getFieldU32(sfStartDate) == start + (2 * kDay * ((at - start) / (2 * kDay))));
        BEAST_EXPECT(sle->getFieldU32(sfStartDate) > start);
        BEAST_EXPECT(sle->getFieldAmount(sfSpent) == XRP(0));
    }

    void
    testLimitSetUnlisted(FeatureBitset features)
    {
        testcase("FirewallLimitSet with unlisted assets refused");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const dave{"dave"};
        Account const erin{"erin"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, carol, dave, erin, backup);
        env.close();
        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();
        {
            auto jv = firewall::set(alice.id(), firewallID(alice));
            jv[sfUnlistedAssets.jsonName] = 1;
            env(jv);
        }
        env.close();

        // A new limit lets an asset out that was refused, so it loosens.
        env(firewall::limitSet(alice.id(), XRP(100), kDay), Ter(temBAD_SIGNER));
        env(firewall::limitSet(alice.id(), XRP(100), kDay),
            Sig(sfCounterpartySignature, dave),
            signedFee(env),
            Ter(tefBAD_AUTH));
        env.close();
        BEAST_EXPECT(!limitEntry(env, alice, xrpIssue()));
        for (auto const& signer : {dave, erin})
        {
            env(firewall::limitSet(alice.id(), XRP(10), kDay),
                firewall::kLimitSigner(signer),
                Ter(temBAD_SIGNER));
            env(firewall::limitSet(alice.id(), XRP(10), kDay),
                firewall::kLimitSigner(signer),
                Sig(sfCounterpartySignature, carol),
                signedFee(env));
        }
        env(firewall::limitSet(alice.id(), XRP(100), kDay),
            Sig(sfCounterpartySignature, carol),
            signedFee(env));
        env.close();
        BEAST_EXPECT(limitCounts(env, alice) == std::make_pair(3u, 2u));

        // With an account-wide limit, deleting a signer's limit leaves the
        // signer the larger account-wide one, so it loosens.
        env(firewall::limitDelete(alice.id(), xrpIssue()),
            firewall::kLimitSigner(dave),
            Ter(temBAD_SIGNER));
        env(firewall::limitDelete(alice.id(), xrpIssue()),
            firewall::kLimitSigner(dave),
            Sig(sfCounterpartySignature, carol),
            signedFee(env));
        env.close();
        BEAST_EXPECT(!limitEntry(env, alice, xrpIssue(), dave.id()));

        // Deleting the account-wide limit refuses the asset again, so it
        // tightens.
        env(firewall::limitDelete(alice.id(), xrpIssue()));
        env.close();
        BEAST_EXPECT(!limitEntry(env, alice, xrpIssue()));

        // Without one, deleting a signer's limit refuses the asset to that
        // signer, so it tightens too.
        env(firewall::limitDelete(alice.id(), xrpIssue()), firewall::kLimitSigner(erin));
        env.close();
        BEAST_EXPECT(!limitEntry(env, alice, xrpIssue(), erin.id()));
        BEAST_EXPECT(limitCounts(env, alice) == std::make_pair(0u, 0u));
    }

    void
    testLimitSetDelete(FeatureBitset features)
    {
        testcase("FirewallLimitSet delete");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const dave{"dave"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, carol, dave, backup);
        env.close();
        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();
        auto const owners = ownerCount(env, alice);

        // Deleting a limit that does not exist.
        env(firewall::limitDelete(alice.id(), xrpIssue()), Ter(tecNO_ENTRY));
        env.close();

        env(firewall::limitSet(alice.id(), XRP(100), kDay));
        env(firewall::limitSet(alice.id(), XRP(10), kDay), firewall::kLimitSigner(dave));
        env.close();
        BEAST_EXPECT(limitCounts(env, alice) == std::make_pair(2u, 1u));
        BEAST_EXPECT(ownerCount(env, alice) == owners + 2);

        // With unlisted assets allowed, deleting any limit loosens.
        env(firewall::limitDelete(alice.id(), xrpIssue()), Ter(temBAD_SIGNER));
        env(firewall::limitDelete(alice.id(), xrpIssue()),
            firewall::kLimitSigner(dave),
            Ter(temBAD_SIGNER));
        env.close();
        BEAST_EXPECT(limitCounts(env, alice) == std::make_pair(2u, 1u));

        env(firewall::limitDelete(alice.id(), xrpIssue()),
            firewall::kLimitSigner(dave),
            Sig(sfCounterpartySignature, carol),
            signedFee(env));
        env.close();
        BEAST_EXPECT(!limitEntry(env, alice, xrpIssue(), dave.id()));
        BEAST_EXPECT(limitCounts(env, alice) == std::make_pair(1u, 0u));
        BEAST_EXPECT(ownerCount(env, alice) == owners + 1);

        // The firewall cannot be deleted while a limit exists.
        env(firewall::del(alice.id(), firewallID(alice)),
            Sig(sfCounterpartySignature, carol),
            signedFee(env),
            Ter(tecHAS_OBLIGATIONS));
        env.close();
        BEAST_EXPECT(env.le(keylet::firewall(alice.id())));

        env(firewall::limitDelete(alice.id(), xrpIssue()),
            Sig(sfCounterpartySignature, carol),
            signedFee(env));
        env.close();
        BEAST_EXPECT(!limitEntry(env, alice, xrpIssue()));
        BEAST_EXPECT(limitCounts(env, alice) == std::make_pair(0u, 0u));
        BEAST_EXPECT(ownerCount(env, alice) == owners);

        // After the last limit, it can; a firewall made again has no limits.
        env(firewall::del(alice.id(), firewallID(alice)),
            Sig(sfCounterpartySignature, carol),
            signedFee(env));
        env.close();
        BEAST_EXPECT(!env.le(keylet::firewall(alice.id())));
        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();
        auto const sle = env.le(keylet::firewall(alice.id()));
        BEAST_EXPECT(sle && !sle->isFieldPresent(sfLimitCount));
        BEAST_EXPECT(sle && !sle->isFieldPresent(sfSignerLimitCount));
    }

    void
    testLimitSetFeeAndReserve(FeatureBitset features)
    {
        testcase("FirewallLimitSet fee and reserve");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        auto const base = env.current()->fees().base;
        env.fund(XRP(10000), carol, backup);
        // alice can hold the firewall and its backup preauthorization only.
        env.fund(drops(env.current()->fees().accountReserve(2, 1) + base * 10), alice);
        env.close();
        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();

        env(firewall::limitSet(alice.id(), XRP(1), kDay), Ter(tecINSUFFICIENT_RESERVE));
        env.close();
        BEAST_EXPECT(!limitEntry(env, alice, xrpIssue()));

        // A counterparty signature adds its base fee.
        env(firewall::limitSet(alice.id(), XRP(1), kDay),
            Sig(sfCounterpartySignature, carol),
            Fee(base),
            Ter(telINSUF_FEE_P));
        env.close();
    }

    void
    testLimitSetBatchInner(FeatureBitset features)
    {
        testcase("FirewallLimitSet as a Batch inner transaction");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, carol, backup);
        env.close();
        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();

        auto const seq = env.seq(alice);
        env(batch::outer(alice, seq, batch::calcBatchFee(env, 0, 2), tfAllOrNothing),
            batch::Inner(firewall::limitSet(alice.id(), XRP(100), kDay), seq + 1),
            batch::Inner(noop(alice), seq + 2),
            Ter(temINVALID_INNER_BATCH));
        env.close();
        BEAST_EXPECT(!limitEntry(env, alice, xrpIssue()));
    }

    // The permissions of the Delegate entry for (account, authorized).
    static std::vector<std::uint32_t>
    delegatePermissions(
        jtx::Env const& env,
        jtx::Account const& account,
        jtx::Account const& authorized)
    {
        std::vector<std::uint32_t> values;
        if (auto const sle = env.le(keylet::delegate(account.id(), authorized.id())))
        {
            for (auto const& permission : sle->getFieldArray(sfPermissions))
                values.push_back(permission.getFieldU32(sfPermissionValue));
        }
        return values;
    }

    void
    testAuthorityChanges(FeatureBitset features)
    {
        testcase("authority changes");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const dave{"dave"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, bob, carol, dave, backup);
        env.close();
        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();

        // A new regular key or signer list needs the counterparty, as on XLS-86.
        env(regkey(alice, dave), Ter(tefFIREWALL_BLOCK));
        env(signers(alice, 1, {{dave, 1}}), Ter(tefFIREWALL_BLOCK));
        env(regkey(alice, dave), Sig(sfCounterpartySignature, carol), signedFee(env));
        env(signers(alice, 1, {{dave, 1}}), Sig(sfCounterpartySignature, carol), signedFee(env));
        env.close();
        auto const account = env.le(keylet::account(alice.id()));
        BEAST_EXPECT(account && account->getAccountID(sfRegularKey) == dave.id());
        BEAST_EXPECT(env.le(keylet::signerList(alice.id())));

        // The master key still cannot be disabled.
        env(fset(alice, asfDisableMaster), Sig(alice), Ter(tefFIREWALL_BLOCK));

        // Granting a delegate a permission it does not hold loosens the
        // firewall: it needs the counterparty, and pays its fee.
        env(delegate::set(alice, bob, {"Payment"}), Ter(tefFIREWALL_BLOCK));
        env(delegate::set(alice, bob, {"Payment"}),
            Sig(sfCounterpartySignature, dave),
            signedFee(env),
            Ter(tefBAD_AUTH));
        env(delegate::set(alice, bob, {"Payment"}),
            Sig(sfCounterpartySignature, carol),
            Fee(env.current()->fees().base),
            Ter(telINSUF_FEE_P));
        env.close();
        BEAST_EXPECT(!env.le(keylet::delegate(alice.id(), bob.id())));

        auto const aliceBefore = env.balance(alice);
        env(delegate::set(alice, bob, {"Payment", "TrustSet"}),
            Sig(sfCounterpartySignature, carol),
            signedFee(env));
        env.close();
        BEAST_EXPECT(env.balance(alice) == aliceBefore - drops(env.current()->fees().base * 2));
        BEAST_EXPECT(delegatePermissions(env, alice, bob).size() == 2);

        // Adding one more permission to the list is a grant too.
        env(delegate::set(alice, bob, {"Payment", "TrustSet", "EscrowCreate"}),
            Ter(tefFIREWALL_BLOCK));
        env.close();
        BEAST_EXPECT(delegatePermissions(env, alice, bob).size() == 2);

        // Keeping a strict subset, or the same list, only tightens, so the owner
        // alone may do it.
        env(delegate::set(alice, bob, {"Payment", "TrustSet"}));
        env(delegate::set(alice, bob, {"TrustSet"}));
        env.close();
        BEAST_EXPECT(delegatePermissions(env, alice, bob).size() == 1);

        // An empty list deletes the Delegate entry, also owner-only.
        env(delegate::set(alice, bob, {}));
        env.close();
        BEAST_EXPECT(!env.le(keylet::delegate(alice.id(), bob.id())));

        // An inner Batch transaction cannot carry the consent for a grant.
        auto inner = delegate::set(alice, bob, {"Payment"});
        json::Value noSignature = json::ValueType::Object;
        noSignature[jss::SigningPubKey] = "";
        inner[sfCounterpartySignature.jsonName] = noSignature;
        auto const seq = env.seq(alice);
        env(batch::outer(alice, seq, batch::calcBatchFee(env, 0, 2), tfIndependent),
            batch::Inner(inner, seq + 1),
            batch::Inner(noop(alice), seq + 2));
        env.close();
        BEAST_EXPECT(!env.le(keylet::delegate(alice.id(), bob.id())));
    }

    void
    testAuthorityChangesWithoutAmendment(FeatureBitset features)
    {
        testcase("authority changes without the amendment");
        using namespace jtx;

        auto const firewallLimits = getRegisteredFeature("FirewallLimits");
        if (!BEAST_EXPECT(firewallLimits))
            return;

        // XLS-86 lets the owner alone grant a delegate.
        {
            Env env{*this, features - *firewallLimits};
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const carol{"carol"};
            Account const backup{"backup"};
            env.fund(XRP(10000), alice, bob, carol, backup);
            env.close();
            env(firewall::set(alice.id(), carol.id(), backup.id()));
            env.close();
            env(delegate::set(alice, bob, {"Payment"}));
            env.close();
            BEAST_EXPECT(delegatePermissions(env, alice, bob).size() == 1);
        }

        // Without a firewall, a counterparty signature on DelegateSet is not
        // verified.
        {
            Env env{*this, features};
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const dave{"dave"};
            env.fund(XRP(10000), alice, bob, dave);
            env.close();
            env(delegate::set(alice, bob, {"Payment"}),
                Sig(sfCounterpartySignature, dave),
                signedFee(env));
            env.close();
            BEAST_EXPECT(delegatePermissions(env, alice, bob).size() == 1);
        }
    }

    void
    testDisabled(FeatureBitset features)
    {
        testcase("disabled");
        using namespace jtx;

        auto const firewallLimits = getRegisteredFeature("FirewallLimits");
        if (!BEAST_EXPECT(firewallLimits))
            return;

        Env env{*this, features - *firewallLimits};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, bob, carol, backup);
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();
        auto const ownersBefore = ownerCount(env, alice);

        // FirewallLimitSet is refused.
        json::Value limit;
        limit[jss::TransactionType] = "FirewallLimitSet";
        limit[jss::Account] = alice.human();
        limit["Asset"][jss::currency] = "XRP";
        limit["LimitAmount"] = "1000000";
        limit["Period"] = 86400;
        env(limit, Ter(temDISABLED));

        // UnlistedAssets is refused on a create and on an update.
        auto create = firewall::set(bob.id(), carol.id(), backup.id());
        create["UnlistedAssets"] = 1;
        env(create, Ter(temDISABLED));

        auto update = firewall::set(alice.id(), keylet::firewall(alice.id()).key);
        update["UnlistedAssets"] = 1;
        env(update, Sig(sfCounterpartySignature, carol), signedFee(env), Ter(temDISABLED));

        // CounterpartySignature is refused on DelegateSet.
        env(delegate::set(alice, bob, {"Payment"}),
            Sig(sfCounterpartySignature, carol),
            signedFee(env),
            Ter(temDISABLED));
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == ownersBefore);
        BEAST_EXPECT(!env.le(keylet::firewall(bob.id())));
        BEAST_EXPECT(!env.le(keylet::delegate(alice.id(), bob.id())));
    }

public:
    void
    run() override
    {
        auto const all = jtx::testableAmendments();
        testDisabled(all);
        testFirewallSetUnlistedAssets(all);
        testLimitSetPreflight(all);
        testLimitSetCreate(all);
        testLimitSetUpdate(all);
        testLimitSetUnlisted(all);
        testLimitSetDelete(all);
        testLimitSetFeeAndReserve(all);
        testLimitSetBatchInner(all);
        testAuthorityChanges(all);
        testAuthorityChangesWithoutAmendment(all);
    }
};

BEAST_DEFINE_TESTSUITE(FirewallLimits, app, xrpl);

}  // namespace xrpl::test
