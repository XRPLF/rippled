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

#include <test/jtx.h>
#include <test/jtx/Oracle.h>
#include <test/jtx/attester.h>
#include <test/jtx/delegate.h>
#include <test/jtx/multisign.h>
#include <test/jtx/xchain_bridge.h>

#include <xrpld/app/misc/TxQ.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

namespace test {

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

    /// @brief ledger RPC requests as a way to drive
    /// input options to lookupLedger. The point of this test is
    /// coverage for lookupLedger, not so much the ledger
    /// RPC request.
    void
    testLookupLedger()
    {
        testcase("Lookup ledger");
        using namespace test::jtx;

        auto cfg = envconfig();
        cfg->FEES.reference_fee = 10;
        Env env{
            *this, std::move(cfg), FeatureBitset{}};  // hashes requested below
                                                      // assume no amendments
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
            auto jrr =
                env.rpc("json", "ledger", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr.isMember(jss::ledger_hash));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "5");

            jvParams[jss::ledger] = "validated";
            jrr = env.rpc("json", "ledger", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr.isMember(jss::ledger_hash));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "5");

            jvParams[jss::ledger] = "current";
            jrr = env.rpc("json", "ledger", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "6");

            // ask for a bad ledger keyword
            jvParams[jss::ledger] = "invalid";
            jrr = env.rpc("json", "ledger", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerIndexMalformed");

            // numeric index
            jvParams[jss::ledger] = 4;
            jrr = env.rpc("json", "ledger", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr.isMember(jss::ledger_hash));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "4");

            // numeric index - out of range
            jvParams[jss::ledger] = 20;
            jrr = env.rpc("json", "ledger", to_string(jvParams))[jss::result];
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
            auto jrr =
                env.rpc("json", "ledger", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr.isMember(jss::ledger_hash));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "3");

            // extra leading hex chars in hash are not allowed
            jvParams[jss::ledger_hash] = "DEADBEEF" + hash3;
            jrr = env.rpc("json", "ledger", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerHashMalformed");

            // request with non-string ledger_hash
            jvParams[jss::ledger_hash] = 2;
            jrr = env.rpc("json", "ledger", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerHashNotString");

            // malformed (non hex) hash
            jvParams[jss::ledger_hash] =
                "2E81FC6EC0DD943197EGC7E3FBE9AE30"
                "7F2775F2F7485BB37307984C3C0F2340";
            jrr = env.rpc("json", "ledger", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerHashMalformed");

            // properly formed, but just doesn't exist
            jvParams[jss::ledger_hash] =
                "8C3EEDB3124D92E49E75D81A8826A2E6"
                "5A75FD71FC3FD6F36FEB803C5F1D812D";
            jrr = env.rpc("json", "ledger", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "lgrNotFound");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerNotFound");
        }

        {
            // access via the ledger_index field, keyword index values
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "closed";
            auto jrr =
                env.rpc("json", "ledger", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr.isMember(jss::ledger_hash));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "5");
            BEAST_EXPECT(jrr.isMember(jss::ledger_index));

            jvParams[jss::ledger_index] = "validated";
            jrr = env.rpc("json", "ledger", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr.isMember(jss::ledger_hash));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "5");

            jvParams[jss::ledger_index] = "current";
            jrr = env.rpc("json", "ledger", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::ledger));
            BEAST_EXPECT(jrr[jss::ledger][jss::ledger_index] == "6");
            BEAST_EXPECT(jrr.isMember(jss::ledger_current_index));

            // ask for a bad ledger keyword
            jvParams[jss::ledger_index] = "invalid";
            jrr = env.rpc("json", "ledger", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerIndexMalformed");

            // numeric index
            for (auto i : {1, 2, 3, 4, 5, 6})
            {
                jvParams[jss::ledger_index] = i;
                jrr =
                    env.rpc("json", "ledger", to_string(jvParams))[jss::result];
                BEAST_EXPECT(jrr.isMember(jss::ledger));
                if (i < 6)
                    BEAST_EXPECT(jrr.isMember(jss::ledger_hash));
                BEAST_EXPECT(
                    jrr[jss::ledger][jss::ledger_index] == std::to_string(i));
            }

            // numeric index - out of range
            jvParams[jss::ledger_index] = 7;
            jrr = env.rpc("json", "ledger", to_string(jvParams))[jss::result];
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
        auto cfg = envconfig([](std::unique_ptr<Config> cfg) {
            auto& section = cfg->section("transaction_queue");
            section.set("minimum_txn_in_ledger_standalone", "3");
            section.set("normal_consensus_increase_percent", "0");
            return cfg;
        });

        cfg->FEES.reference_fee = 10;
        Env env(*this, std::move(cfg));

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
            last_ledger_seq(7),
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
        std::string const txid0 = [&]() {
            auto const& parentHash = env.current()->info().parentHash;
            if (BEAST_EXPECT(jrr[jss::queue_data].size() == 2))
            {
                std::string const txid1 = [&]() {
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
                auto const txid0 = tx[jss::hash].asString();
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
        std::string const txid2 = [&]() {
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
        int hashesLedgerEntryIndex = -1;
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

            for (auto i = 0; i < jrr[jss::ledger][jss::accountState].size();
                 i++)
                if (jrr[jss::ledger][jss::accountState][i]["LedgerEntryType"] ==
                    jss::LedgerHashes)
                {
                    index = jrr[jss::ledger][jss::accountState][i]["index"]
                                .asString();
                    hashesLedgerEntryIndex = i;
                }

            for (auto const& object : jrr[jss::ledger][jss::accountState])
                if (object["LedgerEntryType"] == jss::LedgerHashes)
                    index = object["index"].asString();

            // jss::type is a deprecated field
            BEAST_EXPECT(
                jrr.isMember(jss::warnings) && jrr[jss::warnings].isArray() &&
                jrr[jss::warnings].size() == 1 &&
                jrr[jss::warnings][0u][jss::id].asInt() ==
                    warnRPC_FIELDS_DEPRECATED);
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
            BEAST_EXPECT(
                hashesLedgerEntryIndex > 0 &&
                jrr[jss::ledger][jss::accountState][hashesLedgerEntryIndex] ==
                    index);

            // jss::type is a deprecated field
            BEAST_EXPECT(
                jrr.isMember(jss::warnings) && jrr[jss::warnings].isArray() &&
                jrr[jss::warnings].size() == 1 &&
                jrr[jss::warnings][0u][jss::id].asInt() ==
                    warnRPC_FIELDS_DEPRECATED);
        }
    }

public:
    void
    run() override
    {
        testLedgerRequest();
        testBadInput();
        testLedgerCurrent();
        testLedgerFull();
        testLedgerFullNonAdmin();
        testLedgerAccounts();
        testLookupLedger();
        testNoQueue();
        testQueue();
        testLedgerAccountsOption();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerRPC, rpc, ripple);

}  // namespace test
}  // namespace ripple
