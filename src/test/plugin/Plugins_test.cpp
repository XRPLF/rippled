//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/app/tx/impl/InvariantCheck.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>
#include <test/jtx/PluginEnv.h>
#include <test/jtx/TestHelpers.h>

namespace ripple {

namespace test {

static void
cleanup()
{
    resetPluginInvariantChecks();
    resetTxFunctions();
    clearPluginPointers();
    reinitialize();
}

inline FeatureBitset
supported_amendments_plugins()
{
    reinitialize();
    static const FeatureBitset ids = [] {
        auto const& sa = ripple::detail::supportedAmendments();
        std::vector<uint256> feats;
        feats.reserve(sa.size());
        for (auto const& [s, vote] : sa)
        {
            (void)vote;
            if (auto const f = getRegisteredFeature(s))
                feats.push_back(*f);
            else
                Throw<std::runtime_error>(
                    "Unknown feature: " + s + "  in supportedAmendments.");
        }
        return FeatureBitset(feats);
    }();
    return ids;
}

// Helper function that returns the owner count of an account root.
static std::uint32_t
ownerCount(test::jtx::Env const& env, test::jtx::Account const& acct)
{
    std::uint32_t ret{0};
    if (auto const sleAcct = env.le(acct))
        ret = sleAcct->at(sfOwnerCount);
    return ret;
}

class Plugins_test : public beast::unit_test::suite
{
private:
    std::uint32_t
    openLedgerSeq(jtx::Env& env)
    {
        return env.current()->seq();
    }

    // Close the ledger until the ledger sequence is large enough to close
    // the account.  If margin is specified, close the ledger so `margin`
    // more closes are needed
    void
    incLgrSeqForAccDel(
        jtx::Env& env,
        jtx::Account const& acc,
        std::uint32_t margin = 0)
    {
        int const delta = [&]() -> int {
            if (env.seq(acc) + 255 > openLedgerSeq(env))
                return env.seq(acc) - openLedgerSeq(env) + 255 - margin;
            return 0;
        }();
        BEAST_EXPECT(margin == 0 || delta >= 0);
        for (int i = 0; i < delta; ++i)
            env.close();
        BEAST_EXPECT(openLedgerSeq(env) == env.seq(acc) + 255 - margin);
    }

public:
    std::unique_ptr<Config>
    makeConfig(std::string pluginPath)
    {
        auto cfg = test::jtx::envconfig();
        cfg->PLUGINS.push_back(pluginPath);
        return cfg;
    }

    void
    testPluginLoading()
    {
        testcase("Load Plugin Transactors");

        using namespace jtx;
        Account const alice{"alice"};

        // plugin that doesn't exist
        {
            try
            {
                cleanup();
                // this should crash
                PluginEnv env{
                    *this,
                    makeConfig("plugin_test_faketest.xrplugin"),
                    FeatureBitset{supported_amendments_plugins()}};
                BEAST_EXPECT(false);
            }
            catch (std::exception const&)
            {
                BEAST_EXPECT(true);
            }
        }

        // valid plugin that exists
        {
            cleanup();
            PluginEnv env{
                *this,
                makeConfig("plugin_test_setregularkey.xrplugin"),
                FeatureBitset{supported_amendments_plugins()}};
            env.fund(XRP(5000), alice);
            BEAST_EXPECT(env.balance(alice) == XRP(5000));
            env.close();
        }

        // valid plugin with custom SType/SField
        {
            cleanup();
            PluginEnv env{
                *this,
                makeConfig("plugin_test_trustset.xrplugin"),
                FeatureBitset{supported_amendments_plugins()}};
            env.fund(XRP(5000), alice);
            BEAST_EXPECT(env.balance(alice) == XRP(5000));
            env.close();
        }

        // valid plugin with other features
        {
            cleanup();
            PluginEnv env{
                *this,
                makeConfig("plugin_test_escrowcreate.xrplugin"),
                FeatureBitset{supported_amendments_plugins()}};
            env.fund(XRP(5000), alice);
            BEAST_EXPECT(env.balance(alice) == XRP(5000));
            env.close();
        }
    }

