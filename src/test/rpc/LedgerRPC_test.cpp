//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2016 Ripple Labs Inc.

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

#include <ripple/app/misc/Manifest.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/beast/unit_test.h>
#include <ripple/protocol/AccountID.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/STXChainBridge.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>
#include <test/jtx/Oracle.h>
#include <test/jtx/attester.h>
#include <test/jtx/multisign.h>
#include <test/jtx/xchain_bridge.h>

namespace ripple {

class LedgerRPC_XChain_test : public beast::unit_test::suite,
                              public test::jtx::XChainBridgeObjects
{
    void
    checkErrorValue(
        Json::Value const& jv,
        std::string const& err,
        std::string const& msg)
    {
        if (BEAST_EXPECT(jv.isMember(jss::status)))
            BEAST_EXPECT(jv[jss::status] == "error");
        if (BEAST_EXPECT(jv.isMember(jss::error)))
            BEAST_EXPECT(jv[jss::error] == err);
        if (msg.empty())
        {
            BEAST_EXPECT(
                jv[jss::error_message] == Json::nullValue ||
                jv[jss::error_message] == "");
        }
        else if (BEAST_EXPECT(jv.isMember(jss::error_message)))
            BEAST_EXPECT(jv[jss::error_message] == msg);
    }

    void
    testLedgerEntryBridge()
    {
        testcase("ledger_entry: bridge");
        using namespace test::jtx;

        Env mcEnv{*this, features};
        Env scEnv(*this, envconfig(port_increment, 3), features);

        createBridgeObjects(mcEnv, scEnv);

        std::string const ledgerHash{to_string(mcEnv.closed()->info().hash)};
        std::string bridge_index;
        Json::Value mcBridge;
        {
            // request the bridge via RPC
            Json::Value jvParams;
            jvParams[jss::bridge_account] = mcDoor.human();
            jvParams[jss::bridge] = jvb;
            Json::Value const jrr = mcEnv.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(jrr.isMember(jss::node));
            auto r = jrr[jss::node];
            // std::cout << to_string(r) << '\n';

            BEAST_EXPECT(r.isMember(jss::Account));
            BEAST_EXPECT(r[jss::Account] == mcDoor.human());

            BEAST_EXPECT(r.isMember(jss::Flags));

            BEAST_EXPECT(r.isMember(sfLedgerEntryType.jsonName));
            BEAST_EXPECT(r[sfLedgerEntryType.jsonName] == jss::Bridge);

            // we not created an account yet
            BEAST_EXPECT(r.isMember(sfXChainAccountCreateCount.jsonName));
            BEAST_EXPECT(r[sfXChainAccountCreateCount.jsonName].asInt() == 0);

            // we have not claimed a locking chain tx yet
            BEAST_EXPECT(r.isMember(sfXChainAccountClaimCount.jsonName));
            BEAST_EXPECT(r[sfXChainAccountClaimCount.jsonName].asInt() == 0);

            BEAST_EXPECT(r.isMember(jss::index));
            bridge_index = r[jss::index].asString();
            mcBridge = r;
        }
        {
            // request the bridge via RPC by index
            Json::Value jvParams;
            jvParams[jss::index] = bridge_index;
            Json::Value const jrr = mcEnv.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(jrr.isMember(jss::node));
            BEAST_EXPECT(jrr[jss::node] == mcBridge);
        }
        {
            // swap door accounts and make sure we get an error value
            Json::Value jvParams;
            // Sidechain door account is "master", not scDoor
            jvParams[jss::bridge_account] = Account::master.human();
            jvParams[jss::bridge] = jvb;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = mcEnv.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            checkErrorValue(jrr, "entryNotFound", "");
        }
        {
            // create two claim ids and verify that the bridge counter was
            // incremented
            mcEnv(xchain_create_claim_id(mcAlice, jvb, reward, scAlice));
            mcEnv.close();
            mcEnv(xchain_create_claim_id(mcBob, jvb, reward, scBob));
            mcEnv.close();

            // request the bridge via RPC
            Json::Value jvParams;
            jvParams[jss::bridge_account] = mcDoor.human();
            jvParams[jss::bridge] = jvb;
            // std::cout << to_string(jvParams) << '\n';
            Json::Value const jrr = mcEnv.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(jrr.isMember(jss::node));
            auto r = jrr[jss::node];

            // we executed two create claim id txs
            BEAST_EXPECT(r.isMember(sfXChainClaimID.jsonName));
            BEAST_EXPECT(r[sfXChainClaimID.jsonName].asInt() == 2);
        }
    }

    void
    testLedgerEntryClaimID()
    {
        testcase("ledger_entry: xchain_claim_id");
        using namespace test::jtx;

        Env mcEnv{*this, features};
        Env scEnv(*this, envconfig(port_increment, 3), features);

        createBridgeObjects(mcEnv, scEnv);

        scEnv(xchain_create_claim_id(scAlice, jvb, reward, mcAlice));
        scEnv.close();
        scEnv(xchain_create_claim_id(scBob, jvb, reward, mcBob));
        scEnv.close();

        std::string bridge_index;
        {
            // request the xchain_claim_id via RPC
            Json::Value jvParams;
            jvParams[jss::xchain_owned_claim_id] = jvXRPBridgeRPC;
            jvParams[jss::xchain_owned_claim_id][jss::xchain_owned_claim_id] =
                1;
            // std::cout << to_string(jvParams) << '\n';
            Json::Value const jrr = scEnv.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(jrr.isMember(jss::node));
            auto r = jrr[jss::node];
            // std::cout << to_string(r) << '\n';

            BEAST_EXPECT(r.isMember(jss::Account));
            BEAST_EXPECT(r[jss::Account] == scAlice.human());
            BEAST_EXPECT(
                r[sfLedgerEntryType.jsonName] == jss::XChainOwnedClaimID);
            BEAST_EXPECT(r[sfXChainClaimID.jsonName].asInt() == 1);
            BEAST_EXPECT(r[sfOwnerNode.jsonName].asInt() == 0);
        }

        {
            // request the xchain_claim_id via RPC
            Json::Value jvParams;
            jvParams[jss::xchain_owned_claim_id] = jvXRPBridgeRPC;
            jvParams[jss::xchain_owned_claim_id][jss::xchain_owned_claim_id] =
                2;
            Json::Value const jrr = scEnv.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(jrr.isMember(jss::node));
            auto r = jrr[jss::node];
            // std::cout << to_string(r) << '\n';

            BEAST_EXPECT(r.isMember(jss::Account));
            BEAST_EXPECT(r[jss::Account] == scBob.human());
            BEAST_EXPECT(
                r[sfLedgerEntryType.jsonName] == jss::XChainOwnedClaimID);
            BEAST_EXPECT(r[sfXChainClaimID.jsonName].asInt() == 2);
            BEAST_EXPECT(r[sfOwnerNode.jsonName].asInt() == 0);
        }
    }

    void
    testLedgerEntryCreateAccountClaimID()
    {
        testcase("ledger_entry: xchain_create_account_claim_id");
        using namespace test::jtx;

        Env mcEnv{*this, features};
        Env scEnv(*this, envconfig(port_increment, 3), features);

        // note: signers.size() and quorum are both 5 in createBridgeObjects
        createBridgeObjects(mcEnv, scEnv);

        auto scCarol =
            Account("scCarol");  // Don't fund it - it will be created with the
                                 // xchain transaction
        auto const amt = XRP(1000);
        mcEnv(sidechain_xchain_account_create(
            mcAlice, jvb, scCarol, amt, reward));
        mcEnv.close();

        // send less than quorum of attestations (otherwise funds are
        // immediately transferred and no "claim" object is created)
        size_t constexpr num_attest = 3;
        auto attestations = create_account_attestations(
            scAttester,
            jvb,
            mcAlice,
            amt,
            reward,
            payee,
            /*wasLockingChainSend*/ true,
            1,
            scCarol,
            signers,
            UT_XCHAIN_DEFAULT_NUM_SIGNERS);
        for (size_t i = 0; i < num_attest; ++i)
        {
            scEnv(attestations[i]);
        }
        scEnv.close();

        {
            // request the create account claim_id via RPC
            Json::Value jvParams;
            jvParams[jss::xchain_owned_create_account_claim_id] =
                jvXRPBridgeRPC;
            jvParams[jss::xchain_owned_create_account_claim_id]
                    [jss::xchain_owned_create_account_claim_id] = 1;
            // std::cout << to_string(jvParams) << '\n';
            Json::Value const jrr = scEnv.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            // std::cout << to_string(jrr) << '\n';

            BEAST_EXPECT(jrr.isMember(jss::node));
            auto r = jrr[jss::node];

            BEAST_EXPECT(r.isMember(jss::Account));
            BEAST_EXPECT(r[jss::Account] == Account::master.human());

            BEAST_EXPECT(r.isMember(sfXChainAccountCreateCount.jsonName));
            BEAST_EXPECT(r[sfXChainAccountCreateCount.jsonName].asInt() == 1);

            BEAST_EXPECT(
                r.isMember(sfXChainCreateAccountAttestations.jsonName));
            auto attest = r[sfXChainCreateAccountAttestations.jsonName];
            BEAST_EXPECT(attest.isArray());
            BEAST_EXPECT(attest.size() == 3);
            BEAST_EXPECT(attest[Json::Value::UInt(0)].isMember(
                sfXChainCreateAccountProofSig.jsonName));
            Json::Value a[num_attest];
            for (size_t i = 0; i < num_attest; ++i)
            {
                a[i] = attest[Json::Value::UInt(0)]
                             [sfXChainCreateAccountProofSig.jsonName];
                BEAST_EXPECT(
                    a[i].isMember(jss::Amount) &&
                    a[i][jss::Amount].asInt() == 1000 * drop_per_xrp);
                BEAST_EXPECT(
                    a[i].isMember(jss::Destination) &&
                    a[i][jss::Destination] == scCarol.human());
                BEAST_EXPECT(
                    a[i].isMember(sfAttestationSignerAccount.jsonName) &&
                    std::any_of(
                        signers.begin(), signers.end(), [&](signer const& s) {
                            return a[i][sfAttestationSignerAccount.jsonName] ==
                                s.account.human();
                        }));
                BEAST_EXPECT(
                    a[i].isMember(sfAttestationRewardAccount.jsonName) &&
                    std::any_of(
                        payee.begin(),
                        payee.end(),
                        [&](Account const& account) {
                            return a[i][sfAttestationRewardAccount.jsonName] ==
                                account.human();
                        }));
                BEAST_EXPECT(
                    a[i].isMember(sfWasLockingChainSend.jsonName) &&
                    a[i][sfWasLockingChainSend.jsonName] == 1);
                BEAST_EXPECT(
                    a[i].isMember(sfSignatureReward.jsonName) &&
                    a[i][sfSignatureReward.jsonName].asInt() ==
                        1 * drop_per_xrp);
            }
        }

        // complete attestations quorum - CreateAccountClaimID should not be
        // present anymore
        for (size_t i = num_attest; i < UT_XCHAIN_DEFAULT_NUM_SIGNERS; ++i)
        {
            scEnv(attestations[i]);
        }
        scEnv.close();
        {
            // request the create account claim_id via RPC
            Json::Value jvParams;
            jvParams[jss::xchain_owned_create_account_claim_id] =
                jvXRPBridgeRPC;
            jvParams[jss::xchain_owned_create_account_claim_id]
                    [jss::xchain_owned_create_account_claim_id] = 1;
            // std::cout << to_string(jvParams) << '\n';
            Json::Value const jrr = scEnv.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "");
        }
    }

public:
    void
    run() override
    {
        testLedgerEntryBridge();
        testLedgerEntryClaimID();
        testLedgerEntryCreateAccountClaimID();
    }
};

