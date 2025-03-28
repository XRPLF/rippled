//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.
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
#include <test/jtx/check.h>
#include <test/jtx/envconfig.h>

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/tx/apply.h>

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {

struct Regression_test : public beast::unit_test::suite
{
    // OfferCreate, then OfferCreate with cancel
    void
    testOffer1()
    {
        using namespace jtx;
        Env env(*this);
        auto const gw = Account("gw");
        auto const USD = gw["USD"];
        env.fund(XRP(10000), "alice", gw);
        env(offer("alice", USD(10), XRP(10)), require(owners("alice", 1)));
        env(offer("alice", USD(20), XRP(10)),
            json(R"raw(
                { "OfferSequence" : 4 }
            )raw"),
            require(owners("alice", 1)));
    }

    void
    testLowBalanceDestroy()
    {
        testcase("Account balance < fee destroys correct amount of XRP");
        using namespace jtx;
        Env env(*this);
        env.memoize("alice");

        // The low balance scenario can not deterministically
        // be reproduced against an open ledger. Make a local
        // closed ledger and work with it directly.
        auto closed = std::make_shared<Ledger>(
            create_genesis,
            env.app().config(),
            std::vector<uint256>{},
            env.app().getNodeFamily());
        auto expectedDrops = INITIAL_XRP;
        BEAST_EXPECT(closed->info().drops == expectedDrops);

        auto const aliceXRP = 400;
        auto const aliceAmount = XRP(aliceXRP);

        auto next = std::make_shared<Ledger>(
            *closed, env.app().timeKeeper().closeTime());
        {
            // Fund alice
            auto const jt = env.jt(pay(env.master, "alice", aliceAmount));
            OpenView accum(&*next);

            auto const result =
                ripple::apply(env.app(), accum, *jt.stx, tapNONE, env.journal);
            BEAST_EXPECT(result.ter == tesSUCCESS);
            BEAST_EXPECT(result.applied);

            accum.apply(*next);
        }
        expectedDrops -= next->fees().base;
        BEAST_EXPECT(next->info().drops == expectedDrops);
        {
            auto const sle = next->read(keylet::account(Account("alice").id()));
            BEAST_EXPECT(sle);
            auto balance = sle->getFieldAmount(sfBalance);

            BEAST_EXPECT(balance == aliceAmount);
        }

        {
            // Specify the seq manually since the env's open ledger
            // doesn't know about this account.
            auto const jt = env.jt(noop("alice"), fee(expectedDrops), seq(2));

            OpenView accum(&*next);

            auto const result =
                ripple::apply(env.app(), accum, *jt.stx, tapNONE, env.journal);
            BEAST_EXPECT(result.ter == tecINSUFF_FEE);
            BEAST_EXPECT(result.applied);

            accum.apply(*next);
        }
        {
            auto const sle = next->read(keylet::account(Account("alice").id()));
            BEAST_EXPECT(sle);
            auto balance = sle->getFieldAmount(sfBalance);

            BEAST_EXPECT(balance == XRP(0));
        }
        expectedDrops -= aliceXRP * dropsPerXRP;
        BEAST_EXPECT(next->info().drops == expectedDrops);
    }

    void
    testSecp256r1key()
    {
        testcase("Signing with a secp256r1 key should fail gracefully");
        using namespace jtx;
        Env env(*this);

        // Test case we'll use.
        auto test256r1key = [&env](Account const& acct) {
            auto const baseFee = env.current()->fees().base;
            std::uint32_t const acctSeq = env.seq(acct);
            Json::Value jsonNoop =
                env.json(noop(acct), fee(baseFee), seq(acctSeq), sig(acct));
            JTx jt = env.jt(jsonNoop);
            jt.fill_sig = false;

            // Random secp256r1 public key generated by
            // https://kjur.github.io/jsrsasign/sample-ecdsa.html
            std::string const secp256r1PubKey =
                "045d02995ec24988d9a2ae06a3733aa35ba0741e87527"
                "ed12909b60bd458052c944b24cbf5893c3e5be321774e"
                "5082e11c034b765861d0effbde87423f8476bb2c";

            // Set the key in the JSON.
            jt.jv["SigningPubKey"] = secp256r1PubKey;

            // Set the same key in the STTx.
            auto secp256r1Sig = std::make_unique<STTx>(*(jt.stx));
            auto pubKeyBlob = strUnHex(secp256r1PubKey);
            assert(pubKeyBlob);  // Hex for public key must be valid
            secp256r1Sig->setFieldVL(sfSigningPubKey, *pubKeyBlob);
            jt.stx.reset(secp256r1Sig.release());

            env(jt,
                rpc("invalidTransaction",
                    "fails local checks: Invalid signature."));
        };

        Account const alice{"alice", KeyType::secp256k1};
        Account const becky{"becky", KeyType::ed25519};

        env.fund(XRP(10000), alice, becky);

        test256r1key(alice);
        test256r1key(becky);
    }

    void
    testFeeEscalationAutofill()
    {
        testcase("Autofilled fee should use the escalated fee");
        using namespace jtx;
        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->section("transaction_queue")
                .set("minimum_txn_in_ledger_standalone", "3");
            cfg->FEES.reference_fee = 10;
            return cfg;
        }));
        Env_ss envs(env);

        auto const alice = Account("alice");
        env.fund(XRP(100000), alice);

        auto params = Json::Value(Json::objectValue);
        // Max fee = 50k drops
        params[jss::fee_mult_max] = 5000;
        std::vector<int> const expectedFees({10, 10, 8889, 13889, 20000});

        // We should be able to submit 5 transactions within
        // our fee limit.
        for (int i = 0; i < 5; ++i)
        {
            envs(noop(alice), fee(none), seq(none))(params);

            auto tx = env.tx();
            if (BEAST_EXPECT(tx))
            {
                BEAST_EXPECT(tx->getAccountID(sfAccount) == alice.id());
                BEAST_EXPECT(tx->getTxnType() == ttACCOUNT_SET);
                auto const fee = tx->getFieldAmount(sfFee);
                BEAST_EXPECT(fee == drops(expectedFees[i]));
            }
        }
    }

    void
    testFeeEscalationExtremeConfig()
    {
        testcase("Fee escalation shouldn't allocate extreme memory");
        using clock_type = std::chrono::steady_clock;
        using namespace jtx;
        using namespace std::chrono_literals;

        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            auto& s = cfg->section("transaction_queue");
            s.set("minimum_txn_in_ledger_standalone", "4294967295");
            s.set("minimum_txn_in_ledger", "4294967295");
            s.set("target_txn_in_ledger", "4294967295");
            s.set("normal_consensus_increase_percent", "4294967295");

            return cfg;
        }));

        env(noop(env.master));
        // This test will probably fail if any breakpoints are encountered,
        // but should pass on even the slowest machines.
        auto const start = clock_type::now();
        env.close();
        BEAST_EXPECT(clock_type::now() - start < 1s);
    }

    void
    testJsonInvalid()
    {
        using namespace jtx;
        using boost::asio::buffer;
        testcase("jsonInvalid");

        std::string const request =
            R"json({"command":"path_find","id":19,"subcommand":"create","source_account":"rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh","destination_account":"rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh","destination_amount":"1000000","source_currencies":[{"currency":"0000000000000000000000000000000000000000"},{"currency":"0000000000000000000000005553440000000000"},{"currency":"0000000000000000000000004254430000000000"},{"issuer":"rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh","currency":"0000000000000000000000004254430000000000"},{"issuer":"rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh","currency":"0000000000000000000000004254430000000000"},{"issuer":"rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh","currency":"0000000000000000000000004555520000000000"},{"currency":"0000000000000000000000004554480000000000"},{"currency":"0000000000000000000000004A50590000000000"},{"issuer":"rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh","currency":"000000000000000000000000434E590000000000"},{"currency":"0000000000000000000000004742490000000000"},{"issuer":"rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh","currency":"0000000000000000000000004341440000000000"}]})json";

        Json::Value jvRequest;
        Json::Reader jrReader;

        std::vector<boost::asio::const_buffer> buffers;
        buffers.emplace_back(buffer(request, 1024));
        buffers.emplace_back(
            buffer(request.data() + 1024, request.length() - 1024));
        BEAST_EXPECT(
            jrReader.parse(jvRequest, buffers) && jvRequest.isObject());
    }

    void
    testInvalidTxObjectIDType()
    {
        testcase("Invalid Transaction Object ID Type");
        // Crasher bug introduced in 2.0.1. Fixed in 2.3.0.

        using namespace jtx;
        Env env(*this);

        Account alice("alice");
        Account bob("bob");
        env.fund(XRP(10'000), alice, bob);
        env.close();

        {
            auto const alice_index = keylet::account(alice).key;
            if (BEAST_EXPECT(alice_index.isNonZero()))
            {
                env(check::cash(
                        alice, alice_index, check::DeliverMin(XRP(100))),
                    ter(tecNO_ENTRY));
            }
        }

        {
            auto const bob_index = keylet::account(bob).key;

            auto const digest = [&]() -> std::optional<uint256> {
                auto const& state =
                    env.app().getLedgerMaster().getClosedLedger()->stateMap();
                SHAMapHash digest;
                if (!state.peekItem(bob_index, digest))
                    return std::nullopt;
                return digest.as_uint256();
            }();

            auto const mapCounts = [&](CountedObjects::List const& list) {
                std::map<std::string, int> result;
                for (auto const& e : list)
                {
                    result[e.first] = e.second;
                }

                return result;
            };

            if (BEAST_EXPECT(bob_index.isNonZero()) &&
                BEAST_EXPECT(digest.has_value()))
            {
                auto& cache = env.app().cachedSLEs();
                cache.del(*digest, false);
                auto const beforeCounts =
                    mapCounts(CountedObjects::getInstance().getCounts(0));

                env(check::cash(alice, bob_index, check::DeliverMin(XRP(100))),
                    ter(tecNO_ENTRY));

                auto const afterCounts =
                    mapCounts(CountedObjects::getInstance().getCounts(0));

                using namespace std::string_literals;
                BEAST_EXPECT(
                    beforeCounts.at("CachedView::hit"s) ==
                    afterCounts.at("CachedView::hit"s));
                BEAST_EXPECT(
                    beforeCounts.at("CachedView::hitExpired"s) + 1 ==
                    afterCounts.at("CachedView::hitExpired"s));
                BEAST_EXPECT(
                    beforeCounts.at("CachedView::miss"s) ==
                    afterCounts.at("CachedView::miss"s));
            }
        }
    }

    void
    run() override
    {
        testOffer1();
        testLowBalanceDestroy();
        testSecp256r1key();
        testFeeEscalationAutofill();
        testFeeEscalationExtremeConfig();
        testJsonInvalid();
        testInvalidTxObjectIDType();
    }
};

BEAST_DEFINE_TESTSUITE(Regression, app, ripple);

}  // namespace test
}  // namespace ripple