    void
    testBasicTransactor()
    {
        testcase("Normal Plugin Transactor");

        using namespace jtx;
        Account const alice{"alice"};
        Account const bob{"bob"};

        cleanup();
        PluginEnv env{
            *this,
            makeConfig("plugin_test_setregularkey.xrplugin"),
            FeatureBitset{supported_amendments_plugins()}};
        env.fund(XRP(5000), alice);
        BEAST_EXPECT(env.balance(alice) == XRP(5000));

        // empty (but valid) transaction
        Json::Value jv;
        jv[jss::TransactionType] = "SetRegularKey2";
        jv[jss::Account] = alice.human();
        env(jv);
        env.close();

        // a transaction that actually sets the regular key of the account
        Json::Value jv2;
        jv2[jss::TransactionType] = "SetRegularKey2";
        jv2[jss::Account] = alice.human();
        jv2[sfRegularKey.jsonName] = to_string(bob.id());
        env(jv2);
        auto const accountRoot = env.le(alice);
        BEAST_EXPECT(
            accountRoot->isFieldPresent(sfRegularKey) &&
            (accountRoot->getAccountID(sfRegularKey) == bob.id()));

        env.close();
    }

    void
    testPluginSTypeSField()
    {
        testcase("Plugin STypes and SFields");

        using namespace jtx;
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const danny{"danny"};
        Account const edwina{"edwina"};

        std::string const amendmentName = "featurePluginTest";
        auto const trustSet2Amendment =
            sha512Half(Slice(amendmentName.data(), amendmentName.size()));

        cleanup();
        PluginEnv env{
            *this,
            makeConfig("plugin_test_trustset.xrplugin"),
            FeatureBitset{supported_amendments_plugins()},
            trustSet2Amendment};

        env.fund(XRP(5000), alice, bob, carol, danny);
        IOU const USD = bob["USD"];
        // sanity checks
        BEAST_EXPECT(env.balance(alice) == XRP(5000));
        BEAST_EXPECT(env.balance(bob) == XRP(5000));

        // valid transaction without any custom fields
        {
            Json::Value jv;
            jv[jss::TransactionType] = "TrustSet2";
            jv[jss::Account] = alice.human();
            {
                auto& ja = jv[jss::LimitAmount] =
                    USD(1000).value().getJson(JsonOptions::none);
                ja[jss::issuer] = bob.human();
            }
            env(jv);
            env.close();
            auto const trustline = env.le(keylet::line(alice, USD.issue()));
            BEAST_EXPECT(trustline != nullptr);
        }

        // valid transaction that uses QualityIn2
        {
            Json::Value jv;
            jv[jss::TransactionType] = "TrustSet2";
            jv[jss::Account] = carol.human();
            {
                auto& ja = jv[jss::LimitAmount] =
                    USD(1000).value().getJson(JsonOptions::none);
                ja[jss::issuer] = bob.human();
            }
            jv["QualityIn2"] = "101";
            env(jv);
            env.close();
            auto const trustline = env.le(keylet::line(carol, USD.issue()));
            BEAST_EXPECT(trustline != nullptr);
        }

        // test that the custom SType is outputted correctly
        {
            Json::Value params;
            params[jss::transaction] = to_string(env.tx()->getTransactionID());
            auto resp = env.rpc("json", "tx", to_string(params));

            BEAST_EXPECT(resp[jss::result]["QualityIn2"] == "101");
        }

        // valid transaction that uses FakeElement
        {
            Json::Value jv;
            jv[jss::TransactionType] = "TrustSet2";
            jv[jss::Account] = danny.human();
            {
                auto& ja = jv[jss::LimitAmount] =
                    USD(1000).value().getJson(JsonOptions::none);
                ja[jss::issuer] = bob.human();
            }
            jv["QualityIn2"] = "101";
            {
                Json::Value array(Json::arrayValue);
                Json::Value obj;
                Json::Value innerObj;
                innerObj[jss::Account] = bob.human();
                obj["FakeElement"] = innerObj;
                array.append(obj);
                jv["FakeArray"] = array;
            }
            env(jv);
            env.close();
            auto const trustline = env.le(keylet::line(danny, USD.issue()));
            BEAST_EXPECT(trustline != nullptr);
        }

        // invalid transaction with custom TER
        {
            Json::Value jv;
            jv[jss::TransactionType] = "TrustSet2";
            jv[jss::Account] = alice.human();
            {
                auto& ja = jv[jss::LimitAmount] =
                    USD(1000).value().getJson(JsonOptions::none);
                ja[jss::issuer] = bob.human();
            }
            auto const& tx = env.jt(jv, txflags(0x00000001));  // invalid flag

            // submit tx without expecting a TER object
            Serializer s;
            tx.stx->add(s);
            auto const& jr = env.rpc("submit", strHex(s.slice()));

            if (BEAST_EXPECT(
                    jr.isObject() && jr.isMember(jss::result) &&
                    jr[jss::result].isMember(jss::engine_result_code)))
            {
                auto const& ter =
                    jr[jss::result][jss::engine_result_code].asInt();
                BEAST_EXPECT(ter == -210);
                BEAST_EXPECT(
                    jr[jss::result][jss::engine_result_code].asInt() == -210);
                BEAST_EXPECT(
                    jr[jss::result][jss::engine_result_message] == "Test code");
                BEAST_EXPECT(
                    jr[jss::result][jss::engine_result] == "temINVALID_FLAG2");
            }
        }

        env.close();
    }

