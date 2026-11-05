#include <test/jtx/AMM.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/batch.h>
#include <test/jtx/check.h>
#include <test/jtx/delegate.h>
#include <test/jtx/escrow.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/last_ledger_sequence.h>
#include <test/jtx/mpt.h>
#include <test/jtx/multisign.h>
#include <test/jtx/noop.h>
#include <test/jtx/pay.h>
#include <test/jtx/paychan.h>
#include <test/jtx/rate.h>
#include <test/jtx/regkey.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/sig.h>
#include <test/jtx/sponsor.h>
#include <test/jtx/ter.h>
#include <test/jtx/token.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/helpers/FirewallLimitHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/jss.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
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

        auto const unlisted = [&](std::uint8_t value) {
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
            jv[sfMaxFee.jsonName] = to_string(XRP(1).value().xrp());
            env(jv, Ter(temBAD_SIGNER));
        }
        {
            auto jv = firewall::set(alice.id(), fwID);
            jv[sfMaxFee.jsonName] = to_string(XRP(1).value().xrp());
            env(jv, Ter(temBAD_SIGNER));
        }
        env(firewall::set(alice.id(), fwID), firewall::kCounterparty(dave), Ter(temBAD_SIGNER));
        env(unlisted(1), firewall::kCounterparty(dave), Ter(temBAD_SIGNER));
        {
            auto jv = unlisted(1);
            jv[sfDestinationTag.jsonName] = 7;
            env(jv, Ter(temBAD_SIGNER));
        }
        env.close();
        auto const after = env.le(keylet::firewall(alice.id()));
        BEAST_EXPECT(after && !after->isFieldPresent(sfMaxFee));
        BEAST_EXPECT(after && after->getAccountID(sfCounterparty) == carol.id());
        BEAST_EXPECT(after && !after->isFieldPresent(sfDestinationTag));
        BEAST_EXPECT(unlistedAssets(env, alice) == 0);
    }

    static constexpr std::uint32_t kDay = 86'400;

    static std::uint32_t
    closeTime(jtx::Env& env)
    {
        return env.current()->parentCloseTime().time_since_epoch().count();
    }

    // The owner's limit on the asset, read from the entry that holds the
    // balance; unseated when the asset has no limit.
    static std::optional<STObject>
    limitEntry(jtx::Env const& env, jtx::Account const& owner, Asset const& asset)
    {
        auto const location = firewallLimitField(owner.id(), asset);
        if (!location)
            return std::nullopt;
        auto const sle = env.le(location->entry);
        if (!sle || !sle->isFieldPresent(*location->field))
            return std::nullopt;
        return sle->getFieldObject(*location->field);
    }

    // The firewall's LimitCount, 0 when absent.
    static std::uint32_t
    limitCount(jtx::Env const& env, jtx::Account const& account)
    {
        auto const sle = env.le(keylet::firewall(account.id()));
        return sle ? (*sle)[~sfLimitCount].value_or(0) : 0;
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
        auto const usd = gw["USD"];
        env(trust(alice, usd(1000), tfSetNoRipple));
        env.close();
        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();

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
        env.fund(XRP(10000), alice, bob, carol, backup, gw);
        env.close();
        auto const usd = gw["USD"];
        env(trust(alice, usd(1000), tfSetNoRipple));
        env.close();

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
        auto const limit = limitEntry(env, alice, xrpIssue());
        if (!BEAST_EXPECT(limit))
            return;
        BEAST_EXPECT(limit->getFieldAmount(sfLimitAmount) == XRP(100));
        BEAST_EXPECT(limit->getFieldU32(sfPeriod) == kDay);
        BEAST_EXPECT(limit->getFieldAmount(sfSpent) == XRP(0));
        BEAST_EXPECT(limit->getFieldU32(sfStartDate) == createdAt);
        BEAST_EXPECT(ownerCount(env, alice) == owners);
        BEAST_EXPECT(limitCount(env, alice) == 1);

        // A zero limit stops the asset.
        env(firewall::limitSet(alice.id(), usd(0), kDay));
        env.close();
        auto const usdLimit = limitEntry(env, alice, usd.issue());
        BEAST_EXPECT(usdLimit && usdLimit->getFieldAmount(sfLimitAmount) == usd(0));
        BEAST_EXPECT(usdLimit && usdLimit->getFieldAmount(sfSpent) == usd(0));
        BEAST_EXPECT(ownerCount(env, alice) == owners);
        BEAST_EXPECT(limitCount(env, alice) == 2);
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

        auto const expectLimit = [&](STAmount const& amount, std::uint32_t period) {
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
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, carol, dave, backup);
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
        env(firewall::limitSet(alice.id(), XRP(100), kDay),
            Sig(sfCounterpartySignature, carol),
            signedFee(env));
        env.close();
        BEAST_EXPECT(limitEntry(env, alice, xrpIssue()));
        BEAST_EXPECT(limitCount(env, alice) == 1);

        // Deleting the limit refuses the asset again, so it tightens.
        env(firewall::limitDelete(alice.id(), xrpIssue()));
        env.close();
        BEAST_EXPECT(!limitEntry(env, alice, xrpIssue()));
        BEAST_EXPECT(limitCount(env, alice) == 0);
    }

    void
    testLimitSetDelete(FeatureBitset features)
    {
        testcase("FirewallLimitSet delete");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, carol, backup);
        env.close();
        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();
        auto const owners = ownerCount(env, alice);

        // Deleting a limit that does not exist.
        env(firewall::limitDelete(alice.id(), xrpIssue()), Ter(tecNO_ENTRY));
        env.close();

        env(firewall::limitSet(alice.id(), XRP(100), kDay));
        env.close();
        BEAST_EXPECT(limitCount(env, alice) == 1);
        BEAST_EXPECT(ownerCount(env, alice) == owners);

        // With unlisted assets allowed, deleting a limit loosens.
        env(firewall::limitDelete(alice.id(), xrpIssue()), Ter(temBAD_SIGNER));
        env.close();
        BEAST_EXPECT(limitCount(env, alice) == 1);

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
        BEAST_EXPECT(limitCount(env, alice) == 0);
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

        // A limit is a field of an entry alice already has, so it takes no
        // reserve.
        auto const owners = ownerCount(env, alice);
        env(firewall::limitSet(alice.id(), XRP(1), kDay));
        env.close();
        BEAST_EXPECT(limitEntry(env, alice, xrpIssue()));
        BEAST_EXPECT(ownerCount(env, alice) == owners);

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

    void
    testLimitOnEntries(FeatureBitset features)
    {
        testcase("limits stored on the entries that hold the balances");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const gw{"gw"};
        MPTTester mpt(env, gw, {.holders = {alice}});
        mpt.create({.ownerCount = 1});
        mpt.authorize({.account = alice});

        // One issuer whose account is below alice's and one above it.
        std::optional<Account> lowGw;
        std::optional<Account> highGw;
        for (int i = 0; i < 32 && (!lowGw || !highGw); ++i)
        {
            Account const candidate{"issuer" + std::to_string(i)};
            if (candidate.id() < alice.id() && !lowGw)
                lowGw = candidate;
            if (candidate.id() > alice.id() && !highGw)
                highGw = candidate;
        }
        if (!BEAST_EXPECT(lowGw && highGw))
            return;
        auto const lowUsd = (*lowGw)["USD"];
        auto const highUsd = (*highGw)["USD"];
        env.fund(XRP(10000), carol, backup, *lowGw, *highGw);
        env.close();
        env(trust(alice, lowUsd(1000), tfSetNoRipple));
        env(trust(alice, highUsd(1000), tfSetNoRipple));
        env.close();
        makeFirewall(env, alice, carol, backup, {});
        auto const owners = ownerCount(env, alice);

        env(firewall::limitSet(alice.id(), XRP(100), kDay));
        env(firewall::limitSet(alice.id(), lowUsd(10), kDay));
        env(firewall::limitSet(alice.id(), highUsd(20), kDay));
        env(firewall::limitSet(alice.id(), mpt["MPT"](30), kDay));
        env.close();

        // XRP on the AccountRoot.
        auto const account = env.le(keylet::account(alice.id()));
        BEAST_EXPECT(
            account && account->isFieldPresent(sfFirewallLimit) &&
            account->getFieldObject(sfFirewallLimit).getFieldAmount(sfLimitAmount) == XRP(100));

        // An IOU on alice's side of the line: high when the issuer is below her.
        auto const lowLine = env.le(keylet::trustLine(alice.id(), lowUsd.issue()));
        BEAST_EXPECT(
            lowLine && lowLine->isFieldPresent(sfHighFirewallLimit) &&
            !lowLine->isFieldPresent(sfLowFirewallLimit) &&
            lowLine->getFieldObject(sfHighFirewallLimit).getFieldAmount(sfLimitAmount) ==
                lowUsd(10));

        // Low when the issuer is above her.
        auto const highLine = env.le(keylet::trustLine(alice.id(), highUsd.issue()));
        BEAST_EXPECT(
            highLine && highLine->isFieldPresent(sfLowFirewallLimit) &&
            !highLine->isFieldPresent(sfHighFirewallLimit) &&
            highLine->getFieldObject(sfLowFirewallLimit).getFieldAmount(sfLimitAmount) ==
                highUsd(20));

        // An MPT on the MPToken.
        auto const token = env.le(keylet::mptoken(mpt.issuanceID(), alice.id()));
        BEAST_EXPECT(
            token && token->isFieldPresent(sfFirewallLimit) &&
            token->getFieldObject(sfFirewallLimit).getFieldAmount(sfLimitAmount) == mpt["MPT"](30));

        // Each limit is counted on the firewall and takes no reserve.
        BEAST_EXPECT(limitCount(env, alice) == 4);
        BEAST_EXPECT(ownerCount(env, alice) == owners);
    }

    void
    testLimitSetNoHolding(FeatureBitset features)
    {
        testcase("FirewallLimitSet without a holding");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const gw{"gw"};
        env.fund(XRP(10000), alice, carol, backup, gw);
        env.close();
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        // alice holds LP tokens of an AMM from before her firewall.
        env(trust(alice, usd(1000), tfSetNoRipple));
        env.close();
        env(pay(gw, alice, usd(100)));
        env.close();
        AMM amm(env, gw, XRP(100), usd(100));
        amm.deposit(alice, XRP(10));
        env.close();

        // gw alone holds the EUR line between them.
        env(trust(gw, alice["EUR"](100)));
        env.close();
        MPTTester mpt(env, gw, {.fund = false});
        mpt.create();
        env.close();
        makeFirewall(env, alice, carol, backup, {});

        // An IOU with no line, or a line whose side of alice holds nothing.
        env(firewall::limitSet(alice.id(), gw["GBP"](10), kDay), Ter(tecNO_LINE));
        env(firewall::limitSet(alice.id(), eur(10), kDay), Ter(tecNO_LINE));

        // An MPT with no MPToken.
        env(firewall::limitSet(alice.id(), mpt["MPT"](10), kDay), Ter(tecOBJECT_NOT_FOUND));

        // An asset alice issues has no limit.
        env(firewall::limitSet(alice.id(), alice["USD"](10), kDay), Ter(temBAD_ISSUER));
        env(firewall::limitSet(
                alice.id(), STAmount{MPTIssue{makeMptID(1, alice.id())}, std::uint64_t{10}}, kDay),
            Ter(temBAD_ISSUER));

        // An AMM's LP token is refused: a limit would keep the AMM from being
        // deleted.
        STAmount const lpTokens{amm.lptIssue(), 10};
        BEAST_EXPECT(env.le(keylet::trustLine(alice.id(), amm.lptIssue())));
        env(firewall::limitSet(alice.id(), lpTokens, kDay), Ter(tecNO_PERMISSION));
        env.close();
        BEAST_EXPECT(limitCount(env, alice) == 0);
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

        // XLS-86 lets the owner alone grant a delegate.
        {
            Env env{*this, features - featureFirewallLimits};
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

    // Creates the owner's firewall and preauthorizes each destination.
    static void
    makeFirewall(
        jtx::Env& env,
        jtx::Account const& owner,
        jtx::Account const& counterparty,
        jtx::Account const& backup,
        std::vector<jtx::Account> const& destinations)
    {
        using namespace jtx;
        env(firewall::set(owner.id(), counterparty.id(), backup.id()));
        env.close();
        for (auto const& destination : destinations)
        {
            env(firewall::authorize(owner.id(), firewallID(owner), destination.id()),
                Sig(sfCounterpartySignature, counterparty),
                signedFee(env));
        }
        env.close();
    }

    // The stored Spent of a limit, unseated when the limit does not exist.
    static std::optional<STAmount>
    spentOf(jtx::Env const& env, jtx::Account const& owner, Asset const& asset)
    {
        auto const limit = limitEntry(env, owner, asset);
        if (!limit)
            return std::nullopt;
        return limit->getFieldAmount(sfSpent);
    }

    // The stored StartDate of a limit, 0 when the limit does not exist.
    static std::uint32_t
    startOf(jtx::Env const& env, jtx::Account const& owner, Asset const& asset)
    {
        auto const limit = limitEntry(env, owner, asset);
        return limit ? limit->getFieldU32(sfStartDate) : 0;
    }

    static NetClock::time_point
    at(std::uint64_t seconds)
    {
        return NetClock::time_point{NetClock::duration{seconds}};
    }

    void
    testPeriodHelper()
    {
        testcase("period reset helper");

        STObject limit = STObject::makeInnerObject(sfFirewallLimit);
        limit.setFieldAmount(sfSpent, STAmount{XRPAmount{10}});
        limit.setFieldU32(sfStartDate, 1000);
        limit.setFieldU32(sfPeriod, 100);

        // One second before the reset the period is unchanged.
        auto period = firewallLimitPeriod(limit, at(1099));
        BEAST_EXPECT(period.spent == STAmount{XRPAmount{10}});
        BEAST_EXPECT(period.startDate == 1000);

        // At the reset nothing has been spent.
        period = firewallLimitPeriod(limit, at(1100));
        BEAST_EXPECT(period.spent == STAmount{XRPAmount{0}});
        BEAST_EXPECT(period.startDate == 1100);

        // After several missed periods the start moves by whole periods.
        period = firewallLimitPeriod(limit, at(1555));
        BEAST_EXPECT(period.spent == STAmount{XRPAmount{0}});
        BEAST_EXPECT(period.startDate == 1500);

        // A limit with no period never resets.
        limit.setFieldU32(sfPeriod, 0);
        period = firewallLimitPeriod(limit, at(1'000'000));
        BEAST_EXPECT(period.spent == STAmount{XRPAmount{10}});
        BEAST_EXPECT(period.startDate == 1000);
    }

    void
    testLimitFieldHelper()
    {
        testcase("limit field helper");

        AccountID const low{1};
        AccountID const owner{2};
        AccountID const high{3};
        Currency const usd{4};

        // Whether the helper names the expected entry and field.
        auto const expectField = [&](Asset const& asset, Keylet const& entry, SField const& field) {
            auto const located = firewallLimitField(owner, asset);
            if (!BEAST_EXPECT(located))
                return;
            BEAST_EXPECT(located->entry.type == entry.type);
            BEAST_EXPECT(located->entry.key == entry.key);
            BEAST_EXPECT(located->field == &field);
        };

        // XRP is on the owner's AccountRoot.
        expectField(xrpIssue(), keylet::account(owner), sfFirewallLimit);

        // An IOU is on the owner's side of its line: high when the issuer is
        // below the owner, low when it is above.
        expectField(Issue{usd, low}, keylet::trustLine(owner, low, usd), sfHighFirewallLimit);
        expectField(Issue{usd, high}, keylet::trustLine(owner, high, usd), sfLowFirewallLimit);

        // An MPT is on the owner's MPToken.
        MPTID const mptID = makeMptID(5, low);
        expectField(MPTIssue{mptID}, keylet::mptoken(mptID, owner), sfFirewallLimit);

        // An asset the owner issues has no limit field.
        BEAST_EXPECT(!firewallLimitField(owner, Issue{usd, owner}));
        BEAST_EXPECT(!firewallLimitField(owner, MPTIssue{makeMptID(6, owner)}));
    }

    void
    testBoundHelpers()
    {
        testcase("bound helpers");

        AccountID const owner{1};
        AccountID const destination{3};

        // A bound or a commitment of zero binds nothing.
        STTx const zeroPayment{ttPAYMENT, [&](STObject& tx) {
                                   tx.setAccountID(sfAccount, owner);
                                   tx.setAccountID(sfDestination, destination);
                                   tx.setFieldAmount(sfAmount, STAmount{XRPAmount{0}});
                               }};
        BEAST_EXPECT(!firewallFieldBound(zeroPayment, AccountID{}));
        STTx const zeroCheck{ttCHECK_CREATE, [&](STObject& tx) {
                                 tx.setAccountID(sfAccount, owner);
                                 tx.setAccountID(sfDestination, destination);
                                 tx.setFieldAmount(sfSendMax, STAmount{XRPAmount{0}});
                             }};
        BEAST_EXPECT(!firewallCommitment(zeroCheck));
        BEAST_EXPECT(!firewallFieldBound(zeroCheck, AccountID{}));
    }

    void
    testPaymentLimit(FeatureBitset features)
    {
        testcase("payment limit");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, bob, carol, backup);
        env.close();
        makeFirewall(env, alice, carol, backup, {bob});
        env(firewall::limitSet(alice.id(), XRP(100), kDay));
        env.close();

        env(pay(alice, bob, XRP(60)));
        env.close();
        BEAST_EXPECT(spentOf(env, alice, xrpIssue()) == XRP(60));

        // A payment that would take the limit over is refused before it is
        // applied, and claims no fee.
        auto const balance = env.balance(alice);
        auto const seq = env.seq(alice);
        env(pay(alice, bob, XRP(41)), Ter(tefFIREWALL_LIMIT));
        env.close();
        BEAST_EXPECT(env.balance(alice) == balance);
        BEAST_EXPECT(env.seq(alice) == seq);
        BEAST_EXPECT(spentOf(env, alice, xrpIssue()) == XRP(60));

        env(pay(alice, bob, XRP(40)));
        env.close();
        BEAST_EXPECT(spentOf(env, alice, xrpIssue()) == XRP(100));
        env(pay(alice, bob, drops(1)), Ter(tefFIREWALL_LIMIT));

        // Raising the limit keeps what was spent in the period.
        env(firewall::limitSet(alice.id(), XRP(150), kDay),
            Sig(sfCounterpartySignature, carol),
            signedFee(env));
        env.close();
        BEAST_EXPECT(spentOf(env, alice, xrpIssue()) == XRP(100));

        // Lowering it below what was spent refuses every further charge, and a
        // charge of zero still passes.
        env(firewall::limitSet(alice.id(), XRP(50), kDay));
        env.close();
        BEAST_EXPECT(spentOf(env, alice, xrpIssue()) == XRP(100));
        env(pay(alice, bob, drops(1)), Ter(tefFIREWALL_LIMIT));
        env(noop(alice));
        env.close();
        BEAST_EXPECT(spentOf(env, alice, xrpIssue()) == XRP(100));
    }

    void
    testLimitReset(FeatureBitset features)
    {
        testcase("limit reset");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, bob, carol, backup);
        env.close();
        makeFirewall(env, alice, carol, backup, {bob});
        env(firewall::limitSet(alice.id(), XRP(100), kDay));
        env.close();
        std::uint64_t const start = startOf(env, alice, xrpIssue());
        if (!BEAST_EXPECT(start != 0))
            return;

        env(pay(alice, bob, XRP(100)));
        env.close();
        BEAST_EXPECT(spentOf(env, alice, xrpIssue()) == XRP(100));

        // A close lands one resolution step after the time it is asked for, so
        // these are the last close before the reset and the close at it.
        auto const resolution = env.closed()->header().closeTimeResolution;
        env.close(at(start + kDay) - (2 * resolution));
        BEAST_EXPECT(closeTime(env) == start + kDay - resolution.count());
        // A refused transaction is held for retry; this one may not outlive
        // the ledger.
        env(pay(alice, bob, drops(1)), LastLedgerSeq(env.current()->seq()), Ter(tefFIREWALL_LIMIT));

        // At the reset the whole limit is available again.
        env.close(at(start + kDay) - resolution);
        BEAST_EXPECT(closeTime(env) == start + kDay);
        env(pay(alice, bob, XRP(100)));
        env.close();
        BEAST_EXPECT(spentOf(env, alice, xrpIssue()) == XRP(100));
        BEAST_EXPECT(startOf(env, alice, xrpIssue()) == start + kDay);

        // After several missed periods the start moves by whole periods.
        env.close(at(start + (4 * kDay)) + (3 * resolution));
        std::uint64_t const now = closeTime(env);
        env(pay(alice, bob, XRP(30)));
        env.close();
        BEAST_EXPECT(spentOf(env, alice, xrpIssue()) == XRP(30));
        BEAST_EXPECT(startOf(env, alice, xrpIssue()) == start + (kDay * ((now - start) / kDay)));

        // A transaction that moves no value still brings an ended period to now.
        env.close(at(start + (5 * kDay)));
        std::uint64_t const later = closeTime(env);
        env(noop(alice));
        env.close();
        BEAST_EXPECT(spentOf(env, alice, xrpIssue()) == XRP(0));
        BEAST_EXPECT(startOf(env, alice, xrpIssue()) == start + (kDay * ((later - start) / kDay)));
    }

    void
    testUnlistedAssetsEnforced(FeatureBitset features)
    {
        testcase("unlisted assets enforced");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const gw{"gw"};
        env.fund(XRP(10000), alice, bob, carol, backup, gw);
        env.close();
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        for (auto const& holder : {alice, bob})
        {
            env(trust(holder, usd(1000), tfSetNoRipple));
            env(trust(holder, eur(1000), tfSetNoRipple));
        }
        env.close();
        env(pay(gw, alice, usd(100)));
        env(pay(gw, alice, eur(100)));
        env.close();
        makeFirewall(env, alice, carol, backup, {bob});
        env(firewall::limitSet(alice.id(), XRP(100), kDay));
        env.close();

        // Allowed by default, an asset with no limit leaves freely.
        env(pay(alice, bob, usd(10)));
        env(pay(alice, bob, eur(60)));
        env.close();

        {
            auto jv = firewall::set(alice.id(), firewallID(alice));
            jv[sfUnlistedAssets.jsonName] = 1;
            env(jv);
        }
        env.close();

        // Refused, an asset with no limit cannot leave.
        env(pay(alice, bob, usd(10)), Ter(tefFIREWALL_LIMIT));
        env(pay(alice, bob, eur(10)), Ter(tefFIREWALL_LIMIT));
        env(pay(alice, bob, XRP(10)));
        env.close();
        BEAST_EXPECT(env.balance(bob, usd) == usd(10));
        BEAST_EXPECT(env.balance(bob, eur) == eur(60));
        BEAST_EXPECT(spentOf(env, alice, xrpIssue()) == XRP(10));
    }

    void
    testFieldBounds(FeatureBitset features)
    {
        testcase("field bounds");
        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const gw{"gw"};
        env.fund(XRP(10000), alice, bob, carol, backup, gw);
        env.close();
        auto const usd = gw["USD"];
        env(trust(alice, usd(1000), tfSetNoRipple));
        env(trust(bob, usd(1000), tfSetNoRipple));
        env.close();
        env(pay(gw, alice, usd(100)));
        UInt256 const bobNft = token::getNextID(env, bob, 0, tfTransferable);
        env(token::mint(bob, 0), Txflags(tfTransferable));
        UInt256 const aliceNft = token::getNextID(env, alice, 0, tfTransferable);
        env(token::mint(alice, 0), Txflags(tfTransferable));
        env.close();
        makeFirewall(env, alice, carol, backup, {bob});

        auto const limit = [&](STAmount const& amount) {
            env(firewall::limitSet(alice.id(), amount, kDay));
            env.close();
        };
        auto const expectSpent = [&](STAmount const& amount) {
            BEAST_EXPECT(spentOf(env, alice, amount.asset()) == amount);
        };

        // A payment's bound is its SendMax when present, else its Amount.
        limit(usd(40));
        env(pay(alice, bob, usd(10)), Sendmax(usd(41)), Ter(tefFIREWALL_LIMIT));
        env(pay(alice, bob, usd(41)), Ter(tefFIREWALL_LIMIT));
        env(pay(alice, bob, usd(10)), Sendmax(usd(40)));
        env.close();
        expectSpent(usd(10));

        // An escrow, a new channel and funding a channel are bounded by Amount.
        limit(XRP(200));
        env(escrow::create(alice, bob, XRP(201)),
            escrow::kFinishTime(env.now() + 1s),
            Ter(tefFIREWALL_LIMIT));
        env(escrow::create(alice, bob, XRP(10)), escrow::kFinishTime(env.now() + 1s));
        env.close();
        expectSpent(XRP(10));
        auto const channel = paychan::channel(alice, bob, env.seq(alice));
        env(paychan::create(alice, bob, XRP(191), 100s, alice.pk()), Ter(tefFIREWALL_LIMIT));
        env(paychan::create(alice, bob, XRP(10), 100s, alice.pk()));
        env.close();
        expectSpent(XRP(20));
        env(paychan::fund(alice, channel, XRP(181)), Ter(tefFIREWALL_LIMIT));
        env(paychan::fund(alice, channel, XRP(10)));
        env.close();
        expectSpent(XRP(30));

        // A check is bounded by, and charged, its SendMax.
        env(check::create(alice, bob, XRP(171)), Ter(tefFIREWALL_LIMIT));
        env(check::create(alice, bob, XRP(40)));
        env.close();
        expectSpent(XRP(70));

        // A buy offer is bounded by, and charged, its Amount.
        env(token::createOffer(alice, bobNft, XRP(131)),
            token::Owner(bob),
            token::Destination(bob),
            Ter(tefFIREWALL_LIMIT));
        env(token::createOffer(alice, bobNft, XRP(30)), token::Owner(bob), token::Destination(bob));
        env.close();
        expectSpent(XRP(100));

        // A sell offer, and a mint that offers the NFT for sale, move nothing
        // out, so a used-up limit does not refuse them.
        limit(XRP(100));
        env(token::createOffer(alice, aliceNft, XRP(1000)),
            Txflags(tfSellNFToken),
            token::Destination(bob));
        env(token::mint(alice, 0),
            Txflags(tfTransferable),
            token::Amount(XRP(1000)),
            token::Destination(bob));
        env.close();
        expectSpent(XRP(100));
    }

    void
    testMptLimit(FeatureBitset features)
    {
        testcase("MPT limit");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        MPTTester mpt(env, gw, {.holders = {alice, bob}});
        mpt.create({.ownerCount = 1, .flags = tfMPTCanTransfer});
        mpt.authorize({.account = alice});
        mpt.authorize({.account = bob});
        auto const token = mpt["MPT"];
        env(pay(gw, alice, token(500)));
        env.fund(XRP(10000), carol, backup);
        env.close();
        makeFirewall(env, alice, carol, backup, {bob});
        env(firewall::limitSet(alice.id(), token(50), kDay));
        env.close();

        env(pay(alice, bob, token(30)));
        env(pay(alice, bob, token(21)), Ter(tefFIREWALL_LIMIT));
        env.close();
        BEAST_EXPECT(spentOf(env, alice, token(1).value().asset()) == token(30));
        BEAST_EXPECT(mpt.getBalance(bob) == 30);
    }

    void
    testBackupAndClaims(FeatureBitset features)
    {
        testcase("backup and channel claims");
        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, bob, carol, backup);
        env.close();
        makeFirewall(env, alice, carol, backup, {bob});
        env(firewall::limitSet(alice.id(), XRP(100), kDay));
        env.close();

        auto const channel = paychan::channel(alice, bob, env.seq(alice));
        env(paychan::create(alice, bob, XRP(100), 100s, alice.pk()));
        env.close();
        BEAST_EXPECT(spentOf(env, alice, xrpIssue()) == XRP(100));

        // A payment to the backup is neither refused nor charged.
        env(pay(alice, backup, XRP(500)));
        env.close();
        BEAST_EXPECT(spentOf(env, alice, xrpIssue()) == XRP(100));

        // A check to the backup is charged in full: the value arrives later.
        env(check::create(alice, backup, XRP(1)), Ter(tefFIREWALL_LIMIT));

        // The owner's claim pays the destination out of the channel, which was
        // charged when funded, so it is not charged again.
        env(paychan::claim(alice, channel, XRP(50)));
        env.close();
        BEAST_EXPECT(paychan::channelBalance(*env.current(), channel) == XRP(50));
        BEAST_EXPECT(spentOf(env, alice, xrpIssue()) == XRP(100));

        // Value coming back to the owner does not lower what was spent.
        env(pay(bob, alice, XRP(100)));
        env.close();
        BEAST_EXPECT(spentOf(env, alice, xrpIssue()) == XRP(100));
    }

    void
    testMeasuredOutflow(FeatureBitset features)
    {
        testcase("measured outflow");
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

        // The backup issues an IOU with a transfer fee, so a payment of it to
        // another destination splits between that destination and the backup.
        auto const bkp = backup["USD"];
        env(rate(backup, 1.5));
        env(trust(alice, bkp(1000), tfSetNoRipple));
        env(trust(bob, bkp(1000), tfSetNoRipple));
        auto const usd = gw["USD"];
        env(trust(alice, usd(1000), tfSetNoRipple));
        env(trust(bob, usd(1000), tfSetNoRipple));
        env.close();
        env(pay(backup, alice, bkp(100)));
        env(pay(gw, alice, usd(30)));
        env.close();
        makeFirewall(env, alice, carol, backup, {bob});
        env(firewall::limitSet(alice.id(), bkp(100), kDay));
        env(firewall::limitSet(alice.id(), usd(100), kDay));
        env(firewall::limitSet(alice.id(), XRP(100), kDay));
        env.close();

        // Of the 15 that leave, 5 reach the backup.
        env(pay(alice, bob, bkp(10)), Sendmax(bkp(15)));
        env.close();
        BEAST_EXPECT(env.balance(alice, bkp) == bkp(85));
        BEAST_EXPECT(spentOf(env, alice, bkp.issue()) == bkp(10));

        // A partial payment is charged what left, not its Amount.
        env(pay(alice, bob, usd(100)), Txflags(tfPartialPayment));
        env.close();
        BEAST_EXPECT(env.balance(bob, usd) == usd(30));
        BEAST_EXPECT(spentOf(env, alice, usd.issue()) == usd(30));

        // A fee a sponsor pays is not the owner's, so nothing is excluded.
        env(pay(alice, bob, XRP(10)),
            sponsor::As(dave, spfSponsorFee),
            Sig(sfSponsorSignature, dave));
        env.close();
        BEAST_EXPECT(spentOf(env, alice, xrpIssue()) == XRP(10));
    }

    void
    testCommitmentsSettled(FeatureBitset features)
    {
        testcase("commitments settled by others");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, bob, carol, backup);
        env.close();
        UInt256 const bobNft = token::getNextID(env, bob, 0, tfTransferable);
        env(token::mint(bob, 0), Txflags(tfTransferable));
        env.close();
        makeFirewall(env, alice, carol, backup, {bob});
        env(firewall::limitSet(alice.id(), XRP(70), kDay));
        env.close();

        UInt256 const checkID =
            keylet::check(alice.id(), SeqProxy::rawSequence(env.seq(alice))).key;
        env(check::create(alice, bob, XRP(40)));
        UInt256 const offerID =
            keylet::nftokenOffer(alice.id(), SeqProxy::rawSequence(env.seq(alice))).key;
        env(token::createOffer(alice, bobNft, XRP(30)), token::Owner(bob), token::Destination(bob));
        env.close();
        BEAST_EXPECT(spentOf(env, alice, xrpIssue()) == XRP(70));

        // The third party settles the commitment; it is not charged again.
        auto const aliceBefore = env.balance(alice);
        env(check::cash(bob, checkID, XRP(40)));
        env(token::acceptBuyOffer(bob, offerID));
        env.close();
        BEAST_EXPECT(env.balance(alice) == aliceBefore - XRP(70));
        BEAST_EXPECT(spentOf(env, alice, xrpIssue()) == XRP(70));
    }

    void
    testFailedChangesNoLimit(FeatureBitset features)
    {
        testcase("failed transactions change no limit");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const dave{"dave"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, bob, carol, dave, backup);
        env.close();
        makeFirewall(env, alice, carol, backup, {bob});
        env(firewall::limitSet(alice.id(), XRP(100'000), kDay));
        env.close();
        env(pay(alice, bob, XRP(10)));
        env.close();
        std::uint64_t const start = startOf(env, alice, xrpIssue());

        // After the period ends, a failed transaction does not bring it forward.
        env.close(at(start + (2 * kDay)));
        env(pay(alice, bob, XRP(50'000)), Ter(tecUNFUNDED_PAYMENT));
        env.close();
        BEAST_EXPECT(spentOf(env, alice, xrpIssue()) == XRP(10));
        BEAST_EXPECT(startOf(env, alice, xrpIssue()) == start);

        // An Allow-type outflow over the limit is measured at apply: it fails
        // with the fee claimed and writes nothing.
        env(firewall::limitSet(alice.id(), XRP(50), kDay));
        env.close();
        std::uint64_t const reset = startOf(env, alice, xrpIssue());
        auto const balance = env.balance(alice);
        auto const fee = env.current()->fees().base;
        env(sponsor::set_fee(alice, 0, XRP(51)), sponsor::SponseeAcc(dave), Ter(tecFIREWALL_LIMIT));
        env.close();
        BEAST_EXPECT(env.balance(alice) == balance - drops(fee));
        BEAST_EXPECT(spentOf(env, alice, xrpIssue()) == XRP(0));
        BEAST_EXPECT(startOf(env, alice, xrpIssue()) == reset);
        BEAST_EXPECT(!env.le(keylet::sponsorship(alice.id(), dave.id())));

        env(sponsor::set_fee(alice, 0, XRP(50)), sponsor::SponseeAcc(dave));
        env.close();
        BEAST_EXPECT(spentOf(env, alice, xrpIssue()) == XRP(50));
    }

    void
    testUnlistedAllowOutflow(FeatureBitset features)
    {
        testcase("unlisted Allow-type outflow refused at apply");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const dave{"dave"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, bob, carol, dave, backup);
        env.close();
        makeFirewall(env, alice, carol, backup, {bob});
        {
            auto jv = firewall::set(alice.id(), firewallID(alice));
            jv[sfUnlistedAssets.jsonName] = 1;
            env(jv);
        }
        env.close();

        auto const fee = env.current()->fees().base;
        auto const owners = ownerCount(env, alice);

        // With no XRP limit, funding FeeAmount moves XRP out: it fails with the
        // fee claimed and writes nothing.
        {
            auto const balance = env.balance(alice);
            env(sponsor::set_fee(alice, 0, XRP(1)),
                sponsor::SponseeAcc(dave),
                Ter(tecFIREWALL_LIMIT));
            env.close();
            BEAST_EXPECT(env.balance(alice) == balance - drops(fee));
            BEAST_EXPECT(ownerCount(env, alice) == owners);
            BEAST_EXPECT(!env.le(keylet::sponsorship(alice.id(), dave.id())));
        }

        // The same transaction type moving no XRP beyond the fee succeeds.
        {
            auto const balance = env.balance(alice);
            env(sponsor::set_reserve(alice, 0, 1), sponsor::SponseeAcc(dave));
            env.close();
            BEAST_EXPECT(env.balance(alice) == balance - drops(fee));
            BEAST_EXPECT(env.le(keylet::sponsorship(alice.id(), dave.id())));
        }
    }

    void
    testBatchCharges(FeatureBitset features)
    {
        testcase("Batch inner transactions charged");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, bob, carol, backup);
        env.close();
        makeFirewall(env, alice, carol, backup, {bob});
        env(firewall::limitSet(alice.id(), XRP(100), kDay));
        env.close();

        // Each inner transaction is charged against the limit.
        {
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batch::calcBatchFee(env, 0, 2), tfIndependent),
                batch::Inner(pay(alice, bob, XRP(60)), seq + 1),
                batch::Inner(pay(alice, bob, XRP(60)), seq + 2));
            env.close();
            BEAST_EXPECT(spentOf(env, alice, xrpIssue()) == XRP(60));
        }
    }

    // The engine result of jv carrying a counterparty signing key that is not a
    // public key. Simulation skips signature verification, which would refuse
    // the key before preflight.
    static std::string
    simulateBadCounterpartyKey(jtx::Env& env, json::Value jv)
    {
        json::Value sig = json::ValueType::Object;
        sig[sfSigningPubKey.jsonName] = "0102";
        jv[sfCounterpartySignature.jsonName] = sig;
        json::Value params;
        params[jss::tx_json] = jv;
        return env.rpc("json", "simulate", to_string(params))[jss::result][jss::engine_result]
            .asString();
    }

    void
    testBadCounterpartyKey(FeatureBitset features)
    {
        testcase("counterparty signing key that is not a key");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const agent{"agent"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, agent, carol, backup);
        env.close();
        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();

        BEAST_EXPECT(
            simulateBadCounterpartyKey(env, firewall::limitSet(alice.id(), XRP(100), kDay)) ==
            "temBAD_SIGNATURE");
        BEAST_EXPECT(
            simulateBadCounterpartyKey(env, delegate::set(alice, agent, {"Payment"})) ==
            "temBAD_SIGNATURE");
    }

    void
    testLimitAmountOutOfRange(FeatureBitset features)
    {
        testcase("LimitAmount out of range");
        using namespace jtx;

        // Amounts above the legal maximum, which only decoding a serialized
        // amount produces: XRP above kMaxNativeN and an MPT amount of 2^63.
        MPTID const mptID = makeMptID(1, AccountID{1});
        Serializer xrpRaw;
        xrpRaw.add64(std::uint64_t{0x4000000000000000} | (STAmount::kMaxNativeN + 1));
        SerialIter xrpIter(xrpRaw.slice());
        STAmount const tooMuchXrp{xrpIter, sfLimitAmount};
        Serializer mptRaw;
        mptRaw.add8(0x60);
        mptRaw.add64(std::uint64_t{0x8000000000000000});
        mptRaw.addBitString(mptID);
        SerialIter mptIter(mptRaw.slice());
        STAmount const tooMuchMpt{mptIter, sfLimitAmount};
        BEAST_EXPECT(!isLegalNet(tooMuchXrp));
        BEAST_EXPECT(!isLegalMPT(tooMuchMpt));

        for (bool const cleanup : {false, true})
        {
            Env env{*this, cleanup ? features : features - fixCleanup3_2_0};
            Account const alice{"alice"};
            Account const carol{"carol"};
            Account const backup{"backup"};
            env.fund(XRP(10000), alice, carol, backup);
            env.close();
            env(firewall::set(alice.id(), carol.id(), backup.id()));
            env.close();

            for (auto const& [valid, tooMuch] : {
                     std::pair{STAmount{XRPAmount{100}}, tooMuchXrp},
                     std::pair{STAmount{MPTIssue{mptID}, std::uint64_t{100}}, tooMuchMpt},
                 })
            {
                STTx tx{*env.jt(firewall::limitSet(alice.id(), valid, kDay)).stx};
                tx.setFieldAmount(sfLimitAmount, tooMuch);
                tx.sign(alice.pk(), alice.sk());

                // FirewallLimitSet's own amount check refuses it; with
                // fixCleanup3_2_0 the universal amount check refuses it first.
                auto const jr = env.rpc("submit", strHex(tx.getSerializer().slice()));
                BEAST_EXPECT(jr[jss::result][jss::engine_result] == "temBAD_AMOUNT");
                env.close();
                BEAST_EXPECT(!limitEntry(env, alice, valid.asset()));
            }
        }
    }

    void
    testEmptyHoldingDeleted(FeatureBitset features)
    {
        testcase("empty holding with a limit kept");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        MPTTester mpt(env, gw, {.holders = {alice}});
        mpt.create({.ownerCount = 1});
        mpt.authorize({.account = alice});
        env.fund(XRP(10000), bob, carol, backup);
        env.close();
        makeFirewall(env, alice, carol, backup, {bob});
        env(firewall::limitSet(alice.id(), XRP(100), kDay));
        env(firewall::limitSet(alice.id(), mpt["MPT"](50), kDay));
        env.close();

        // Deleting the MPToken would remove its limit, so it is refused.
        auto const mptAsset = mpt["MPT"](1).value().asset();
        mpt.authorize({.account = alice, .flags = tfMPTUnauthorize, .err = tecHAS_OBLIGATIONS});
        env.close();
        BEAST_EXPECT(env.le(keylet::mptoken(mpt.issuanceID(), alice.id())));
        BEAST_EXPECT(spentOf(env, alice, mptAsset) == mpt["MPT"](0));

        // Once the limit is deleted, deleting the MPToken moves nothing and
        // charges no limit.
        env(firewall::limitDelete(alice.id(), mptAsset),
            Sig(sfCounterpartySignature, carol),
            signedFee(env));
        env.close();
        mpt.authorize({.account = alice, .flags = tfMPTUnauthorize});
        env.close();
        BEAST_EXPECT(!env.le(keylet::mptoken(mpt.issuanceID(), alice.id())));
        BEAST_EXPECT(spentOf(env, alice, xrpIssue()) == XRP(0));
    }

    void
    testHoldingKept(FeatureBitset features)
    {
        testcase("holding with a limit kept");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const gw{"gw"};
        env.fund(XRP(10000), alice, carol, backup, gw);
        env.close();
        auto const usd = gw["USD"];
        // alice's side ripples, as her account does, so it is default once empty.
        env(trust(alice, usd(1000)));
        env.close();
        env(pay(gw, alice, usd(100)));
        env.close();
        makeFirewall(env, alice, carol, backup, {gw});
        env(firewall::limitSet(alice.id(), usd(1000), kDay));
        env.close();

        // Whether alice's side of the line keeps its reserve.
        auto const aliceReserved = [&]() {
            auto const line = env.le(keylet::trustLine(alice.id(), usd.issue()));
            if (!line)
                return false;
            bool const aliceIsLow = alice.id() < gw.id();
            return line->isFlag(aliceIsLow ? lsfLowReserve : lsfHighReserve);
        };

        // With no trust limit left, paying back the whole balance would clear
        // alice's side; the limit keeps it.
        env(trust(alice, usd(0)));
        env.close();
        env(pay(alice, gw, usd(100)));
        env.close();
        BEAST_EXPECT(env.balance(alice, usd) == usd(0));
        BEAST_EXPECT(aliceReserved());

        // A TrustSet that would leave the line default keeps it too.
        env(trust(alice, usd(0)));
        env.close();
        BEAST_EXPECT(aliceReserved());

        // Once the limit is deleted, the same TrustSet deletes the line.
        env(firewall::limitDelete(alice.id(), usd.issue()),
            Sig(sfCounterpartySignature, carol),
            signedFee(env));
        env.close();
        BEAST_EXPECT(!limitEntry(env, alice, usd.issue()));
        env(trust(alice, usd(0)));
        env.close();
        BEAST_EXPECT(!env.le(keylet::trustLine(alice.id(), usd.issue())));
    }

    void
    testCommitmentOtherAsset(FeatureBitset features)
    {
        testcase("commitment in another asset than the fee");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const gw{"gw"};
        env.fund(XRP(10000), alice, bob, carol, backup, gw);
        env.close();
        auto const usd = gw["USD"];
        env(trust(alice, usd(1000), tfSetNoRipple));
        env.close();
        env(pay(gw, alice, usd(100)));
        env.close();
        makeFirewall(env, alice, carol, backup, {bob});
        env(firewall::limitSet(alice.id(), XRP(70), kDay));
        env(firewall::limitSet(alice.id(), usd(50), kDay));
        env.close();

        // A check committing USD charges the USD limit; the XRP that leaves is
        // only the fee, so the XRP limit is charged nothing.
        env(check::create(alice, bob, usd(30)));
        env.close();
        BEAST_EXPECT(spentOf(env, alice, usd.issue()) == usd(30));
        BEAST_EXPECT(spentOf(env, alice, xrpIssue()) == XRP(0));
        env(check::create(alice, bob, usd(21)), Ter(tefFIREWALL_LIMIT));
    }

    void
    testDisabled(FeatureBitset features)
    {
        testcase("disabled");
        using namespace jtx;

        Env env{*this, features - featureFirewallLimits};
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

        // Every update needs the counterparty, as on XLS-86.
        auto maxFee = firewall::set(alice.id(), keylet::firewall(alice.id()).key);
        maxFee[sfMaxFee.jsonName] = to_string(XRP(1).value().xrp());
        env(maxFee, Ter(temBAD_SIGNER));

        // CounterpartySignature is refused on DelegateSet.
        env(delegate::set(alice, bob, {"Payment"}),
            Sig(sfCounterpartySignature, carol),
            signedFee(env),
            Ter(temDISABLED));
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == ownersBefore);
        auto const sle = env.le(keylet::firewall(alice.id()));
        BEAST_EXPECT(sle && !sle->isFieldPresent(sfMaxFee));
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
        testLimitOnEntries(all);
        testLimitSetNoHolding(all);
        testAuthorityChanges(all);
        testAuthorityChangesWithoutAmendment(all);
        testPeriodHelper();
        testLimitFieldHelper();
        testBoundHelpers();
        testPaymentLimit(all);
        testLimitReset(all);
        testUnlistedAssetsEnforced(all);
        testFieldBounds(all);
        testMptLimit(all);
        testBackupAndClaims(all);
        testMeasuredOutflow(all);
        testCommitmentsSettled(all);
        testFailedChangesNoLimit(all);
        testUnlistedAllowOutflow(all);
        testBatchCharges(all);
        testBadCounterpartyKey(all);
        testLimitAmountOutOfRange(all);
        testEmptyHoldingDeleted(all);
        testHoldingKept(all);
        testCommitmentOtherAsset(all);
    }
};

BEAST_DEFINE_TESTSUITE(FirewallLimits, app, xrpl);

}  // namespace xrpl::test
