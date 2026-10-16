#include <test/jtx/AMM.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/acctdelete.h>
#include <test/jtx/amount.h>
#include <test/jtx/delegate.h>
#include <test/jtx/fee.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <string>
#include <utility>

namespace xrpl::test {

class TokenPreauth_test : public beast::unit_test::Suite
{
    static json::Value
    tokenTx(
        json::StaticString const& txType,
        jtx::Account const& issuer,
        jtx::Account const& holder,
        MPTID const& issuanceID,
        std::uint32_t flags)
    {
        json::Value jv;
        jv[jss::TransactionType] = txType;
        jv[jss::Account] = issuer.human();
        jv[sfHolder.jsonName] = holder.human();
        jv[sfMPTokenIssuanceID.jsonName] = to_string(issuanceID);
        jv[jss::Flags] = flags;
        return jv;
    }

    // Pre-authorizes holder, or with tfUnauthorize removes the
    // pre-authorization.
    static json::Value
    tokenPreauth(
        jtx::Account const& issuer,
        jtx::Account const& holder,
        MPTID const& issuanceID,
        std::uint32_t flags = 0)
    {
        return tokenTx(jss::TokenPreauth, issuer, holder, issuanceID, flags);
    }

    // Blocks holder, or with tfUnblock removes the block.
    static json::Value
    tokenBlock(
        jtx::Account const& issuer,
        jtx::Account const& holder,
        MPTID const& issuanceID,
        std::uint32_t flags = 0)
    {
        return tokenTx(jss::TokenBlock, issuer, holder, issuanceID, flags);
    }

    static bool
    isPreauthorized(jtx::Env& env, jtx::Account const& holder, MPTID const& issuanceID)
    {
        return static_cast<bool>(env.le(keylet::tokenPreauth(holder, issuanceID)));
    }

    static bool
    isBlocked(jtx::Env& env, jtx::Account const& holder, MPTID const& issuanceID)
    {
        return static_cast<bool>(env.le(keylet::tokenBlock(holder, issuanceID)));
    }

    static bool
    hasMPToken(jtx::Env& env, jtx::Account const& holder, MPTID const& issuanceID)
    {
        return static_cast<bool>(env.le(keylet::mptoken(issuanceID, holder)));
    }

    static bool
    isLocked(jtx::Env& env, jtx::Account const& holder, MPTID const& issuanceID)
    {
        auto const sle = env.le(keylet::mptoken(issuanceID, holder));
        return sle && sle->isFlag(lsfMPTLocked);
    }

    void
    testDisabled(FeatureBitset features)
    {
        testcase("Amendment disabled");
        using namespace jtx;

        Env env{*this, features - featureTokenPreauth};
        Account const gw{"gw"};
        Account const alice{"alice"};
        MPTTester mpt(env, gw, {.holders = {alice}});
        mpt.create({.flags = tfMPTCanLock});

        env(tokenBlock(gw, alice, mpt.issuanceID()), Ter(temDISABLED));
        env.close();

        // Opting in is unaffected.
        mpt.authorize({.account = alice});
        BEAST_EXPECT(hasMPToken(env, alice, mpt.issuanceID()));
    }

    void
    testPreflight(FeatureBitset features)
    {
        testcase("Preflight");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"};
        Account const alice{"alice"};
        MPTTester mpt(env, gw, {.holders = {alice}});
        mpt.create({.flags = tfMPTCanLock});
        auto const id = mpt.issuanceID();

        // Each transaction has a single flag; anything else is unknown.
        env(tokenPreauth(gw, alice, id, 0x00000002), Ter(temINVALID_FLAG));
        env(tokenPreauth(gw, alice, id, tfUnauthorize | 0x00000002), Ter(temINVALID_FLAG));
        env(tokenBlock(gw, alice, id, 0x00000002), Ter(temINVALID_FLAG));
        env(tokenBlock(gw, alice, id, tfUnblock | 0x00000002), Ter(temINVALID_FLAG));

        // Issuer may not name itself.
        env(tokenPreauth(gw, gw, id), Ter(temMALFORMED));
        env(tokenBlock(gw, gw, id), Ter(temMALFORMED));
        env(tokenPreauth(gw, gw, id, tfUnauthorize), Ter(temMALFORMED));
        env(tokenBlock(gw, gw, id, tfUnblock), Ter(temMALFORMED));
    }