    void
    testPluginLedgerObjectInvariantCheck()
    {
        testcase("Plugin Ledger Objects and Invariant Checks");

        using namespace jtx;
        Account const alice{"alice"};
        Account const bob{"bob"};

        std::string const amendmentName = "featurePluginTest2";
        auto const newEscrowCreateAmendment =
            sha512Half(Slice(amendmentName.data(), amendmentName.size()));

        cleanup();
        PluginEnv env{
            *this,
            makeConfig("plugin_test_escrowcreate.xrplugin"),
            FeatureBitset{supported_amendments_plugins()},
            newEscrowCreateAmendment};

        env.fund(XRP(5000), alice);
        env.fund(XRP(5000), bob);
        // sanity checks
        BEAST_EXPECT(env.balance(alice) == XRP(5000));
        BEAST_EXPECT(env.balance(bob) == XRP(5000));

        static const std::uint16_t ltNEW_ESCROW = 0x0001;
        static const std::uint16_t NEW_ESCROW_NAMESPACE = 't';
        auto new_escrow_keylet = [](const AccountID& src,
                                    std::uint32_t seq) noexcept -> Keylet {
            return {ltNEW_ESCROW, indexHash(NEW_ESCROW_NAMESPACE, src, seq)};
        };

        // valid transaction
        {
            auto const seq = env.seq(alice);
            Json::Value jv;
            jv[jss::TransactionType] = "NewEscrowCreate";
            jv[jss::Account] = alice.human();
            jv[jss::Amount] = "10000";
            jv[jss::Destination] = alice.human();
            jv[sfFinishAfter.jsonName] =
                env.now().time_since_epoch().count() + 10;

            env(jv);
            auto const newEscrow = env.le(new_escrow_keylet(alice, seq));
            BEAST_EXPECT(newEscrow != nullptr);
            env.close();
        }

        {
            // Test account_objects type filter
            Json::Value params;
            params[jss::account] = alice.human();
            params[jss::type] = "new_escrow";
            auto resp = env.rpc("json", "account_objects", to_string(params));
            auto const& objs = resp[jss::result][jss::account_objects];

            if (BEAST_EXPECT(objs.isArray() && (objs.size() == 1)))
            {
                BEAST_EXPECT(objs[0u]["LedgerEntryType"] == "NewEscrow");
            }
        }

        {
            // Test account_objects deletion_blockers_only filter
            Json::Value params;
            params[jss::account] = alice.human();
            params[jss::deletion_blockers_only] = true;
            auto resp = env.rpc("json", "account_objects", to_string(params));

            if (BEAST_EXPECT(
                    resp[jss::result][jss::account_objects].isArray() &&
                    (resp[jss::result][jss::account_objects].size() == 1)))
            {
                auto const& objs = resp[jss::result][jss::account_objects];
                BEAST_EXPECT(objs[0u]["LedgerEntryType"] == "NewEscrow");
            }
        }

        {
            // Test ledger_data filter
            Json::Value params;
            params[jss::account] = alice.human();
            params[jss::type] = "new_escrow";
            auto resp = env.rpc("json", "ledger_data", to_string(params));

            auto const& objs = resp[jss::result][jss::state];

            if (BEAST_EXPECT(objs.isArray() && (objs.size() == 1)))
            {
                BEAST_EXPECT(objs[0u]["LedgerEntryType"] == "NewEscrow");
            }
        }

        {
            // Test deletion blocker
            incLgrSeqForAccDel(env, alice);

            auto const acctDelFee{drops(env.current()->fees().increment)};
            env(acctdelete(alice, bob),
                fee(acctDelFee),
                ter(tecHAS_OBLIGATIONS));
        }

        {
            // invalid transaction that triggers the invariant check
            BEAST_EXPECT(ownerCount(env, bob) == 0);
            Json::Value jv;
            jv[jss::TransactionType] = "NewEscrowCreate";
            jv[jss::Account] = bob.human();
            jv[jss::Amount] = "0";
            jv[jss::Destination] = bob.human();
            jv[sfFinishAfter.jsonName] =
                env.now().time_since_epoch().count() + 10;

            env(jv, ter(tecINVARIANT_FAILED));
            BEAST_EXPECT(ownerCount(env, bob) == 0);
        }
    }

