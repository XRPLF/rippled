#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/Env_ss.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>  // IWYU pragma: keep
#include <test/jtx/check.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/fee.h>
#include <test/jtx/jtx_json.h>
#include <test/jtx/noop.h>
#include <test/jtx/offer.h>
#include <test/jtx/owners.h>
#include <test/jtx/pay.h>
#include <test/jtx/require.h>
#include <test/jtx/rpc.h>
#include <test/jtx/seq.h>
#include <test/jtx/sig.h>
#include <test/jtx/tags.h>
#include <test/jtx/ter.h>

#include <xrpld/app/ledger/LedgerMaster.h>

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/apply.h>

#include <boost/asio/buffer.hpp>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace xrpl::test {

struct Regression_test : public beast::unit_test::Suite
{
    // OfferCreate, then OfferCreate with cancel
    void
    testOffer1()
    {
        using namespace jtx;
        Env env(*this);
        auto const gw = Account("gw");
        auto const usd = gw["USD"];
        env.fund(XRP(10000), "alice", gw);
        env(offer("alice", usd(10), XRP(10)), Require(Owners("alice", 1)));
        env(offer("alice", usd(20), XRP(10)),
            Json(R"raw(
                { "OfferSequence" : 4 }
            )raw"),
            Require(Owners("alice", 1)));
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
            kCreateGenesis,
            Rules{env.app().config().features},
            env.app().config().fees.toFees(),
            std::vector<uint256>{},
            env.app().getNodeFamily());
        auto expectedDrops = kInitialXrp;
        BEAST_EXPECT(closed->header().drops == expectedDrops);

        auto const aliceXRP = 400;
        auto const aliceAmount = XRP(aliceXRP);

        auto next = std::make_shared<Ledger>(*closed, env.app().getTimeKeeper().closeTime());
        {
            // Fund alice
            auto const jt = env.jt(pay(env.master, "alice", aliceAmount));
            OpenView accum(&*next);

            auto const result = xrpl::apply(env.app(), accum, *jt.stx, TapNone, env.journal);
            BEAST_EXPECT(isTesSuccess(result.ter));
            BEAST_EXPECT(result.applied);

            accum.apply(*next);
        }
        expectedDrops -= next->fees().base;
        BEAST_EXPECT(next->header().drops == expectedDrops);
        {
            auto const sle = next->read(keylet::account(Account("alice").id()));
            BEAST_EXPECT(sle);
            auto balance = sle->getFieldAmount(sfBalance);

            BEAST_EXPECT(balance == aliceAmount);
        }

        {
            // Specify the seq manually since the env's open ledger
            // doesn't know about this account.
            auto const jt = env.jt(noop("alice"), Fee(expectedDrops), Seq(2));

            OpenView accum(&*next);

            auto const result = xrpl::apply(env.app(), accum, *jt.stx, TapNone, env.journal);
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
        expectedDrops -= aliceXRP * kDropsPerXrp;
        BEAST_EXPECT(next->header().drops == expectedDrops);
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
            json::Value const jsonNoOp =
                env.json(noop(acct), Fee(baseFee), Seq(acctSeq), Sig(acct));
            JTx jt = env.jt(jsonNoOp);
            jt.fillSig = false;

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

            env(jt, Rpc("invalidTransaction", "fails local checks: Invalid signature."));
        };

        Account const alice{"alice", KeyType::Secp256k1};
        Account const becky{"becky", KeyType::Ed25519};

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
            cfg->section("transaction_queue").set("minimum_txn_in_ledger_standalone", "3");
            cfg->fees.referenceFee = 10;
            return cfg;
        }));
        EnvSs envs(env);

        auto const alice = Account("alice");
        env.fund(XRP(100000), alice);

        auto params = json::Value(json::ValueType::Object);
        // Max fee = 50k drops
        params[jss::fee_mult_max] = 5000;
        std::vector<int> const expectedFees({10, 10, 8889, 13889, 20000});

        // We should be able to submit 5 transactions within
        // our fee limit.
        for (int i = 0; i < 5; ++i)
        {
            envs(noop(alice), Fee(kNone), Seq(kNone))(params);

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

        json::Value jvRequest;
        json::Reader jrReader;

        std::vector<boost::asio::const_buffer> buffers;
        buffers.emplace_back(buffer(request, 1024));
        buffers.emplace_back(buffer(request.data() + 1024, request.length() - 1024));
        BEAST_EXPECT(jrReader.parse(jvRequest, buffers) && jvRequest.isObject());
    }

    void
    testInvalidTxObjectIDType()
    {
        testcase("Invalid Transaction Object ID Type");
        // Crasher bug introduced in 2.0.1. Fixed in 2.3.0.

        using namespace jtx;
        Env env(*this);

        Account const alice("alice");
        Account const bob("bob");
        env.fund(XRP(10'000), alice, bob);
        env.close();

        {
            auto const aliceIndex = keylet::account(alice).key;
            if (BEAST_EXPECT(aliceIndex.isNonZero()))
            {
                env(check::cash(alice, aliceIndex, check::DeliverMin(XRP(100))), Ter(tecNO_ENTRY));
            }
        }

        {
            auto const bobIndex = keylet::account(bob).key;

            auto const digest = [&]() -> std::optional<uint256> {
                auto const& state = env.app().getLedgerMaster().getClosedLedger()->stateMap();
                SHAMapHash digest;
                if (!state.peekItem(bobIndex, digest))
                    return std::nullopt;
                return digest.asUInt256();
            }();

            auto const mapCounts = [&](CountedObjects::List const& list) {
                std::map<std::string, int> result;
                for (auto const& e : list)
                {
                    result[e.first] = e.second;
                }

                return result;
            };

            if (BEAST_EXPECT(bobIndex.isNonZero()) && BEAST_EXPECT(digest.has_value()))
            {
                auto& cache = env.app().getCachedSLEs();
                cache.del(*digest, false);  // NOLINT(bugprone-unchecked-optional-access)
                auto const beforeCounts = mapCounts(CountedObjects::getInstance().getCounts(0));

                env(check::cash(alice, bobIndex, check::DeliverMin(XRP(100))), Ter(tecNO_ENTRY));

                auto const afterCounts = mapCounts(CountedObjects::getInstance().getCounts(0));

                using namespace std::string_literals;
                BEAST_EXPECT(
                    beforeCounts.at("CachedView::hit"s) == afterCounts.at("CachedView::hit"s));
                BEAST_EXPECT(
                    beforeCounts.at("CachedView::hitExpired"s) + 1 ==
                    afterCounts.at("CachedView::hitExpired"s));
                BEAST_EXPECT(
                    beforeCounts.at("CachedView::miss"s) == afterCounts.at("CachedView::miss"s));
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

BEAST_DEFINE_TESTSUITE(Regression, app, xrpl);

}  // namespace xrpl::test