    void
    testPreclaim(FeatureBitset features)
    {
        testcase("Preclaim");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const ghost{"ghost"};
        MPTTester mpt(env, gw, {.holders = {alice, bob}});
        mpt.create({.flags = tfMPTCanLock});
        auto const id = mpt.issuanceID();

        // Issuance must exist.
        MPTID const missing = makeMptID(env.seq(gw) + 10, gw);
        env(tokenBlock(gw, alice, missing), Ter(tecOBJECT_NOT_FOUND));
        env(tokenPreauth(gw, alice, missing), Ter(tecOBJECT_NOT_FOUND));

        // Only the issuer may act on its issuance.
        env(tokenBlock(bob, alice, id), Ter(tecNO_PERMISSION));
        env(tokenPreauth(bob, alice, id), Ter(tecNO_PERMISSION));

        // Nothing to remove.
        env(tokenBlock(gw, alice, id, tfUnblock), Ter(tecNO_ENTRY));
        env(tokenPreauth(gw, alice, id, tfUnauthorize), Ter(tecNO_ENTRY));

        // Pre-authorizing needs an existing account; blocking does not.
        env(tokenPreauth(gw, ghost, id), Ter(tecNO_DST));
        env(tokenBlock(gw, ghost, id));
        env.close();
        BEAST_EXPECT(isBlocked(env, ghost, id));

        // The entry's key does not include the issuer (the issuance ID
        // already does), so removal checks it explicitly.
        env(tokenBlock(bob, ghost, id, tfUnblock), Ter(tecNO_PERMISSION));
        env(tokenPreauth(gw, bob, id));
        env(tokenPreauth(alice, bob, id, tfUnauthorize), Ter(tecNO_PERMISSION));
        env.close();
        BEAST_EXPECT(isBlocked(env, ghost, id));
        BEAST_EXPECT(isPreauthorized(env, bob, id));

        // Pseudo-accounts can not be blocked or pre-authorized.
        {
            MPTTester dex(env, gw, {.holders = {alice}, .fund = false});
            dex.create({.flags = tfMPTCanLock | kMptDexFlags});
            dex.authorize({.account = alice});
            dex.pay(gw, alice, 1'000);
            AMM const amm(env, alice, MPT(dex)(100), XRP(100));
            env.close();
            Account const ammAccount{"amm", amm.ammAccount()};
            env(tokenBlock(gw, ammAccount, dex.issuanceID()), Ter(tecPSEUDO_ACCOUNT));
            env(tokenPreauth(gw, ammAccount, dex.issuanceID()), Ter(tecPSEUDO_ACCOUNT));
        }

        // A block does not lock, so it does not need an issuance that allows
        // locking.
        {
            MPTTester noLock(env, gw, {.holders = {alice, bob}, .fund = false});
            noLock.create({.flags = 0});
            noLock.authorize({.account = alice});
            env(tokenBlock(gw, alice, noLock.issuanceID()));
            env(tokenBlock(gw, bob, noLock.issuanceID()));
            env(tokenBlock(gw, ghost, noLock.issuanceID()));
            env.close();
            BEAST_EXPECT(isBlocked(env, alice, noLock.issuanceID()));
            BEAST_EXPECT(isBlocked(env, bob, noLock.issuanceID()));
            BEAST_EXPECT(isBlocked(env, ghost, noLock.issuanceID()));
            BEAST_EXPECT(!isLocked(env, alice, noLock.issuanceID()));
        }
    }

    void
    testStateMachine(FeatureBitset features)
    {
        testcase("State machine");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"};
        Account const alice{"alice"};
        MPTTester mpt(env, gw, {.holders = {alice}});
        mpt.create({.flags = tfMPTCanLock});
        auto const id = mpt.issuanceID();
        auto const baseOwners = env.ownerCount(gw);

        // A holder is never both pre-authorized and blocked.
        auto const state = [&] -> std::string {
            bool const preauthorized = isPreauthorized(env, alice, id);
            bool const blocked = isBlocked(env, alice, id);
            BEAST_EXPECT(!(preauthorized && blocked));
            if (blocked)
                return "blocked";
            return preauthorized ? "preauthorized" : "none";
        };

        // none -> Authorized
        env(tokenPreauth(gw, alice, id));
        env.close();
        BEAST_EXPECT(state() == "preauthorized");
        BEAST_EXPECT(env.ownerCount(gw) == baseOwners + 1);

        env(tokenPreauth(gw, alice, id), Ter(tecDUPLICATE));
        env(tokenBlock(gw, alice, id, tfUnblock), Ter(tecNO_ENTRY));

        // Pre-authorized -> blocked: the block replaces the pre-authorization,
        // so the reserve is unchanged.
        env(tokenBlock(gw, alice, id));
        env.close();
        BEAST_EXPECT(state() == "blocked");
        BEAST_EXPECT(env.ownerCount(gw) == baseOwners + 1);

        env(tokenBlock(gw, alice, id), Ter(tecDUPLICATE));
        // An authorize-only action never clears a block.
        env(tokenPreauth(gw, alice, id), Ter(tecNO_PERMISSION));
        env(tokenPreauth(gw, alice, id, tfUnauthorize), Ter(tecNO_ENTRY));
        env.close();
        BEAST_EXPECT(state() == "blocked");

        // Blocked -> none
        env(tokenBlock(gw, alice, id, tfUnblock));
        env.close();
        BEAST_EXPECT(state() == "none");
        BEAST_EXPECT(env.ownerCount(gw) == baseOwners);

        // none -> Blocked -> none, and none -> Authorized -> none
        env(tokenBlock(gw, alice, id));
        env.close();
        BEAST_EXPECT(state() == "blocked");
        env(tokenBlock(gw, alice, id, tfUnblock));
        env(tokenPreauth(gw, alice, id));
        env.close();
        BEAST_EXPECT(state() == "preauthorized");
        env(tokenPreauth(gw, alice, id, tfUnauthorize));
        env.close();
        BEAST_EXPECT(state() == "none");
        BEAST_EXPECT(env.ownerCount(gw) == baseOwners);
    }

    void
    testDestroyedIssuanceCleanup(FeatureBitset features)
    {
        testcase("Entries of a destroyed issuance can still be removed");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"};
        Account const alice{"alice"};
        MPTTester mpt(env, gw, {.holders = {alice}});
        mpt.create({.flags = tfMPTCanLock});
        auto const id = mpt.issuanceID();

        env(tokenBlock(gw, alice, id));
        env.close();
        auto const owners = env.ownerCount(gw);

        mpt.destroy();
        BEAST_EXPECT(env.ownerCount(gw) == owners - 1);

        env(tokenPreauth(gw, alice, id), Ter(tecOBJECT_NOT_FOUND));
        env(tokenBlock(gw, alice, id, tfUnblock));
        env.close();
        BEAST_EXPECT(!isBlocked(env, alice, id));
        BEAST_EXPECT(env.ownerCount(gw) == owners - 2);
    }

    void
    testAccountDelete(FeatureBitset features)
    {
        testcase("TokenPreauth entries do not block account deletion");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        MPTTester mpt(env, gw, {.holders = {alice, bob}});
        mpt.create({.flags = tfMPTCanLock});
        auto const id = mpt.issuanceID();

        env(tokenBlock(gw, alice, id));
        env(tokenPreauth(gw, bob, id));
        env.close();

        // While the issuance exists the issuer can not be deleted anyway.
        auto const acctDelFee = drops(env.current()->fees().increment);
        incLgrSeqForAccDel(env, gw);
        env(acctdelete(gw, alice), Fee(acctDelFee), Ter(tecHAS_OBLIGATIONS));
        env.close();

        // Once it is destroyed, the entries are cleaned up with the account.
        mpt.destroy();
        incLgrSeqForAccDel(env, gw);
        env(acctdelete(gw, alice), Fee(acctDelFee));
        env.close();
        BEAST_EXPECT(!env.le(keylet::account(gw)));
        BEAST_EXPECT(!isBlocked(env, alice, id));
        BEAST_EXPECT(!isPreauthorized(env, bob, id));

        // The holder never owned the entry, so it can be deleted regardless.
        Account const carol{"carol"};
        env.fund(XRP(1'000), carol);
        env.close();
        MPTTester m2(env, carol, {.holders = {bob}, .fund = false});
        m2.create({.flags = tfMPTCanLock});
        env(tokenBlock(carol, bob, m2.issuanceID()));
        env.close();
        // (bob's account is already old enough to delete.)
        env(acctdelete(bob, alice), Fee(acctDelFee));
        env.close();
        BEAST_EXPECT(!env.le(keylet::account(bob)));
        BEAST_EXPECT(isBlocked(env, bob, m2.issuanceID()));
    }

    void
    testReserve(FeatureBitset features)
    {
        testcase("Reserve");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"};
        Account const alice{"alice"};
        MPTTester mpt(env, gw, {.holders = {alice}, .xrp = XRP(10'000)});
        mpt.create({.flags = tfMPTCanLock});
        auto const id = mpt.issuanceID();

        // Leave gw just enough for its current reserve plus fees.
        auto const reserve = env.current()->fees().accountReserve(env.ownerCount(gw), 1);
        auto const baseFee = env.current()->fees().base;
        env(pay(gw, alice, env.balance(gw) - reserve - baseFee * 2));
        env.close();

        env(tokenBlock(gw, alice, id), Ter(tecINSUFFICIENT_RESERVE));
        env.close();
        BEAST_EXPECT(!isBlocked(env, alice, id));
    }

    void
    testDelegation(FeatureBitset features)
    {
        testcase("Delegation");
        using namespace jtx;

        // Transaction-level permissions: the delegate can perform every action
        // of the transactions it holds on the issuer's behalf; the issuer pays
        // the reserve, the delegate the fee.
        {
            Env env{*this, features};
            Account const gw{"gw"};
            Account const alice{"alice"};
            Account const ops{"ops"};
            MPTTester mpt(env, gw, {.holders = {alice}});
            mpt.create({.flags = tfMPTCanLock});
            auto const id = mpt.issuanceID();
            env.fund(XRP(1'000), ops);
            env.close();
            mpt.authorize({.account = alice});

            // No Delegate entry yet.
            env(tokenBlock(gw, alice, id), delegate::As(ops), Ter(terNO_DELEGATE_PERMISSION));
            env.close();

            // Permission over one transaction does not cover the other.
            env(delegate::set(gw, ops, {"TokenPreauth"}));
            env.close();
            env(tokenBlock(gw, alice, id), delegate::As(ops), Ter(terNO_DELEGATE_PERMISSION));
            env.close();

            env(delegate::set(gw, ops, {"TokenPreauth", "TokenBlock"}));
            env.close();

            auto const gwBalance = env.balance(gw);
            auto const opsBalance = env.balance(ops);
            auto const gwOwners = env.ownerCount(gw);
            auto const fee = env.current()->fees().base;

            env(tokenBlock(gw, alice, id), delegate::As(ops));
            env.close();
            BEAST_EXPECT(isBlocked(env, alice, id));
            BEAST_EXPECT(env.ownerCount(gw) == gwOwners + 1);
            BEAST_EXPECT(env.balance(gw) == gwBalance);
            BEAST_EXPECT(env.balance(ops) == opsBalance - fee);

            env(tokenBlock(gw, alice, id, tfUnblock), delegate::As(ops));
            env(tokenPreauth(gw, alice, id), delegate::As(ops));
            env.close();
            BEAST_EXPECT(!isBlocked(env, alice, id));
            BEAST_EXPECT(isPreauthorized(env, alice, id));

            env(tokenPreauth(gw, alice, id, tfUnauthorize), delegate::As(ops));
            env.close();
            BEAST_EXPECT(!isPreauthorized(env, alice, id));
            BEAST_EXPECT(env.ownerCount(gw) == gwOwners);
        }

        // The TokenBlockCreate granular permission lets an issuer hand
        // blocking to a compliance account without also letting it remove
        // blocks. It does not cover TokenPreauth.
        {
            Env env{*this, features};
            Account const gw{"gw"};
            Account const alice{"alice"};
            Account const ops{"ops"};
            MPTTester mpt(env, gw, {.holders = {alice}});
            mpt.create({.flags = tfMPTCanLock});
            auto const id = mpt.issuanceID();
            env.fund(XRP(1'000), ops);
            env.close();

            env(delegate::set(gw, ops, {"TokenBlockCreate"}));
            env.close();

            env(tokenBlock(gw, alice, id), delegate::As(ops));
            env.close();
            BEAST_EXPECT(isBlocked(env, alice, id));

            env(tokenBlock(gw, alice, id, tfUnblock),
                delegate::As(ops),
                Ter(terNO_DELEGATE_PERMISSION));
            env(tokenPreauth(gw, alice, id), delegate::As(ops), Ter(terNO_DELEGATE_PERMISSION));
            env(tokenPreauth(gw, alice, id, tfUnauthorize),
                delegate::As(ops),
                Ter(terNO_DELEGATE_PERMISSION));
            env.close();
            BEAST_EXPECT(isBlocked(env, alice, id));

            // Removing the block takes the issuer, or the transaction-level
            // TokenBlock permission.
            env(tokenBlock(gw, alice, id, tfUnblock));
            env.close();
            BEAST_EXPECT(!isBlocked(env, alice, id));
            env(delegate::set(gw, ops, {"TokenBlockCreate", "TokenBlock"}));
            env.close();
            env(tokenBlock(gw, alice, id), delegate::As(ops));
            env(tokenBlock(gw, alice, id, tfUnblock), delegate::As(ops));
            env.close();
            BEAST_EXPECT(!isBlocked(env, alice, id));
        }

        // A delegate acts only for the account that granted it: it can not
        // act for an issuer it holds no permission from.
        {
            Env env{*this, features};
            Account const gw{"gw"};
            Account const gw2{"gw2"};
            Account const alice{"alice"};
            Account const ops{"ops"};
            MPTTester mpt(env, gw2, {.holders = {alice}});
            mpt.create({.flags = tfMPTCanLock});
            env.fund(XRP(1'000), gw, ops);
            env.close();

            env(delegate::set(gw, ops, {"TokenPreauth", "TokenBlock"}));
            env.close();

            env(tokenBlock(gw2, alice, mpt.issuanceID()),
                delegate::As(ops),
                Ter(terNO_DELEGATE_PERMISSION));
            env.close();
            BEAST_EXPECT(!isBlocked(env, alice, mpt.issuanceID()));
        }
    }

    void
    testRPC(FeatureBitset features)
    {
        testcase("ledger_entry and account_objects");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"};
        Account const alice{"alice"};
        MPTTester mpt(env, gw, {.holders = {alice}});
        mpt.create({.flags = tfMPTCanLock});
        auto const id = mpt.issuanceID();

        Account const bob{"bob"};
        env.fund(XRP(1'000), bob);
        env(tokenBlock(gw, alice, id));
        env(tokenPreauth(gw, bob, id));
        env.close();
        auto const blockIndex = to_string(keylet::tokenBlock(alice, id).key);
        auto const preauthIndex = to_string(keylet::tokenPreauth(bob, id).key);

        auto const byKey = [&](json::StaticString const& type, Account const& holder) {
            json::Value params;
            params[jss::ledger_index] = jss::validated;
            params[type][jss::holder] = holder.human();
            params[type][jss::mpt_issuance_id] = to_string(id);
            return env.rpc("json", "ledger_entry", to_string(params))[jss::result];
        };

        // By business key: {holder, mpt_issuance_id}.
        {
            auto const jrr = byKey(jss::token_block, alice);
            BEAST_EXPECT(jrr[jss::index].asString() == blockIndex);
            BEAST_EXPECT(jrr[jss::node][sfLedgerEntryType.jsonName] == jss::TokenBlock);
            BEAST_EXPECT(jrr[jss::node][sfHolder.jsonName] == alice.human());
        }
        {
            auto const jrr = byKey(jss::token_preauth, bob);
            BEAST_EXPECT(jrr[jss::index].asString() == preauthIndex);
            BEAST_EXPECT(jrr[jss::node][sfLedgerEntryType.jsonName] == jss::TokenPreauth);
            BEAST_EXPECT(jrr[jss::node][sfHolder.jsonName] == bob.human());
        }

        // By index.
        {
            json::Value params;
            params[jss::ledger_index] = jss::validated;
            params[jss::token_block] = blockIndex;
            auto const jrr = env.rpc("json", "ledger_entry", to_string(params))[jss::result];
            BEAST_EXPECT(jrr[jss::node][sfLedgerEntryType.jsonName] == jss::TokenBlock);
        }

        // Wrong type, or the issuer as holder -> entryNotFound.
        BEAST_EXPECT(byKey(jss::token_preauth, alice)[jss::error] == "entryNotFound");
        BEAST_EXPECT(byKey(jss::token_block, bob)[jss::error] == "entryNotFound");
        BEAST_EXPECT(byKey(jss::token_block, gw)[jss::error] == "entryNotFound");

        // Missing holder -> malformed.
        {
            json::Value params;
            params[jss::ledger_index] = jss::validated;
            params[jss::token_block][jss::mpt_issuance_id] = to_string(id);
            auto const jrr = env.rpc("json", "ledger_entry", to_string(params))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "malformedRequest");
        }

        // The issuer's block list and pre-authorization list via
        // account_objects.
        for (auto const& [type, index] :
             {std::pair{jss::token_block, blockIndex}, std::pair{jss::token_preauth, preauthIndex}})
        {
            json::Value params;
            params[jss::account] = gw.human();
            params[jss::type] = type;
            auto const jrr = env.rpc("json", "account_objects", to_string(params))[jss::result];
            BEAST_EXPECT(jrr[jss::account_objects].size() == 1);
            BEAST_EXPECT(jrr[jss::account_objects][0u][jss::index].asString() == index);
        }
    }

public:
    void
    run() override
    {
        FeatureBitset const all{jtx::testableAmendments()};
        testDisabled(all);
        testPreflight(all);
        testPreclaim(all);
        testStateMachine(all);
        testDestroyedIssuanceCleanup(all);
        testAccountDelete(all);
        testReserve(all);
        testDelegation(all);
        testRPC(all);
    }
};

BEAST_DEFINE_TESTSUITE(TokenPreauth, app, xrpl);

}  // namespace xrpl::test