    void
    testPluginFailure()
    {
        testcase("Plugin Failure cases");

        using namespace jtx;
        Account const alice{"alice"};

        // invalid plugin with bad transactor type
        {
            bool errored = false;
            cleanup();

            try
            {
                // this should crash
                PluginEnv env{
                    *this,
                    makeConfig("plugin_test_badtransactor.xrplugin"),
                    FeatureBitset{supported_amendments_plugins()}};
                BEAST_EXPECT(false);
            }
            catch (...)
            {
                errored = true;
            }
            BEAST_EXPECT(errored);
        }

        // invalid plugin with bad ledger entry type
        {
            bool errored = false;
            cleanup();
            try
            {
                // this should crash
                PluginEnv env{
                    *this,
                    makeConfig("plugin_test_badledgerentry.xrplugin"),
                    FeatureBitset{supported_amendments_plugins()}};
                BEAST_EXPECT(false);
            }
            catch (...)
            {
                errored = true;
            }
            BEAST_EXPECT(errored);
        }

        // invalid plugin with bad SType ID
        {
            bool errored = false;
            cleanup();
            try
            {
                // this should crash
                PluginEnv env{
                    *this,
                    makeConfig("plugin_test_badstypeid.xrplugin"),
                    FeatureBitset{supported_amendments_plugins()}};
                BEAST_EXPECT(false);
            }
            catch (...)
            {
                errored = true;
            }
            BEAST_EXPECT(errored);
        }

        // invalid plugin with bad SType ID for a custom SField
        {
            bool errored = false;
            cleanup();
            try
            {
                // this should crash
                PluginEnv env{
                    *this,
                    makeConfig("plugin_test_badsfieldtypeid.xrplugin"),
                    FeatureBitset{supported_amendments_plugins()}};
                BEAST_EXPECT(false);
            }
            catch (...)
            {
                errored = true;
            }
            BEAST_EXPECT(errored);
        }

        // invalid plugin with bad (SType ID, Field Value) pair for a custom
        // SField
        {
            bool errored = false;
            cleanup();
            try
            {
                // this should crash
                PluginEnv env{
                    *this,
                    makeConfig("plugin_test_badsfieldtypepair.xrplugin"),
                    FeatureBitset{supported_amendments_plugins()}};
                BEAST_EXPECT(false);
            }
            catch (...)
            {
                errored = true;
            }
            BEAST_EXPECT(errored);
        }

        // invalid plugin with bad TER code
        {
            cleanup();
            PluginEnv env{
                *this,
                makeConfig("plugin_test_badtercode.xrplugin"),
                FeatureBitset{supported_amendments_plugins()}};

            env(pay(env.master, alice, XRP(5000)), ter(tefEXCEPTION));
        }

        // invalid plugin with bad inner object format
        {
            bool errored = false;
            cleanup();
            try
            {
                // this should crash
                PluginEnv env{
                    *this,
                    makeConfig("plugin_test_badinnerobject.xrplugin"),
                    FeatureBitset{supported_amendments_plugins()}};
                BEAST_EXPECT(false);
            }
            catch (...)
            {
                errored = true;
            }
            BEAST_EXPECT(errored);
        }
    }

    void
    run() override
    {
        using namespace test::jtx;
        testPluginLoading();
        testBasicTransactor();
        testPluginSTypeSField();
        testPluginLedgerObjectInvariantCheck();
        testPluginFailure();

        // run after all plugin tests
        // to ensure that no leftover plugin data affects other tests
        cleanup();

        // don't allow any more modifications of amendments
        registrationIsDone();
    }
};

BEAST_DEFINE_TESTSUITE(Plugins, plugins, ripple);

}  // namespace test
}  // namespace ripple