class LedgerRPC_test : public beast::unit_test::suite
{
    void
    checkErrorValue(
        Json::Value const& jv,
        std::string const& err,
        std::string const& msg)
    {
        if (BEAST_EXPECT(jv.isMember(jss::status)))
            BEAST_EXPECT(jv[jss::status] == "error");
        if (BEAST_EXPECT(jv.isMember(jss::error)))
            BEAST_EXPECT(jv[jss::error] == err);
        if (msg.empty())
        {
            BEAST_EXPECT(
                jv[jss::error_message] == Json::nullValue ||
                jv[jss::error_message] == "");
        }
        else if (BEAST_EXPECT(jv.isMember(jss::error_message)))
            BEAST_EXPECT(jv[jss::error_message] == msg);
    }

    // Corrupt a valid address by replacing the 10th character with '!'.
    // '!' is not part of the ripple alphabet.
    std::string
    makeBadAddress(std::string good)
    {
        std::string ret = std::move(good);
        ret.replace(10, 1, 1, '!');
        return ret;
    }

    void
    testLedgerRequest()
    {
        testcase("Basic Request");
        using namespace test::jtx;

        Env env{*this};

        env.close();
        BEAST_EXPECT(env.current()->info().seq == 4);

        {
            Json::Value jvParams;
            // can be either numeric or quoted numeric
            jvParams[jss::ledger_index] = 1;
            auto const jrr =
                env.rpc("json", "ledger", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::ledger][jss::closed] == true);
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "1");
        }

        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "1";
            auto const jrr =
                env.rpc("json", "ledger", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::ledger][jss::closed] == true);
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "1");
        }

        {
            // using current identifier
            auto const jrr = env.rpc("ledger", "current")[jss::result];
            BEAST_EXPECT(jrr[jss::ledger][jss::closed] == false);
            BEAST_EXPECT(
                jrr[jss::ledger][jss::ledger_index] ==
                std::to_string(env.current()->info().seq));
            BEAST_EXPECT(
                jrr[jss::ledger_current_index] == env.current()->info().seq);
        }
    }

    void
    testBadInput()
    {
        testcase("Bad Input");
        using namespace test::jtx;
        Env env{*this};
        Account const gw{"gateway"};
        auto const USD = gw["USD"];
        Account const bob{"bob"};

        env.fund(XRP(10000), gw, bob);
        env.close();
        env.trust(USD(1000), bob);
        env.close();

        {
            // ask for an arbitrary string - index
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "potato";
            auto const jrr =
                env.rpc("json", "ledger", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "invalidParams", "ledgerIndexMalformed");
        }

        {
            // ask for a negative index
            Json::Value jvParams;
            jvParams[jss::ledger_index] = -1;
            auto const jrr =
                env.rpc("json", "ledger", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "invalidParams", "ledgerIndexMalformed");
        }

        {
            // ask for a bad ledger index
            Json::Value jvParams;
            jvParams[jss::ledger_index] = 10u;
            auto const jrr =
                env.rpc("json", "ledger", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "lgrNotFound", "ledgerNotFound");
        }

        {
            // unrecognized string arg -- error
            auto const jrr = env.rpc("ledger", "arbitrary_text")[jss::result];
            checkErrorValue(jrr, "lgrNotFound", "ledgerNotFound");
        }

        {
            // Request queue for closed ledger
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::queue] = true;
            auto const jrr =
                env.rpc("json", "ledger", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "invalidParams", "Invalid parameters.");
        }

        {
            // Request a ledger with a very large (double) sequence.
            auto const ret =
                env.rpc("json", "ledger", "{ \"ledger_index\" : 2e15 }");
            BEAST_EXPECT(RPC::contains_error(ret));
            BEAST_EXPECT(ret[jss::error_message] == "Invalid parameters.");
        }

        {
            // Request a ledger with very large (integer) sequence.
            auto const ret = env.rpc(
                "json", "ledger", "{ \"ledger_index\" : 1000000000000000 }");
            checkErrorValue(ret, "invalidParams", "Invalid parameters.");
        }
    }

    void
    testLedgerCurrent()
    {
        testcase("ledger_current Request");
        using namespace test::jtx;

        Env env{*this};

        env.close();
        BEAST_EXPECT(env.current()->info().seq == 4);

        {
            auto const jrr = env.rpc("ledger_current")[jss::result];
            BEAST_EXPECT(
                jrr[jss::ledger_current_index] == env.current()->info().seq);
        }
    }

    void
    testMissingLedgerEntryLedgerHash()
    {
        testcase("Missing ledger_entry ledger_hash");
        using namespace test::jtx;
        Env env{*this};
        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        Json::Value jvParams;
        jvParams[jss::account_root] = alice.human();
        jvParams[jss::ledger_hash] =
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
        auto const jrr =
            env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
        checkErrorValue(jrr, "lgrNotFound", "ledgerNotFound");
    }

    void
    testLedgerFull()
    {
        testcase("Ledger Request, Full Option");
        using namespace test::jtx;

        Env env{*this};

        env.close();

        Json::Value jvParams;
        jvParams[jss::ledger_index] = 3u;
        jvParams[jss::full] = true;
        auto const jrr =
            env.rpc("json", "ledger", to_string(jvParams))[jss::result];
        BEAST_EXPECT(jrr[jss::ledger].isMember(jss::accountState));
        BEAST_EXPECT(jrr[jss::ledger][jss::accountState].isArray());
        BEAST_EXPECT(jrr[jss::ledger][jss::accountState].size() == 3u);
    }

    void
    testLedgerFullNonAdmin()
    {
        testcase("Ledger Request, Full Option Without Admin");
        using namespace test::jtx;

        Env env{*this, envconfig(no_admin)};

        //        env.close();

        Json::Value jvParams;
        jvParams[jss::ledger_index] = 1u;
        jvParams[jss::full] = true;
        auto const jrr =
            env.rpc("json", "ledger", to_string(jvParams))[jss::result];
        checkErrorValue(
            jrr, "noPermission", "You don't have permission for this command.");
    }

    void
    testLedgerAccounts()
    {
        testcase("Ledger Request, Accounts Option");
        using namespace test::jtx;

        Env env{*this};

        env.close();

        Json::Value jvParams;
        jvParams[jss::ledger_index] = 3u;
        jvParams[jss::accounts] = true;
        auto const jrr =
            env.rpc("json", "ledger", to_string(jvParams))[jss::result];
        BEAST_EXPECT(jrr[jss::ledger].isMember(jss::accountState));
        BEAST_EXPECT(jrr[jss::ledger][jss::accountState].isArray());
        BEAST_EXPECT(jrr[jss::ledger][jss::accountState].size() == 3u);
    }

    void
    testLedgerEntryAccountRoot()
    {
        testcase("ledger_entry Request AccountRoot");
        using namespace test::jtx;
        Env env{*this};
        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};
        {
            // Exercise ledger_closed along the way.
            Json::Value const jrr = env.rpc("ledger_closed")[jss::result];
            BEAST_EXPECT(jrr[jss::ledger_hash] == ledgerHash);
            BEAST_EXPECT(jrr[jss::ledger_index] == 3);
        }

        std::string accountRootIndex;
        {
            // Request alice's account root.
            Json::Value jvParams;
            jvParams[jss::account_root] = alice.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::node));
            BEAST_EXPECT(jrr[jss::node][jss::Account] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfBalance.jsonName] == "10000000000");
            accountRootIndex = jrr[jss::index].asString();
        }
        {
            constexpr char alicesAcctRootBinary[]{
                "1100612200800000240000000425000000032D00000000559CE54C3B934E4"
                "73A995B477E92EC229F99CED5B62BF4D2ACE4DC42719103AE2F6240000002"
                "540BE4008114AE123A8556F3CF91154711376AFB0F894F832B3D"};

            // Request alice's account root, but with binary == true;
            Json::Value jvParams;
            jvParams[jss::account_root] = alice.human();
            jvParams[jss::binary] = 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::node_binary));
            BEAST_EXPECT(jrr[jss::node_binary] == alicesAcctRootBinary);
        }
        {
            // Request alice's account root using the index.
            Json::Value jvParams;
            jvParams[jss::index] = accountRootIndex;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(!jrr.isMember(jss::node_binary));
            BEAST_EXPECT(jrr.isMember(jss::node));
            BEAST_EXPECT(jrr[jss::node][jss::Account] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfBalance.jsonName] == "10000000000");
        }
        {
            // Request alice's account root by index, but with binary == false.
            Json::Value jvParams;
            jvParams[jss::index] = accountRootIndex;
            jvParams[jss::binary] = 0;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::node));
            BEAST_EXPECT(jrr[jss::node][jss::Account] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfBalance.jsonName] == "10000000000");
        }
        {
            // Request using a corrupted AccountID.
            Json::Value jvParams;
            jvParams[jss::account_root] = makeBadAddress(alice.human());
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedAddress", "");
        }
        {
            // Request an account that is not in the ledger.
            Json::Value jvParams;
            jvParams[jss::account_root] = Account("bob").human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "");
        }
    }

    void
    testLedgerEntryCheck()
    {
        testcase("ledger_entry Request Check");
        using namespace test::jtx;
        Env env{*this};
        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        auto const checkId = keylet::check(env.master, env.seq(env.master));

        env(check::create(env.master, alice, XRP(100)));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};
        {
            // Request a check.
            Json::Value jvParams;
            jvParams[jss::check] = to_string(checkId.key);
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][sfLedgerEntryType.jsonName] == jss::Check);
            BEAST_EXPECT(jrr[jss::node][sfSendMax.jsonName] == "100000000");
        }
        {
            // Request an index that is not a check.  We'll use alice's
            // account root index.
            std::string accountRootIndex;
            {
                Json::Value jvParams;
                jvParams[jss::account_root] = alice.human();
                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];
                accountRootIndex = jrr[jss::index].asString();
            }
            Json::Value jvParams;
            jvParams[jss::check] = accountRootIndex;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "unexpectedLedgerType", "");
        }
    }

    void
    testLedgerEntryDepositPreauth()
    {
        testcase("ledger_entry Deposit Preauth");

        using namespace test::jtx;

        Env env{*this};
        Account const alice{"alice"};
        Account const becky{"becky"};

        env.fund(XRP(10000), alice, becky);
        env.close();

        env(deposit::auth(alice, becky));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};
        std::string depositPreauthIndex;
        {
            // Request a depositPreauth by owner and authorized.
            Json::Value jvParams;
            jvParams[jss::deposit_preauth][jss::owner] = alice.human();
            jvParams[jss::deposit_preauth][jss::authorized] = becky.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(
                jrr[jss::node][sfLedgerEntryType.jsonName] ==
                jss::DepositPreauth);
            BEAST_EXPECT(jrr[jss::node][sfAccount.jsonName] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfAuthorize.jsonName] == becky.human());
            depositPreauthIndex = jrr[jss::node][jss::index].asString();
        }
        {
            // Request a depositPreauth by index.
            Json::Value jvParams;
            jvParams[jss::deposit_preauth] = depositPreauthIndex;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(
                jrr[jss::node][sfLedgerEntryType.jsonName] ==
                jss::DepositPreauth);
            BEAST_EXPECT(jrr[jss::node][sfAccount.jsonName] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfAuthorize.jsonName] == becky.human());
        }
        {
            // Malformed request: deposit_preauth neither object nor string.
            Json::Value jvParams;
            jvParams[jss::deposit_preauth] = -5;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Malformed request: deposit_preauth not hex string.
            Json::Value jvParams;
            jvParams[jss::deposit_preauth] = "0123456789ABCDEFG";
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Malformed request: missing [jss::deposit_preauth][jss::owner]
            Json::Value jvParams;
            jvParams[jss::deposit_preauth][jss::authorized] = becky.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Malformed request: [jss::deposit_preauth][jss::owner] not string.
            Json::Value jvParams;
            jvParams[jss::deposit_preauth][jss::owner] = 7;
            jvParams[jss::deposit_preauth][jss::authorized] = becky.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Malformed: missing [jss::deposit_preauth][jss::authorized]
            Json::Value jvParams;
            jvParams[jss::deposit_preauth][jss::owner] = alice.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Malformed: [jss::deposit_preauth][jss::authorized] not string.
            Json::Value jvParams;
            jvParams[jss::deposit_preauth][jss::owner] = alice.human();
            jvParams[jss::deposit_preauth][jss::authorized] = 47;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Malformed: [jss::deposit_preauth][jss::owner] is malformed.
            Json::Value jvParams;
            jvParams[jss::deposit_preauth][jss::owner] =
                "rP6P9ypfAmc!pw8SZHNwM4nvZHFXDraQas";

            jvParams[jss::deposit_preauth][jss::authorized] = becky.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedOwner", "");
        }
        {
            // Malformed: [jss::deposit_preauth][jss::authorized] is malformed.
            Json::Value jvParams;
            jvParams[jss::deposit_preauth][jss::owner] = alice.human();
            jvParams[jss::deposit_preauth][jss::authorized] =
                "rP6P9ypfAmc!pw8SZHNwM4nvZHFXDraQas";

            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedAuthorized", "");
        }
    }

    void
    testLedgerEntryDirectory()
    {
        testcase("ledger_entry Request Directory");
        using namespace test::jtx;
        Env env{*this};
        Account const alice{"alice"};
        Account const gw{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(10000), alice, gw);
        env.close();

        env.trust(USD(1000), alice);
        env.close();

        // Run up the number of directory entries so alice has two
        // directory nodes.
        for (int d = 1'000'032; d >= 1'000'000; --d)
        {
            env(offer(alice, USD(1), drops(d)));
        }
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};
        {
            // Exercise ledger_closed along the way.
            Json::Value const jrr = env.rpc("ledger_closed")[jss::result];
            BEAST_EXPECT(jrr[jss::ledger_hash] == ledgerHash);
            BEAST_EXPECT(jrr[jss::ledger_index] == 5);
        }

        std::string const dirRootIndex =
            "A33EC6BB85FB5674074C4A3A43373BB17645308F3EAE1933E3E35252162B217D";
        {
            // Locate directory by index.
            Json::Value jvParams;
            jvParams[jss::directory] = dirRootIndex;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][sfIndexes.jsonName].size() == 32);
        }
        {
            // Locate directory by directory root.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;
            jvParams[jss::directory][jss::dir_root] = dirRootIndex;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::index] == dirRootIndex);
        }
        {
            // Locate directory by owner.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;
            jvParams[jss::directory][jss::owner] = alice.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::index] == dirRootIndex);
        }
        {
            // Locate directory by directory root and sub_index.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;
            jvParams[jss::directory][jss::dir_root] = dirRootIndex;
            jvParams[jss::directory][jss::sub_index] = 1;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::index] != dirRootIndex);
            BEAST_EXPECT(jrr[jss::node][sfIndexes.jsonName].size() == 2);
        }
        {
            // Locate directory by owner and sub_index.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;
            jvParams[jss::directory][jss::owner] = alice.human();
            jvParams[jss::directory][jss::sub_index] = 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::index] != dirRootIndex);
            BEAST_EXPECT(jrr[jss::node][sfIndexes.jsonName].size() == 2);
        }
        {
            // Null directory argument.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::nullValue;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Non-integer sub_index.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;
            jvParams[jss::directory][jss::dir_root] = dirRootIndex;
            jvParams[jss::directory][jss::sub_index] = 1.5;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Malformed owner entry.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;

            std::string const badAddress = makeBadAddress(alice.human());
            jvParams[jss::directory][jss::owner] = badAddress;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedAddress", "");
        }
        {
            // Malformed directory object.  Specify both dir_root and owner.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;
            jvParams[jss::directory][jss::owner] = alice.human();
            jvParams[jss::directory][jss::dir_root] = dirRootIndex;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Incomplete directory object.  Missing both dir_root and owner.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;
            jvParams[jss::directory][jss::sub_index] = 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
    }

    void
    testLedgerEntryEscrow()
    {
        testcase("ledger_entry Request Escrow");
        using namespace test::jtx;
        Env env{*this};
        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        // Lambda to create an escrow.
        auto escrowCreate = [](test::jtx::Account const& account,
                               test::jtx::Account const& to,
                               STAmount const& amount,
                               NetClock::time_point const& cancelAfter) {
            Json::Value jv;
            jv[jss::TransactionType] = jss::EscrowCreate;
            jv[jss::Flags] = tfUniversal;
            jv[jss::Account] = account.human();
            jv[jss::Destination] = to.human();
            jv[jss::Amount] = amount.getJson(JsonOptions::none);
            jv[sfFinishAfter.jsonName] =
                cancelAfter.time_since_epoch().count() + 2;
            return jv;
        };

        using namespace std::chrono_literals;
        env(escrowCreate(alice, alice, XRP(333), env.now() + 2s));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};
        std::string escrowIndex;
        {
            // Request the escrow using owner and sequence.
            Json::Value jvParams;
            jvParams[jss::escrow] = Json::objectValue;
            jvParams[jss::escrow][jss::owner] = alice.human();
            jvParams[jss::escrow][jss::seq] = env.seq(alice) - 1;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][jss::Amount] == XRP(333).value().getText());
            escrowIndex = jrr[jss::index].asString();
        }
        {
            // Request the escrow by index.
            Json::Value jvParams;
            jvParams[jss::escrow] = escrowIndex;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][jss::Amount] == XRP(333).value().getText());
        }
        {
            // Malformed owner entry.
            Json::Value jvParams;
            jvParams[jss::escrow] = Json::objectValue;

            std::string const badAddress = makeBadAddress(alice.human());
            jvParams[jss::escrow][jss::owner] = badAddress;
            jvParams[jss::escrow][jss::seq] = env.seq(alice) - 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedOwner", "");
        }
        {
            // Missing owner.
            Json::Value jvParams;
            jvParams[jss::escrow] = Json::objectValue;
            jvParams[jss::escrow][jss::seq] = env.seq(alice) - 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Missing sequence.
            Json::Value jvParams;
            jvParams[jss::escrow] = Json::objectValue;
            jvParams[jss::escrow][jss::owner] = alice.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Non-integer sequence.
            Json::Value jvParams;
            jvParams[jss::escrow] = Json::objectValue;
            jvParams[jss::escrow][jss::owner] = alice.human();
            jvParams[jss::escrow][jss::seq] =
                std::to_string(env.seq(alice) - 1);
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
    }

    void
    testLedgerEntryOffer()
    {
        testcase("ledger_entry Request Offer");
        using namespace test::jtx;
        Env env{*this};
        Account const alice{"alice"};
        Account const gw{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(10000), alice, gw);
        env.close();

        env(offer(alice, USD(321), XRP(322)));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};
        std::string offerIndex;
        {
            // Request the offer using owner and sequence.
            Json::Value jvParams;
            jvParams[jss::offer] = Json::objectValue;
            jvParams[jss::offer][jss::account] = alice.human();
            jvParams[jss::offer][jss::seq] = env.seq(alice) - 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][jss::TakerGets] == "322000000");
            offerIndex = jrr[jss::index].asString();
        }
        {
            // Request the offer using its index.
            Json::Value jvParams;
            jvParams[jss::offer] = offerIndex;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][jss::TakerGets] == "322000000");
        }
        {
            // Malformed account entry.
            Json::Value jvParams;
            jvParams[jss::offer] = Json::objectValue;

            std::string const badAddress = makeBadAddress(alice.human());
            jvParams[jss::offer][jss::account] = badAddress;
            jvParams[jss::offer][jss::seq] = env.seq(alice) - 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedAddress", "");
        }
        {
            // Malformed offer object.  Missing account member.
            Json::Value jvParams;
            jvParams[jss::offer] = Json::objectValue;
            jvParams[jss::offer][jss::seq] = env.seq(alice) - 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Malformed offer object.  Missing seq member.
            Json::Value jvParams;
            jvParams[jss::offer] = Json::objectValue;
            jvParams[jss::offer][jss::account] = alice.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Malformed offer object.  Non-integral seq member.
            Json::Value jvParams;
            jvParams[jss::offer] = Json::objectValue;
            jvParams[jss::offer][jss::account] = alice.human();
            jvParams[jss::offer][jss::seq] = std::to_string(env.seq(alice) - 1);
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
    }

    void
    testLedgerEntryPayChan()
    {
        testcase("ledger_entry Request Pay Chan");
        using namespace test::jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this};
        Account const alice{"alice"};

        env.fund(XRP(10000), alice);
        env.close();

        // Lambda to create a PayChan.
        auto payChanCreate = [](test::jtx::Account const& account,
                                test::jtx::Account const& to,
                                STAmount const& amount,
                                NetClock::duration const& settleDelay,
                                PublicKey const& pk) {
            Json::Value jv;
            jv[jss::TransactionType] = jss::PaymentChannelCreate;
            jv[jss::Account] = account.human();
            jv[jss::Destination] = to.human();
            jv[jss::Amount] = amount.getJson(JsonOptions::none);
            jv[sfSettleDelay.jsonName] = settleDelay.count();
            jv[sfPublicKey.jsonName] = strHex(pk.slice());
            return jv;
        };

        env(payChanCreate(alice, env.master, XRP(57), 18s, alice.pk()));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};

        uint256 const payChanIndex{
            keylet::payChan(alice, env.master, env.seq(alice) - 1).key};
        {
            // Request the payment channel using its index.
            Json::Value jvParams;
            jvParams[jss::payment_channel] = to_string(payChanIndex);
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][sfAmount.jsonName] == "57000000");
            BEAST_EXPECT(jrr[jss::node][sfBalance.jsonName] == "0");
            BEAST_EXPECT(jrr[jss::node][sfSettleDelay.jsonName] == 18);
        }
        {
            // Request an index that is not a payment channel.
            Json::Value jvParams;
            jvParams[jss::payment_channel] = ledgerHash;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "");
        }
    }

    void
    testLedgerEntryRippleState()
    {
        testcase("ledger_entry Request RippleState");
        using namespace test::jtx;
        Env env{*this};
        Account const alice{"alice"};
        Account const gw{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(10000), alice, gw);
        env.close();

        env.trust(USD(999), alice);
        env.close();

        env(pay(gw, alice, USD(97)));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};
        {
            // Request the trust line using the accounts and currency.
            Json::Value jvParams;
            jvParams[jss::ripple_state] = Json::objectValue;
            jvParams[jss::ripple_state][jss::accounts] = Json::arrayValue;
            jvParams[jss::ripple_state][jss::accounts][0u] = alice.human();
            jvParams[jss::ripple_state][jss::accounts][1u] = gw.human();
            jvParams[jss::ripple_state][jss::currency] = "USD";
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][sfBalance.jsonName][jss::value] == "-97");
            BEAST_EXPECT(
                jrr[jss::node][sfHighLimit.jsonName][jss::value] == "999");
        }
        {
            // ripple_state is not an object.
            Json::Value jvParams;
            jvParams[jss::ripple_state] = "ripple_state";
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // ripple_state.currency is missing.
            Json::Value jvParams;
            jvParams[jss::ripple_state] = Json::objectValue;
            jvParams[jss::ripple_state][jss::accounts] = Json::arrayValue;
            jvParams[jss::ripple_state][jss::accounts][0u] = alice.human();
            jvParams[jss::ripple_state][jss::accounts][1u] = gw.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // ripple_state accounts is not an array.
            Json::Value jvParams;
            jvParams[jss::ripple_state] = Json::objectValue;
            jvParams[jss::ripple_state][jss::accounts] = 2;
            jvParams[jss::ripple_state][jss::currency] = "USD";
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // ripple_state one of the accounts is missing.
            Json::Value jvParams;
            jvParams[jss::ripple_state] = Json::objectValue;
            jvParams[jss::ripple_state][jss::accounts] = Json::arrayValue;
            jvParams[jss::ripple_state][jss::accounts][0u] = alice.human();
            jvParams[jss::ripple_state][jss::currency] = "USD";
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // ripple_state more than 2 accounts.
            Json::Value jvParams;
            jvParams[jss::ripple_state] = Json::objectValue;
            jvParams[jss::ripple_state][jss::accounts] = Json::arrayValue;
            jvParams[jss::ripple_state][jss::accounts][0u] = alice.human();
            jvParams[jss::ripple_state][jss::accounts][1u] = gw.human();
            jvParams[jss::ripple_state][jss::accounts][2u] = alice.human();
            jvParams[jss::ripple_state][jss::currency] = "USD";
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // ripple_state account[0] is not a string.
            Json::Value jvParams;
            jvParams[jss::ripple_state] = Json::objectValue;
            jvParams[jss::ripple_state][jss::accounts] = Json::arrayValue;
            jvParams[jss::ripple_state][jss::accounts][0u] = 44;
            jvParams[jss::ripple_state][jss::accounts][1u] = gw.human();
            jvParams[jss::ripple_state][jss::currency] = "USD";
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // ripple_state account[1] is not a string.
            Json::Value jvParams;
            jvParams[jss::ripple_state] = Json::objectValue;
            jvParams[jss::ripple_state][jss::accounts] = Json::arrayValue;
            jvParams[jss::ripple_state][jss::accounts][0u] = alice.human();
            jvParams[jss::ripple_state][jss::accounts][1u] = 21;
            jvParams[jss::ripple_state][jss::currency] = "USD";
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // ripple_state account[0] == account[1].
            Json::Value jvParams;
            jvParams[jss::ripple_state] = Json::objectValue;
            jvParams[jss::ripple_state][jss::accounts] = Json::arrayValue;
            jvParams[jss::ripple_state][jss::accounts][0u] = alice.human();
            jvParams[jss::ripple_state][jss::accounts][1u] = alice.human();
            jvParams[jss::ripple_state][jss::currency] = "USD";
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // ripple_state malformed account[0].
            Json::Value jvParams;
            jvParams[jss::ripple_state] = Json::objectValue;
            jvParams[jss::ripple_state][jss::accounts] = Json::arrayValue;
            jvParams[jss::ripple_state][jss::accounts][0u] =
                makeBadAddress(alice.human());
            jvParams[jss::ripple_state][jss::accounts][1u] = gw.human();
            jvParams[jss::ripple_state][jss::currency] = "USD";
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedAddress", "");
        }
        {
            // ripple_state malformed account[1].
            Json::Value jvParams;
            jvParams[jss::ripple_state] = Json::objectValue;
            jvParams[jss::ripple_state][jss::accounts] = Json::arrayValue;
            jvParams[jss::ripple_state][jss::accounts][0u] = alice.human();
            jvParams[jss::ripple_state][jss::accounts][1u] =
                makeBadAddress(gw.human());
            jvParams[jss::ripple_state][jss::currency] = "USD";
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedAddress", "");
        }
        {
            // ripple_state malformed currency.
            Json::Value jvParams;
            jvParams[jss::ripple_state] = Json::objectValue;
            jvParams[jss::ripple_state][jss::accounts] = Json::arrayValue;
            jvParams[jss::ripple_state][jss::accounts][0u] = alice.human();
            jvParams[jss::ripple_state][jss::accounts][1u] = gw.human();
            jvParams[jss::ripple_state][jss::currency] = "USDollars";
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedCurrency", "");
        }
    }

    void
    testLedgerEntryTicket()
    {
        testcase("ledger_entry Request Ticket");
        using namespace test::jtx;
        Env env{*this};
        env.close();

        // Create two tickets.
        std::uint32_t const tkt1{env.seq(env.master) + 1};
        env(ticket::create(env.master, 2));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};
        // Request four tickets: one before the first one we created, the
        // two created tickets, and the ticket that would come after the
        // last created ticket.
        {
            // Not a valid ticket requested by index.
            Json::Value jvParams;
            jvParams[jss::ticket] =
                to_string(getTicketIndex(env.master, tkt1 - 1));
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "");
        }
        {
            // First real ticket requested by index.
            Json::Value jvParams;
            jvParams[jss::ticket] = to_string(getTicketIndex(env.master, tkt1));
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][sfLedgerEntryType.jsonName] == jss::Ticket);
            BEAST_EXPECT(jrr[jss::node][sfTicketSequence.jsonName] == tkt1);
        }
        {
            // Second real ticket requested by account and sequence.
            Json::Value jvParams;
            jvParams[jss::ticket] = Json::objectValue;
            jvParams[jss::ticket][jss::account] = env.master.human();
            jvParams[jss::ticket][jss::ticket_seq] = tkt1 + 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][jss::index] ==
                to_string(getTicketIndex(env.master, tkt1 + 1)));
        }
        {
            // Not a valid ticket requested by account and sequence.
            Json::Value jvParams;
            jvParams[jss::ticket] = Json::objectValue;
            jvParams[jss::ticket][jss::account] = env.master.human();
            jvParams[jss::ticket][jss::ticket_seq] = tkt1 + 2;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "");
        }
        {
            // Request a ticket using an account root entry.
            Json::Value jvParams;
            jvParams[jss::ticket] = to_string(keylet::account(env.master).key);
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "unexpectedLedgerType", "");
        }
        {
            // Malformed account entry.
            Json::Value jvParams;
            jvParams[jss::ticket] = Json::objectValue;

            std::string const badAddress = makeBadAddress(env.master.human());
            jvParams[jss::ticket][jss::account] = badAddress;
            jvParams[jss::ticket][jss::ticket_seq] = env.seq(env.master) - 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedAddress", "");
        }
        {
            // Malformed ticket object.  Missing account member.
            Json::Value jvParams;
            jvParams[jss::ticket] = Json::objectValue;
            jvParams[jss::ticket][jss::ticket_seq] = env.seq(env.master) - 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Malformed ticket object.  Missing seq member.
            Json::Value jvParams;
            jvParams[jss::ticket] = Json::objectValue;
            jvParams[jss::ticket][jss::account] = env.master.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
        {
            // Malformed ticket object.  Non-integral seq member.
            Json::Value jvParams;
            jvParams[jss::ticket] = Json::objectValue;
            jvParams[jss::ticket][jss::account] = env.master.human();
            jvParams[jss::ticket][jss::ticket_seq] =
                std::to_string(env.seq(env.master) - 1);
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "malformedRequest", "");
        }
    }

    void
    testLedgerEntryDID()
    {
        testcase("ledger_entry Request DID");
        using namespace test::jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this};
        Account const alice{"alice"};

        env.fund(XRP(10000), alice);
        env.close();

        // Lambda to create a DID.
        auto didCreate = [](test::jtx::Account const& account) {
            Json::Value jv;
            jv[jss::TransactionType] = jss::DIDSet;
            jv[jss::Account] = account.human();
            jv[sfDIDDocument.jsonName] = strHex(std::string{"data"});
            jv[sfURI.jsonName] = strHex(std::string{"uri"});
            return jv;
        };

        env(didCreate(alice));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};

        {
            // Request the DID using its index.
            Json::Value jvParams;
            jvParams[jss::did] = alice.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][sfDIDDocument.jsonName] ==
                strHex(std::string{"data"}));
            BEAST_EXPECT(
                jrr[jss::node][sfURI.jsonName] == strHex(std::string{"uri"}));
        }
        {
            // Request an index that is not a DID.
            Json::Value jvParams;
            jvParams[jss::did] = env.master.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "");
        }
    }

    void
    testLedgerEntryInvalidParams(unsigned int apiVersion)
    {
        testcase(
            "ledger_entry Request With Invalid Parameters v" +
            std::to_string(apiVersion));
        using namespace test::jtx;
        Env env{*this};

        std::string const ledgerHash{to_string(env.closed()->info().hash)};

        auto makeParams = [&apiVersion](std::function<void(Json::Value&)> f) {
            Json::Value params;
            params[jss::api_version] = apiVersion;
            f(params);
            return params;
        };
        // "features" is not an option supported by ledger_entry.
        {
            auto const jvParams =
                makeParams([&ledgerHash](Json::Value& jvParams) {
                    jvParams[jss::features] = ledgerHash;
                    jvParams[jss::ledger_hash] = ledgerHash;
                });
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            if (apiVersion < 2u)
                checkErrorValue(jrr, "unknownOption", "");
            else
                checkErrorValue(jrr, "invalidParams", "");
        }
        Json::Value const injectObject = []() {
            Json::Value obj(Json::objectValue);
            obj[jss::account] = "rhigTLJJyXXSRUyRCQtqi1NoAZZzZnS4KU";
            obj[jss::ledger_index] = "validated";
            return obj;
        }();
        Json::Value const injectArray = []() {
            Json::Value arr(Json::arrayValue);
            arr[0u] = "rhigTLJJyXXSRUyRCQtqi1NoAZZzZnS4KU";
            arr[1u] = "validated";
            return arr;
        }();

        // invalid input for fields that can handle an object, but can't handle
        // an array
        for (auto const& field :
             {jss::directory, jss::escrow, jss::offer, jss::ticket, jss::amm})
        {
            auto const jvParams =
                makeParams([&field, &injectArray](Json::Value& jvParams) {
                    jvParams[field] = injectArray;
                });

            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            if (apiVersion < 2u)
                checkErrorValue(jrr, "internal", "Internal error.");
            else
                checkErrorValue(jrr, "invalidParams", "");
        }
        // Fields that can handle objects just fine
        for (auto const& field :
             {jss::directory, jss::escrow, jss::offer, jss::ticket, jss::amm})
        {
            auto const jvParams =
                makeParams([&field, &injectObject](Json::Value& jvParams) {
                    jvParams[field] = injectObject;
                });

            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            checkErrorValue(jrr, "malformedRequest", "");
        }

        for (auto const& inject : {injectObject, injectArray})
        {
            // invalid input for fields that can't handle an object or an array
            for (auto const& field :
                 {jss::index,
                  jss::account_root,
                  jss::check,
                  jss::payment_channel})
            {
                auto const jvParams =
                    makeParams([&field, &inject](Json::Value& jvParams) {
                        jvParams[field] = inject;
                    });

                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];

                if (apiVersion < 2u)
                    checkErrorValue(jrr, "internal", "Internal error.");
                else
                    checkErrorValue(jrr, "invalidParams", "");
            }
            // directory sub-fields
            for (auto const& field : {jss::dir_root, jss::owner})
            {
                auto const jvParams =
                    makeParams([&field, &inject](Json::Value& jvParams) {
                        jvParams[jss::directory][field] = inject;
                    });

                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];

                if (apiVersion < 2u)
                    checkErrorValue(jrr, "internal", "Internal error.");
                else
                    checkErrorValue(jrr, "invalidParams", "");
            }
            // escrow sub-fields
            {
                auto const jvParams =
                    makeParams([&inject](Json::Value& jvParams) {
                        jvParams[jss::escrow][jss::owner] = inject;
                        jvParams[jss::escrow][jss::seq] = 99;
                    });

                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];

                if (apiVersion < 2u)
                    checkErrorValue(jrr, "internal", "Internal error.");
                else
                    checkErrorValue(jrr, "invalidParams", "");
            }
            // offer sub-fields
            {
                auto const jvParams =
                    makeParams([&inject](Json::Value& jvParams) {
                        jvParams[jss::offer][jss::account] = inject;
                        jvParams[jss::offer][jss::seq] = 99;
                    });

                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];

                if (apiVersion < 2u)
                    checkErrorValue(jrr, "internal", "Internal error.");
                else
                    checkErrorValue(jrr, "invalidParams", "");
            }
            // ripple_state sub-fields
            {
                auto const jvParams =
                    makeParams([&inject](Json::Value& jvParams) {
                        Json::Value rs(Json::objectValue);
                        rs[jss::currency] = "FOO";
                        rs[jss::accounts] = Json::Value(Json::arrayValue);
                        rs[jss::accounts][0u] =
                            "rhigTLJJyXXSRUyRCQtqi1NoAZZzZnS4KU";
                        rs[jss::accounts][1u] =
                            "rKssEq6pg1KbqEqAFnua5mFAL6Ggpsh2wv";
                        rs[jss::currency] = inject;
                        jvParams[jss::ripple_state] = std::move(rs);
                    });

                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];

                if (apiVersion < 2u)
                    checkErrorValue(jrr, "internal", "Internal error.");
                else
                    checkErrorValue(jrr, "invalidParams", "");
            }
            // ticket sub-fields
            {
                auto const jvParams =
                    makeParams([&inject](Json::Value& jvParams) {
                        jvParams[jss::ticket][jss::account] = inject;
                        jvParams[jss::ticket][jss::ticket_seq] = 99;
                    });

                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];

                if (apiVersion < 2u)
                    checkErrorValue(jrr, "internal", "Internal error.");
                else
                    checkErrorValue(jrr, "invalidParams", "");
            }

            // Fields that can handle malformed inputs just fine
            for (auto const& field : {jss::nft_page, jss::deposit_preauth})
            {
                auto const jvParams =
                    makeParams([&field, &inject](Json::Value& jvParams) {
                        jvParams[field] = inject;
                    });

                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];

                checkErrorValue(jrr, "malformedRequest", "");
            }
            // Subfields of deposit_preauth that can handle malformed inputs
            // fine
            for (auto const& field : {jss::owner, jss::authorized})
            {
                auto const jvParams =
                    makeParams([&field, &inject](Json::Value& jvParams) {
                        auto pa = Json::Value(Json::objectValue);
                        pa[jss::owner] = "rhigTLJJyXXSRUyRCQtqi1NoAZZzZnS4KU";
                        pa[jss::authorized] =
                            "rKssEq6pg1KbqEqAFnua5mFAL6Ggpsh2wv";
                        pa[field] = inject;
                        jvParams[jss::deposit_preauth] = std::move(pa);
                    });

                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];

                checkErrorValue(jrr, "malformedRequest", "");
            }
        }
    }

    /// @brief ledger RPC requests as a way to drive
    /// input options to lookupLedger. The point of this test is
    /// coverage for lookupLedger, not so much the ledger
    /// RPC request.
    void
    testLookupLedger()
    {
        testcase("Lookup ledger");
        using namespace test::jtx;
        Env env{*this, FeatureBitset{}};  // hashes requested below assume
                                          // no amendments
        env.fund(XRP(10000), "alice");
        env.close();
        env.fund(XRP(10000), "bob");
        env.close();
        env.fund(XRP(10000), "jim");
        env.close();
        env.fund(XRP(10000), "jill");

        {
            // access via the legacy ledger field, keyword index values
            Json::Value jvParams;
            jvParams[jss::ledger] = "closed";
            auto jrr = env.rpc(
                "json",
                "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr.isMember(jss::ledger_hash));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "5");

            jvParams[jss::ledger] = "validated";
            jrr = env.rpc(
                "json",
                "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr.isMember(jss::ledger_hash));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "5");

            jvParams[jss::ledger] = "current";
            jrr = env.rpc(
                "json",
                "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "6");

            // ask for a bad ledger keyword
            jvParams[jss::ledger] = "invalid";
            jrr = env.rpc(
                "json",
                "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerIndexMalformed");

            // numeric index
            jvParams[jss::ledger] = 4;
            jrr = env.rpc(
                "json",
                "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr.isMember(jss::ledger_hash));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "4");

            // numeric index - out of range
            jvParams[jss::ledger] = 20;
            jrr = env.rpc(
                "json",
                "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "lgrNotFound");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerNotFound");
        }

        {
            std::string const hash3{
                "E86DE7F3D7A4D9CE17EF7C8BA08A8F4D"
                "8F643B9552F0D895A31CDA78F541DE4E"};
            // access via the ledger_hash field
            Json::Value jvParams;
            jvParams[jss::ledger_hash] = hash3;
            auto jrr = env.rpc(
                "json",
                "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr.isMember(jss::ledger_hash));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "3");

            // extra leading hex chars in hash are not allowed
            jvParams[jss::ledger_hash] = "DEADBEEF" + hash3;
            jrr = env.rpc(
                "json",
                "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerHashMalformed");

            // request with non-string ledger_hash
            jvParams[jss::ledger_hash] = 2;
            jrr = env.rpc(
                "json",
                "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerHashNotString");

            // malformed (non hex) hash
            jvParams[jss::ledger_hash] =
                "2E81FC6EC0DD943197EGC7E3FBE9AE30"
                "7F2775F2F7485BB37307984C3C0F2340";
            jrr = env.rpc(
                "json",
                "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerHashMalformed");

            // properly formed, but just doesn't exist
            jvParams[jss::ledger_hash] =
                "8C3EEDB3124D92E49E75D81A8826A2E6"
                "5A75FD71FC3FD6F36FEB803C5F1D812D";
            jrr = env.rpc(
                "json",
                "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "lgrNotFound");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerNotFound");
        }

        {
            // access via the ledger_index field, keyword index values
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "closed";
            auto jrr = env.rpc(
                "json",
                "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr.isMember(jss::ledger_hash));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "5");
            BEAST_EXPECT(jrr.isMember(jss::ledger_index));

            jvParams[jss::ledger_index] = "validated";
            jrr = env.rpc(
                "json",
                "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr.isMember(jss::ledger_hash));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "5");

            jvParams[jss::ledger_index] = "current";
            jrr = env.rpc(
                "json",
                "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "6");
            BEAST_EXPECT(jrr.isMember(jss::ledger_current_index));

            // ask for a bad ledger keyword
            jvParams[jss::ledger_index] = "invalid";
            jrr = env.rpc(
                "json",
                "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerIndexMalformed");

            // numeric index
            for (auto i : {1, 2, 3, 4, 5, 6})
            {
                jvParams[jss::ledger_index] = i;
                jrr = env.rpc(
                    "json",
                    "ledger",
                    boost::lexical_cast<std::string>(jvParams))[jss::result];
                BEAST_EXPECT(jrr.isMember(jss::ledger));
                if (i < 6)
                    BEAST_EXPECT(jrr.isMember(jss::ledger_hash));
                BEAST_EXPECT(
                    jrr[jss::ledger][jss::ledger_index] == std::to_string(i));
            }

            // numeric index - out of range
            jvParams[jss::ledger_index] = 7;
            jrr = env.rpc(
                "json",
                "ledger",
                boost::lexical_cast<std::string>(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "lgrNotFound");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerNotFound");
        }
    }

    void
    testNoQueue()
    {
        testcase("Ledger with queueing disabled");
        using namespace test::jtx;
        Env env{*this};

        Json::Value jv;
        jv[jss::ledger_index] = "current";
        jv[jss::queue] = true;
        jv[jss::expand] = true;

        auto jrr = env.rpc("json", "ledger", to_string(jv))[jss::result];
        BEAST_EXPECT(!jrr.isMember(jss::queue_data));
    }

    void
    testQueue()
    {
        testcase("Ledger with Queued Transactions");
        using namespace test::jtx;
        Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
                    auto& section = cfg->section("transaction_queue");
                    section.set("minimum_txn_in_ledger_standalone", "3");
                    section.set("normal_consensus_increase_percent", "0");
                    return cfg;
                })};

        Json::Value jv;
        jv[jss::ledger_index] = "current";
        jv[jss::queue] = true;
        jv[jss::expand] = true;

        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const charlie{"charlie"};
        Account const daria{"daria"};
        env.fund(XRP(10000), alice);
        env.fund(XRP(10000), bob);
        env.close();
        env.fund(XRP(10000), charlie);
        env.fund(XRP(10000), daria);
        env.close();

        auto jrr = env.rpc("json", "ledger", to_string(jv))[jss::result];
        BEAST_EXPECT(!jrr.isMember(jss::queue_data));

        // Fill the open ledger
        for (;;)
        {
            auto metrics = env.app().getTxQ().getMetrics(*env.current());
            if (metrics.openLedgerFeeLevel > metrics.minProcessingFeeLevel)
                break;
            env(noop(alice));
        }

        BEAST_EXPECT(env.current()->info().seq == 5);
        // Put some txs in the queue
        // Alice
        auto aliceSeq = env.seq(alice);
        env(pay(alice, "george", XRP(1000)),
            json(R"({"LastLedgerSequence":7})"),
            ter(terQUEUED));
        env(offer(alice, XRP(50000), alice["USD"](5000)),
            seq(aliceSeq + 1),
            ter(terQUEUED));
        env(noop(alice), seq(aliceSeq + 2), ter(terQUEUED));
        // Bob
        auto batch = [&env](Account a) {
            auto aSeq = env.seq(a);
            // Enough fee to get in front of alice in the queue
            for (int i = 0; i < 10; ++i)
            {
                env(noop(a), fee(1000 + i), seq(aSeq + i), ter(terQUEUED));
            }
        };
        batch(bob);
        // Charlie
        batch(charlie);
        // Daria
        batch(daria);

        jrr = env.rpc("json", "ledger", to_string(jv))[jss::result];
        BEAST_EXPECT(jrr[jss::queue_data].size() == 33);

        // Close enough ledgers so that alice's first tx expires.
        env.close();
        env.close();
        env.close();
        BEAST_EXPECT(env.current()->info().seq == 8);

        jrr = env.rpc("json", "ledger", to_string(jv))[jss::result];
        BEAST_EXPECT(jrr[jss::queue_data].size() == 11);

        env.close();

        jrr = env.rpc("json", "ledger", to_string(jv))[jss::result];
        const std::string txid0 = [&]() {
            auto const& parentHash = env.current()->info().parentHash;
            if (BEAST_EXPECT(jrr[jss::queue_data].size() == 2))
            {
                const std::string txid1 = [&]() {
                    auto const& txj = jrr[jss::queue_data][1u];
                    BEAST_EXPECT(txj[jss::account] == alice.human());
                    BEAST_EXPECT(txj[jss::fee_level] == "256");
                    BEAST_EXPECT(txj["preflight_result"] == "tesSUCCESS");
                    BEAST_EXPECT(txj["retries_remaining"] == 10);
                    BEAST_EXPECT(txj.isMember(jss::tx));
                    auto const& tx = txj[jss::tx];
                    BEAST_EXPECT(tx[jss::Account] == alice.human());
                    BEAST_EXPECT(tx[jss::TransactionType] == jss::AccountSet);
                    return tx[jss::hash].asString();
                }();

                auto const& txj = jrr[jss::queue_data][0u];
                BEAST_EXPECT(txj[jss::account] == alice.human());
                BEAST_EXPECT(txj[jss::fee_level] == "256");
                BEAST_EXPECT(txj["preflight_result"] == "tesSUCCESS");
                BEAST_EXPECT(txj["retries_remaining"] == 10);
                BEAST_EXPECT(txj.isMember(jss::tx));
                auto const& tx = txj[jss::tx];
                BEAST_EXPECT(tx[jss::Account] == alice.human());
                BEAST_EXPECT(tx[jss::TransactionType] == jss::OfferCreate);
                const auto txid0 = tx[jss::hash].asString();
                uint256 tx0, tx1;
                BEAST_EXPECT(tx0.parseHex(txid0));
                BEAST_EXPECT(tx1.parseHex(txid1));
                BEAST_EXPECT((tx0 ^ parentHash) < (tx1 ^ parentHash));
                return txid0;
            }
            return std::string{};
        }();

        env.close();

        jv[jss::expand] = false;

        jrr = env.rpc("json", "ledger", to_string(jv))[jss::result];
        if (BEAST_EXPECT(jrr[jss::queue_data].size() == 2))
        {
            auto const& parentHash = env.current()->info().parentHash;
            auto const txid1 = [&]() {
                auto const& txj = jrr[jss::queue_data][1u];
                BEAST_EXPECT(txj[jss::account] == alice.human());
                BEAST_EXPECT(txj[jss::fee_level] == "256");
                BEAST_EXPECT(txj["preflight_result"] == "tesSUCCESS");
                BEAST_EXPECT(txj.isMember(jss::tx));
                return txj[jss::tx].asString();
            }();
            auto const& txj = jrr[jss::queue_data][0u];
            BEAST_EXPECT(txj[jss::account] == alice.human());
            BEAST_EXPECT(txj[jss::fee_level] == "256");
            BEAST_EXPECT(txj["preflight_result"] == "tesSUCCESS");
            BEAST_EXPECT(txj["retries_remaining"] == 9);
            BEAST_EXPECT(txj["last_result"] == "terPRE_SEQ");
            BEAST_EXPECT(txj.isMember(jss::tx));
            BEAST_EXPECT(txj[jss::tx] == txid0);
            uint256 tx0, tx1;
            BEAST_EXPECT(tx0.parseHex(txid0));
            BEAST_EXPECT(tx1.parseHex(txid1));
            BEAST_EXPECT((tx0 ^ parentHash) < (tx1 ^ parentHash));
        }

        env.close();

        jv[jss::expand] = true;
        jv[jss::binary] = true;

        jrr = env.rpc("json", "ledger", to_string(jv))[jss::result];
        if (BEAST_EXPECT(jrr[jss::queue_data].size() == 2))
        {
            auto const& txj = jrr[jss::queue_data][1u];
            BEAST_EXPECT(txj[jss::account] == alice.human());
            BEAST_EXPECT(txj[jss::fee_level] == "256");
            BEAST_EXPECT(txj["preflight_result"] == "tesSUCCESS");
            BEAST_EXPECT(txj["retries_remaining"] == 8);
            BEAST_EXPECT(txj["last_result"] == "terPRE_SEQ");
            BEAST_EXPECT(txj.isMember(jss::tx));
            BEAST_EXPECT(txj[jss::tx].isMember(jss::tx_blob));

            auto const& txj2 = jrr[jss::queue_data][0u];
            BEAST_EXPECT(txj2[jss::account] == alice.human());
            BEAST_EXPECT(txj2[jss::fee_level] == "256");
            BEAST_EXPECT(txj2["preflight_result"] == "tesSUCCESS");
            BEAST_EXPECT(txj2["retries_remaining"] == 10);
            BEAST_EXPECT(!txj2.isMember("last_result"));
            BEAST_EXPECT(txj2.isMember(jss::tx));
            BEAST_EXPECT(txj2[jss::tx].isMember(jss::tx_blob));
        }

        for (int i = 0; i != 9; ++i)
        {
            env.close();
        }

        jv[jss::expand] = false;
        jv[jss::binary] = false;

        jrr = env.rpc("json", "ledger", to_string(jv))[jss::result];
        const std::string txid2 = [&]() {
            if (BEAST_EXPECT(jrr[jss::queue_data].size() == 1))
            {
                auto const& txj = jrr[jss::queue_data][0u];
                BEAST_EXPECT(txj[jss::account] == alice.human());
                BEAST_EXPECT(txj[jss::fee_level] == "256");
                BEAST_EXPECT(txj["preflight_result"] == "tesSUCCESS");
                BEAST_EXPECT(txj["retries_remaining"] == 1);
                BEAST_EXPECT(txj["last_result"] == "terPRE_SEQ");
                BEAST_EXPECT(txj.isMember(jss::tx));
                BEAST_EXPECT(txj[jss::tx] != txid0);
                return txj[jss::tx].asString();
            }
            return std::string{};
        }();

        jv[jss::full] = true;

        jrr = env.rpc("json", "ledger", to_string(jv))[jss::result];
        if (BEAST_EXPECT(jrr[jss::queue_data].size() == 1))
        {
            auto const& txj = jrr[jss::queue_data][0u];
            BEAST_EXPECT(txj[jss::account] == alice.human());
            BEAST_EXPECT(txj[jss::fee_level] == "256");
            BEAST_EXPECT(txj["preflight_result"] == "tesSUCCESS");
            BEAST_EXPECT(txj["retries_remaining"] == 1);
            BEAST_EXPECT(txj["last_result"] == "terPRE_SEQ");
            BEAST_EXPECT(txj.isMember(jss::tx));
            auto const& tx = txj[jss::tx];
            BEAST_EXPECT(tx[jss::Account] == alice.human());
            BEAST_EXPECT(tx[jss::TransactionType] == jss::AccountSet);
            BEAST_EXPECT(tx[jss::hash] == txid2);
        }
    }

    void
    testLedgerAccountsOption()
    {
        testcase("Ledger Request, Accounts Hashes");
        using namespace test::jtx;

        Env env{*this};

        env.close();

        std::string index;
        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = 3u;
            jvParams[jss::accounts] = true;
            jvParams[jss::expand] = true;
            jvParams[jss::type] = "hashes";
            auto const jrr =
                env.rpc("json", "ledger", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::ledger].isMember(jss::accountState));
            BEAST_EXPECT(jrr[jss::ledger][jss::accountState].isArray());
            BEAST_EXPECT(jrr[jss::ledger][jss::accountState].size() == 1u);
            BEAST_EXPECT(
                jrr[jss::ledger][jss::accountState][0u]["LedgerEntryType"] ==
                jss::LedgerHashes);
            index = jrr[jss::ledger][jss::accountState][0u]["index"].asString();
        }
        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = 3u;
            jvParams[jss::accounts] = true;
            jvParams[jss::expand] = false;
            jvParams[jss::type] = "hashes";
            auto const jrr =
                env.rpc("json", "ledger", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::ledger].isMember(jss::accountState));
            BEAST_EXPECT(jrr[jss::ledger][jss::accountState].isArray());
            BEAST_EXPECT(jrr[jss::ledger][jss::accountState].size() == 1u);
            BEAST_EXPECT(jrr[jss::ledger][jss::accountState][0u] == index);
        }
    }

    void
    testInvalidOracleLedgerEntry()
    {
        testcase("Invalid Oracle Ledger Entry");
        using namespace ripple::test::jtx;
        using namespace ripple::test::jtx::oracle;

        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(1'000), owner);
        Oracle oracle(env, {.owner = owner});

        // Malformed document id
        auto res = Oracle::ledgerEntry(env, owner, NoneTag);
        BEAST_EXPECT(res[jss::error].asString() == "invalidParams");
        std::vector<AnyValue> invalid = {-1, 1.2, "", "Invalid"};
        for (auto const& v : invalid)
        {
            auto const res = Oracle::ledgerEntry(env, owner, v);
            BEAST_EXPECT(res[jss::error].asString() == "malformedDocumentID");
        }
        // Missing document id
        res = Oracle::ledgerEntry(env, owner, std::nullopt);
        BEAST_EXPECT(res[jss::error].asString() == "malformedRequest");

        // Missing account
        res = Oracle::ledgerEntry(env, std::nullopt, 1);
        BEAST_EXPECT(res[jss::error].asString() == "malformedRequest");

        // Malformed account
        std::string malfAccount = to_string(owner.id());
        malfAccount.replace(10, 1, 1, '!');
        res = Oracle::ledgerEntry(env, malfAccount, 1);
        BEAST_EXPECT(res[jss::error].asString() == "malformedAddress");
    }

    void
    testOracleLedgerEntry()
    {
        testcase("Oracle Ledger Entry");
        using namespace ripple::test::jtx;
        using namespace ripple::test::jtx::oracle;

        Env env(*this);
        std::vector<AccountID> accounts;
        std::vector<std::uint32_t> oracles;
        for (int i = 0; i < 10; ++i)
        {
            Account const owner(std::string("owner") + std::to_string(i));
            env.fund(XRP(1'000), owner);
            // different accounts can have the same asset pair
            Oracle oracle(env, {.owner = owner, .documentID = i});
            accounts.push_back(owner.id());
            oracles.push_back(oracle.documentID());
            // same account can have different asset pair
            Oracle oracle1(env, {.owner = owner, .documentID = i + 10});
            accounts.push_back(owner.id());
            oracles.push_back(oracle1.documentID());
        }
        for (int i = 0; i < accounts.size(); ++i)
        {
            auto const jv = [&]() {
                // document id is uint32
                if (i % 2)
                    return Oracle::ledgerEntry(env, accounts[i], oracles[i]);
                // document id is string
                return Oracle::ledgerEntry(
                    env, accounts[i], std::to_string(oracles[i]));
            }();
            try
            {
                BEAST_EXPECT(
                    jv[jss::node][jss::Owner] == to_string(accounts[i]));
            }
            catch (...)
            {
                fail();
            }
        }
    }

public:
    void
    run() override
    {
        testLedgerRequest();
        testBadInput();
        testLedgerCurrent();
        testMissingLedgerEntryLedgerHash();
        testLedgerFull();
        testLedgerFullNonAdmin();
        testLedgerAccounts();
        testLedgerEntryAccountRoot();
        testLedgerEntryCheck();
        testLedgerEntryDepositPreauth();
        testLedgerEntryDirectory();
        testLedgerEntryEscrow();
        testLedgerEntryOffer();
        testLedgerEntryPayChan();
        testLedgerEntryRippleState();
        testLedgerEntryTicket();
        testLookupLedger();
        testNoQueue();
        testQueue();
        testLedgerAccountsOption();
        testLedgerEntryDID();
        testInvalidOracleLedgerEntry();
        testOracleLedgerEntry();

        forAllApiVersions(std::bind_front(
            &LedgerRPC_test::testLedgerEntryInvalidParams, this));
    }
};

BEAST_DEFINE_TESTSUITE(LedgerRPC, app, ripple);
BEAST_DEFINE_TESTSUITE(LedgerRPC_XChain, app, ripple);

}  // namespace ripple
