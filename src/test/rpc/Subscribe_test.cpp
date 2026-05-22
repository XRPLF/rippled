#include <test/jtx/Env.h>
#include <test/jtx/WSClient.h>
#include <test/jtx/amount.h>
#include <test/jtx/domain.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/fee.h>
#include <test/jtx/offer.h>
#include <test/jtx/owners.h>  // IWYU pragma: keep
#include <test/jtx/paths.h>
#include <test/jtx/pay.h>
#include <test/jtx/permissioned_dex.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/seq.h>
#include <test/jtx/sig.h>
#include <test/jtx/tags.h>
#include <test/jtx/token.h>
#include <test/jtx/txflags.h>

#include <xrpld/app/main/LoadManager.h>
#include <xrpld/core/Config.h>
#include <xrpld/core/ConfigSections.h>

#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/core/NetworkIDService.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/STValidation.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/tokens.h>
#include <xrpl/server/LoadFeeTrack.h>
#include <xrpl/server/NetworkOPs.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace xrpl::test {

class Subscribe_test : public beast::unit_test::Suite
{
public:
    void
    testServer()
    {
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env{*this, singleThreadIo(envconfig())};
        auto wsc = makeWSClient(env.app().config());
        json::Value stream;

        {
            // RPC subscribe to server stream
            stream[jss::streams] = json::ValueType::Array;
            stream[jss::streams].append("server");
            auto jv = wsc->invoke("subscribe", stream);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::status] == "success");
        }

        // here we forcibly stop the load manager because it can (rarely but
        // every-so-often) cause fees to raise or lower AFTER we've called the
        // first findMsg but BEFORE we unsubscribe, thus causing the final
        // findMsg check to fail since there is one unprocessed ws msg created
        // by the loadmanager
        env.app().getLoadManager().stop();
        {
            // Raise fee to cause an update
            auto& feeTrack = env.app().getFeeTrack();
            for (int i = 0; i < 5; ++i)
                feeTrack.raiseLocalFee();
            env.app().getOPs().reportFeeChange();

            // Check stream update
            BEAST_EXPECT(
                wsc->findMsg(5s, [&](auto const& jv) { return jv[jss::type] == "serverStatus"; }));
        }

        {
            // RPC unsubscribe
            auto jv = wsc->invoke("unsubscribe", stream);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::status] == "success");
        }

        {
            // Raise fee to cause an update
            auto& feeTrack = env.app().getFeeTrack();
            for (int i = 0; i < 5; ++i)
                feeTrack.raiseLocalFee();
            env.app().getOPs().reportFeeChange();

            // Check stream update
            auto jvo = wsc->getMsg(10ms);
            BEAST_EXPECTS(!jvo, "getMsg: " + to_string(jvo.value()));
        }
    }

    void
    testLedger()
    {
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env{*this, singleThreadIo(envconfig())};
        auto wsc = makeWSClient(env.app().config());
        json::Value stream;

        {
            // RPC subscribe to ledger stream
            stream[jss::streams] = json::ValueType::Array;
            stream[jss::streams].append("ledger");
            auto jv = wsc->invoke("subscribe", stream);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::result][jss::ledger_index] == 2);
            BEAST_EXPECT(
                jv[jss::result][jss::network_id] == env.app().getNetworkIDService().getNetworkID());
        }

        {
            // Accept a ledger
            BEAST_EXPECT(env.syncClose());

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                return jv[jss::ledger_index] == 3 &&
                    jv[jss::network_id] == env.app().getNetworkIDService().getNetworkID();
            }));
        }

        {
            // Accept another ledger
            BEAST_EXPECT(env.syncClose());

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                return jv[jss::ledger_index] == 4 &&
                    jv[jss::network_id] == env.app().getNetworkIDService().getNetworkID();
            }));
        }

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", stream);
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
        }
        BEAST_EXPECT(jv[jss::status] == "success");
    }

    void
    testTransactionsAPIv1()
    {
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this, singleThreadIo(envconfig()));
        auto baseFee = env.current()->fees().base.drops();
        auto wsc = makeWSClient(env.app().config());
        json::Value stream;

        {
            // RPC subscribe to transactions stream
            stream[jss::streams] = json::ValueType::Array;
            stream[jss::streams].append("transactions");
            auto jv = wsc->invoke("subscribe", stream);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::status] == "success");
        }

        {
            env.fund(XRP(10000), "alice");
            BEAST_EXPECT(env.syncClose());

            // Check stream update for payment transaction
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                return jv[jss::meta]["AffectedNodes"][1u]["CreatedNode"]["NewFields"]
                         [jss::Account] == Account("alice").human() &&
                    jv[jss::transaction][jss::TransactionType] == jss::Payment &&
                    jv[jss::transaction][jss::DeliverMax] ==
                    std::to_string(10000000000 + baseFee) &&
                    jv[jss::transaction][jss::Fee] == std::to_string(baseFee) &&
                    jv[jss::transaction][jss::Sequence] == 1;
            }));

            // Check stream update for accountset transaction
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                return jv[jss::meta]["AffectedNodes"][0u]["ModifiedNode"]["FinalFields"]
                         [jss::Account] == Account("alice").human();
            }));

            env.fund(XRP(10000), "bob");
            BEAST_EXPECT(env.syncClose());

            // Check stream update for payment transaction
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                return jv[jss::meta]["AffectedNodes"][1u]["CreatedNode"]["NewFields"]
                         [jss::Account]  //
                    == Account("bob").human() &&
                    jv[jss::transaction][jss::TransactionType]  //
                    == jss::Payment &&
                    jv[jss::transaction][jss::DeliverMax]  //
                    == std::to_string(10000000000 + baseFee) &&
                    jv[jss::transaction][jss::Fee]  //
                    == std::to_string(baseFee) &&
                    jv[jss::transaction][jss::Sequence]  //
                    == 2;
            }));

            // Check stream update for accountset transaction
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                return jv[jss::meta]["AffectedNodes"][0u]["ModifiedNode"]["FinalFields"]
                         [jss::Account] == Account("bob").human();
            }));
        }

        {
            // RPC unsubscribe
            auto jv = wsc->invoke("unsubscribe", stream);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::status] == "success");
        }

        {
            // RPC subscribe to accounts stream
            stream = json::ValueType::Object;
            stream[jss::accounts] = json::ValueType::Array;
            stream[jss::accounts].append(Account("alice").human());
            auto jv = wsc->invoke("subscribe", stream);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::status] == "success");
        }

        {
            // Transaction that does not affect stream
            env.fund(XRP(10000), "carol");
            BEAST_EXPECT(env.syncClose());
            BEAST_EXPECT(!wsc->getMsg(10ms));

            // Transactions concerning alice
            env.trust(Account("bob")["USD"](100), "alice");
            BEAST_EXPECT(env.syncClose());

            // Check stream updates
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                return jv[jss::meta]["AffectedNodes"][1u]["ModifiedNode"]["FinalFields"]
                         [jss::Account] == Account("alice").human();
            }));

            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                return jv[jss::meta]["AffectedNodes"][1u]["CreatedNode"]["NewFields"]["LowLimit"]
                         [jss::issuer] == Account("alice").human();
            }));
        }

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", stream);
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
        }
        BEAST_EXPECT(jv[jss::status] == "success");
    }

    void
    testTransactionsAPIv2()
    {
        testcase("transactions API version 2");

        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->fees.referenceFee = 10;
            cfg = singleThreadIo(std::move(cfg));
            return cfg;
        }));
        auto wsc = makeWSClient(env.app().config());
        json::Value stream{json::ValueType::Object};

        {
            // RPC subscribe to transactions stream
            stream[jss::api_version] = 2;
            stream[jss::streams] = json::ValueType::Array;
            stream[jss::streams].append("transactions");
            auto jv = wsc->invoke("subscribe", stream);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::status] == "success");
        }

        {
            env.fund(XRP(10000), "alice");
            BEAST_EXPECT(env.syncClose());

            // Check stream update for payment transaction
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                return jv[jss::meta]["AffectedNodes"][1u]["CreatedNode"]["NewFields"]
                         [jss::Account]  //
                    == Account("alice").human() &&
                    jv[jss::close_time_iso]  //
                    == "2000-01-01T00:00:10Z" &&
                    jv[jss::validated] == true &&  //
                    jv[jss::ledger_hash] ==
                    "0F1A9E0C109ADEF6DA2BDE19217C12BBEC57174CBDBD212B0EBDC1CEDB"
                    "853185" &&  //
                    !jv[jss::inLedger] &&
                    jv[jss::ledger_index] == 3 &&           //
                    jv[jss::tx_json][jss::TransactionType]  //
                    == jss::Payment &&
                    jv[jss::tx_json][jss::DeliverMax]  //
                    == "10000000010" &&
                    !jv[jss::tx_json].isMember(jss::Amount) &&
                    jv[jss::tx_json][jss::Fee]  //
                    == "10" &&
                    jv[jss::tx_json][jss::Sequence]  //
                    == 1;
            }));

            // Check stream update for accountset transaction
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                return jv[jss::meta]["AffectedNodes"][0u]["ModifiedNode"]["FinalFields"]
                         [jss::Account] == Account("alice").human();
            }));
        }

        {
            // RPC unsubscribe
            auto jv = wsc->invoke("unsubscribe", stream);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::status] == "success");
        }
    }

    void
    testManifests()
    {
        using namespace jtx;
        Env env(*this, singleThreadIo(envconfig()));
        auto wsc = makeWSClient(env.app().config());
        json::Value stream;

        {
            // RPC subscribe to manifests stream
            stream[jss::streams] = json::ValueType::Array;
            stream[jss::streams].append("manifests");
            auto jv = wsc->invoke("subscribe", stream);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::status] == "success");
        }

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", stream);
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
        }
        BEAST_EXPECT(jv[jss::status] == "success");
    }

    void
    testValidations(FeatureBitset features)
    {
        using namespace jtx;

        Env env{*this, singleThreadIo(envconfig(validator, "")), features};
        auto& cfg = env.app().config();
        if (!BEAST_EXPECT(cfg.section(SECTION_VALIDATION_SEED).empty()))
            return;
        auto const parsedseed = parseBase58<Seed>(cfg.section(SECTION_VALIDATION_SEED).values()[0]);
        if (BEAST_EXPECT(parsedseed); not parsedseed.has_value())
            return;

        std::string const valPublicKey = toBase58(
            TokenType::NodePublic,
            derivePublicKey(
                KeyType::Secp256k1, generateSecretKey(KeyType::Secp256k1, *parsedseed)));

        auto wsc = makeWSClient(env.app().config());
        json::Value stream;

        {
            // RPC subscribe to validations stream
            stream[jss::streams] = json::ValueType::Array;
            stream[jss::streams].append("validations");
            auto jv = wsc->invoke("subscribe", stream);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::status] == "success");
        }

        {
            // Lambda to check ledger validations from the stream.
            auto validValidationFields = [&env, &valPublicKey](json::Value const& jv) {
                if (jv[jss::type] != "validationReceived")
                    return false;

                if (jv[jss::validation_public_key].asString() != valPublicKey)
                    return false;

                if (jv[jss::ledger_hash] != to_string(env.closed()->header().hash))
                    return false;

                if (jv[jss::ledger_index] != std::to_string(env.closed()->header().seq))
                    return false;

                if (jv[jss::flags] != (kVfFullyCanonicalSig | kVfFullValidation))
                    return false;

                if (jv[jss::full] != true)
                    return false;

                if (jv.isMember(jss::load_fee))
                    return false;

                if (!jv.isMember(jss::signature))
                    return false;

                if (!jv.isMember(jss::signing_time))
                    return false;

                if (!jv.isMember(jss::cookie))
                    return false;

                if (!jv.isMember(jss::validated_hash))
                    return false;

                uint32_t const netID = env.app().getNetworkIDService().getNetworkID();
                if (!jv.isMember(jss::network_id) || jv[jss::network_id] != netID)
                    return false;

                // Certain fields are only added on a flag ledger.
                bool const isFlagLedger = (env.closed()->header().seq + 1) % 256 == 0;

                if (jv.isMember(jss::server_version) != isFlagLedger)
                    return false;

                if (jv.isMember(jss::reserve_base) != isFlagLedger)
                    return false;

                if (jv.isMember(jss::reserve_inc) != isFlagLedger)
                    return false;

                return true;
            };

            // Check stream update.  Look at enough stream entries so we see
            // at least one flag ledger.
            while (env.closed()->header().seq < 300)
            {
                BEAST_EXPECT(env.syncClose());
                using namespace std::chrono_literals;
                BEAST_EXPECT(wsc->findMsg(5s, validValidationFields));
            }
        }

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", stream);
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
        }
        BEAST_EXPECT(jv[jss::status] == "success");
    }

    void
    testSubByUrl()
    {
        using namespace jtx;
        testcase("Subscribe by url");
        Env env{*this, singleThreadIo(envconfig())};

        json::Value jv;
        jv[jss::url] = "http://localhost/events";
        jv[jss::url_username] = "admin";
        jv[jss::url_password] = "password";
        jv[jss::streams] = json::ValueType::Array;
        jv[jss::streams][0u] = "validations";
        auto jr = env.rpc("json", "subscribe", to_string(jv))[jss::result];
        BEAST_EXPECT(jr[jss::status] == "success");

        jv[jss::streams][0u] = "ledger";
        jr = env.rpc("json", "subscribe", to_string(jv))[jss::result];
        BEAST_EXPECT(jr[jss::status] == "success");
        BEAST_EXPECT(jr[jss::network_id] == env.app().getNetworkIDService().getNetworkID());

        jr = env.rpc("json", "unsubscribe", to_string(jv))[jss::result];
        BEAST_EXPECT(jr[jss::status] == "success");

        jv[jss::streams][0u] = "validations";
        jr = env.rpc("json", "unsubscribe", to_string(jv))[jss::result];
        BEAST_EXPECT(jr[jss::status] == "success");
    }

    void
    testSubErrors(bool subscribe)
    {
        using namespace jtx;
        auto const method = subscribe ? "subscribe" : "unsubscribe";
        testcase << "Error cases for " << method;

        Env env{*this, singleThreadIo(envconfig())};
        auto wsc = makeWSClient(env.app().config());

        {
            auto const jr = env.rpc("json", method, "{}")[jss::result];
            BEAST_EXPECT(jr[jss::error] == "invalidParams");
            BEAST_EXPECT(jr[jss::error_message] == "Invalid parameters.");
        }

        {
            json::Value jv;
            jv[jss::url] = "not-a-url";
            jv[jss::username] = "admin";
            jv[jss::password] = "password";
            auto const jr = env.rpc("json", method, to_string(jv))[jss::result];
            if (subscribe)
            {
                BEAST_EXPECT(jr[jss::error] == "invalidParams");
                BEAST_EXPECT(jr[jss::error_message] == "Failed to parse url.");
            }
            // else TODO: why isn't this an error for unsubscribe ?
            // (findRpcSub returns null)
        }

        {
            json::Value jv;
            jv[jss::url] = "ftp://scheme.not.supported.tld";
            auto const jr = env.rpc("json", method, to_string(jv))[jss::result];
            if (subscribe)
            {
                BEAST_EXPECT(jr[jss::error] == "invalidParams");
                BEAST_EXPECT(jr[jss::error_message] == "Only http and https is supported.");
            }
        }

        {
            Env envNonadmin{*this, singleThreadIo(noAdmin(envconfig()))};
            json::Value jv;
            jv[jss::url] = "no-url";
            auto const jr = envNonadmin.rpc("json", method, to_string(jv))[jss::result];
            BEAST_EXPECT(jr[jss::error] == "noPermission");
            BEAST_EXPECT(jr[jss::error_message] == "You don't have permission for this command.");
        }

        std::initializer_list<json::Value> const nonArrays{
            json::ValueType::Null,
            json::ValueType::Int,
            json::ValueType::UInt,
            json::ValueType::Real,
            "",
            json::ValueType::Boolean,
            json::ValueType::Object};

        for (auto const& f : {jss::accounts_proposed, jss::accounts})
        {
            for (auto const& nonArray : nonArrays)
            {
                json::Value jv;
                jv[f] = nonArray;
                auto const jr = wsc->invoke(method, jv)[jss::result];
                BEAST_EXPECT(jr[jss::error] == "invalidParams");
                BEAST_EXPECT(jr[jss::error_message] == "Invalid parameters.");
            }

            {
                json::Value jv;
                jv[f] = json::ValueType::Array;
                auto const jr = wsc->invoke(method, jv)[jss::result];
                BEAST_EXPECT(jr[jss::error] == "actMalformed");
                BEAST_EXPECT(jr[jss::error_message] == "Account malformed.");
            }
        }

        for (auto const& nonArray : nonArrays)
        {
            json::Value jv;
            jv[jss::books] = nonArray;
            auto const jr = wsc->invoke(method, jv)[jss::result];
            BEAST_EXPECT(jr[jss::error] == "invalidParams");
            BEAST_EXPECT(jr[jss::error_message] == "Invalid parameters.");
        }

        {
            json::Value jv;
            jv[jss::books] = json::ValueType::Array;
            jv[jss::books][0u] = 1;
            auto const jr = wsc->invoke(method, jv)[jss::result];
            BEAST_EXPECT(jr[jss::error] == "invalidParams");
            BEAST_EXPECT(jr[jss::error_message] == "Invalid parameters.");
        }

        {
            json::Value jv;
            jv[jss::books] = json::ValueType::Array;
            jv[jss::books][0u] = json::ValueType::Object;
            jv[jss::books][0u][jss::taker_gets] = json::ValueType::Object;
            jv[jss::books][0u][jss::taker_pays] = json::ValueType::Object;
            auto const jr = wsc->invoke(method, jv)[jss::result];

            BEAST_EXPECT(jr[jss::error] == "srcCurMalformed");
            BEAST_EXPECT(jr[jss::error_message] == "Source currency is malformed.");
        }

        {
            json::Value jv;
            jv[jss::books] = json::ValueType::Array;
            jv[jss::books][0u] = json::ValueType::Object;
            jv[jss::books][0u][jss::taker_gets] = json::ValueType::Object;
            jv[jss::books][0u][jss::taker_pays] = json::ValueType::Object;
            jv[jss::books][0u][jss::taker_pays][jss::currency] = "ZZZZ";
            auto const jr = wsc->invoke(method, jv)[jss::result];
            BEAST_EXPECT(jr[jss::error] == "srcCurMalformed");
            BEAST_EXPECT(jr[jss::error_message] == "Source currency is malformed.");
        }

        {
            json::Value jv;
            jv[jss::books] = json::ValueType::Array;
            jv[jss::books][0u] = json::ValueType::Object;
            jv[jss::books][0u][jss::taker_gets] = json::ValueType::Object;
            jv[jss::books][0u][jss::taker_pays] = json::ValueType::Object;
            jv[jss::books][0u][jss::taker_pays][jss::currency] = "USD";
            jv[jss::books][0u][jss::taker_pays][jss::issuer] = 1;
            auto const jr = wsc->invoke(method, jv)[jss::result];
            BEAST_EXPECT(jr[jss::error] == "srcIsrMalformed");
            BEAST_EXPECT(jr[jss::error_message] == "Source issuer is malformed.");
        }

        {
            json::Value jv;
            jv[jss::books] = json::ValueType::Array;
            jv[jss::books][0u] = json::ValueType::Object;
            jv[jss::books][0u][jss::taker_gets] = json::ValueType::Object;
            jv[jss::books][0u][jss::taker_pays] = json::ValueType::Object;
            jv[jss::books][0u][jss::taker_pays][jss::currency] = "USD";
            jv[jss::books][0u][jss::taker_pays][jss::issuer] = Account{"gateway"}.human() + "DEAD";
            auto const jr = wsc->invoke(method, jv)[jss::result];
            BEAST_EXPECT(jr[jss::error] == "srcIsrMalformed");
            BEAST_EXPECT(jr[jss::error_message] == "Source issuer is malformed.");
        }

        {
            json::Value jv;
            jv[jss::books] = json::ValueType::Array;
            jv[jss::books][0u] = json::ValueType::Object;
            jv[jss::books][0u][jss::taker_pays] =
                Account{"gateway"}["USD"](1).value().getJson(JsonOptions::Values::IncludeDate);
            jv[jss::books][0u][jss::taker_gets] = json::ValueType::Object;
            auto const jr = wsc->invoke(method, jv)[jss::result];
            // NOTE: this error is slightly incongruous with the equivalent source currency error
            BEAST_EXPECT(jr[jss::error] == "dstAmtMalformed");
            BEAST_EXPECT(
                jr[jss::error_message] == "Destination amount/currency/issuer is malformed.");
        }

        {
            json::Value jv;
            jv[jss::books] = json::ValueType::Array;
            jv[jss::books][0u] = json::ValueType::Object;
            jv[jss::books][0u][jss::taker_pays] =
                Account{"gateway"}["USD"](1).value().getJson(JsonOptions::Values::IncludeDate);
            jv[jss::books][0u][jss::taker_gets][jss::currency] = "ZZZZ";
            auto const jr = wsc->invoke(method, jv)[jss::result];
            // NOTE: this error is slightly incongruous with the
            // equivalent source currency error
            BEAST_EXPECT(jr[jss::error] == "dstAmtMalformed");
            BEAST_EXPECT(
                jr[jss::error_message] == "Destination amount/currency/issuer is malformed.");
        }

        {
            json::Value jv;
            jv[jss::books] = json::ValueType::Array;
            jv[jss::books][0u] = json::ValueType::Object;
            jv[jss::books][0u][jss::taker_pays] =
                Account{"gateway"}["USD"](1).value().getJson(JsonOptions::Values::IncludeDate);
            jv[jss::books][0u][jss::taker_gets][jss::currency] = "USD";
            jv[jss::books][0u][jss::taker_gets][jss::issuer] = 1;
            auto const jr = wsc->invoke(method, jv)[jss::result];
            BEAST_EXPECT(jr[jss::error] == "dstIsrMalformed");
            BEAST_EXPECT(jr[jss::error_message] == "Destination issuer is malformed.");
        }

        {
            json::Value jv;
            jv[jss::books] = json::ValueType::Array;
            jv[jss::books][0u] = json::ValueType::Object;
            jv[jss::books][0u][jss::taker_pays] =
                Account{"gateway"}["USD"](1).value().getJson(JsonOptions::Values::IncludeDate);
            jv[jss::books][0u][jss::taker_gets][jss::currency] = "USD";
            jv[jss::books][0u][jss::taker_gets][jss::issuer] = Account{"gateway"}.human() + "DEAD";
            auto const jr = wsc->invoke(method, jv)[jss::result];
            BEAST_EXPECT(jr[jss::error] == "dstIsrMalformed");
            BEAST_EXPECT(jr[jss::error_message] == "Destination issuer is malformed.");
        }

        {
            json::Value jv;
            jv[jss::books] = json::ValueType::Array;
            jv[jss::books][0u] = json::ValueType::Object;
            jv[jss::books][0u][jss::taker_pays] =
                Account{"gateway"}["USD"](1).value().getJson(JsonOptions::Values::IncludeDate);
            jv[jss::books][0u][jss::taker_gets] =
                Account{"gateway"}["USD"](1).value().getJson(JsonOptions::Values::IncludeDate);
            auto const jr = wsc->invoke(method, jv)[jss::result];
            BEAST_EXPECT(jr[jss::error] == "badMarket");
            BEAST_EXPECT(jr[jss::error_message] == "No such market.");
        }

        for (auto const& nonArray : nonArrays)
        {
            json::Value jv;
            jv[jss::streams] = nonArray;
            auto const jr = wsc->invoke(method, jv)[jss::result];
            BEAST_EXPECT(jr[jss::error] == "invalidParams");
            BEAST_EXPECT(jr[jss::error_message] == "Invalid parameters.");
        }

        {
            json::Value jv;
            jv[jss::streams] = json::ValueType::Array;
            jv[jss::streams][0u] = 1;
            auto const jr = wsc->invoke(method, jv)[jss::result];
            BEAST_EXPECT(jr[jss::error] == "malformedStream");
            BEAST_EXPECT(jr[jss::error_message] == "Stream malformed.");
        }

        {
            json::Value jv;
            jv[jss::streams] = json::ValueType::Array;
            jv[jss::streams][0u] = "not_a_stream";
            auto const jr = wsc->invoke(method, jv)[jss::result];
            BEAST_EXPECT(jr[jss::error] == "malformedStream");
            BEAST_EXPECT(jr[jss::error_message] == "Stream malformed.");
        }

        if (subscribe)
        {
            // invalid taker - not a string
            {
                json::Value jv;
                jv[jss::books] = json::ValueType::Array;
                jv[jss::books][0u] = json::ValueType::Object;
                jv[jss::books][0u][jss::taker_pays] =
                    Account{"gateway"}["USD"](1).value().getJson(JsonOptions::Values::IncludeDate);
                jv[jss::books][0u][jss::taker_gets][jss::currency] = "XRP";
                jv[jss::books][0u][jss::taker] = 1;
                auto const jr = wsc->invoke(method, jv)[jss::result];
                BEAST_EXPECTS(jr[jss::error] == "actMalformed", jr.toStyledString());
                BEAST_EXPECT(jr[jss::error_message] == "Account malformed.");
            }

            // invalid taker - malformed account string
            {
                json::Value jv;
                jv[jss::books] = json::ValueType::Array;
                jv[jss::books][0u] = json::ValueType::Object;
                jv[jss::books][0u][jss::taker_pays] =
                    Account{"gateway"}["USD"](1).value().getJson(JsonOptions::Values::IncludeDate);
                jv[jss::books][0u][jss::taker_gets][jss::currency] = "XRP";
                jv[jss::books][0u][jss::taker] = "not_an_account";
                auto const jr = wsc->invoke(method, jv)[jss::result];
                BEAST_EXPECTS(jr[jss::error] == "actMalformed", jr.toStyledString());
                BEAST_EXPECT(jr[jss::error_message] == "Account malformed.");
            }

            // invalid taker - account string with extra characters
            {
                json::Value jv;
                jv[jss::books] = json::ValueType::Array;
                jv[jss::books][0u] = json::ValueType::Object;
                jv[jss::books][0u][jss::taker_pays] =
                    Account{"gateway"}["USD"](1).value().getJson(JsonOptions::Values::IncludeDate);
                jv[jss::books][0u][jss::taker_gets][jss::currency] = "XRP";
                jv[jss::books][0u][jss::taker] = Account{"alice"}.human() + "DEAD";
                auto const jr = wsc->invoke(method, jv)[jss::result];
                BEAST_EXPECTS(jr[jss::error] == "actMalformed", jr.toStyledString());
                BEAST_EXPECT(jr[jss::error_message] == "Account malformed.");
            }
        }
    }

    void
    testHistoryTxStream()
    {
        testcase("HistoryTxStream");

        using namespace std::chrono_literals;
        using namespace jtx;
        using IdxHashVec = std::vector<std::tuple<int, std::string, bool, int>>;

        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const david("david");
        ///////////////////////////////////////////////////////////////////

        /*
         * return true if the subscribe or unsubscribe result is a success
         */
        auto goodSubRPC = [](json::Value const& subReply) -> bool {
            return subReply.isMember(jss::result) && subReply[jss::result].isMember(jss::status) &&
                subReply[jss::result][jss::status] == jss::success;
        };

        /*
         * try to receive txns from the tx stream subscription via the WSClient.
         * return {true, true} if received numReplies replies and also
         * received a tx with the account_history_tx_first == true
         */
        auto getTxHash = [](WSClient& wsc,
                            IdxHashVec& v,
                            int numReplies,
                            std::chrono::milliseconds timeout =
                                std::chrono::milliseconds{5000}) -> std::pair<bool, bool> {
            bool firstFlag = false;

            for (int i = 0; i < numReplies; ++i)
            {
                std::uint32_t idx{0};
                auto reply = wsc.getMsg(timeout);
                if (reply)
                {
                    auto r = *reply;
                    if (r.isMember(jss::account_history_tx_index))
                        idx = r[jss::account_history_tx_index].asInt();
                    if (r.isMember(jss::account_history_tx_first))
                        firstFlag = true;
                    bool const boundary = r.isMember(jss::account_history_boundary);
                    int const ledgerIdx = r[jss::ledger_index].asInt();
                    if (r.isMember(jss::transaction) && r[jss::transaction].isMember(jss::hash))
                    {
                        auto t{r[jss::transaction]};
                        v.emplace_back(idx, t[jss::hash].asString(), boundary, ledgerIdx);
                        continue;
                    }
                }
                return {false, firstFlag};
            }

            return {true, firstFlag};
        };

        /*
         * send payments between the two accounts a and b,
         * and close ledgersToClose ledgers
         */
        auto sendPayments = [this](
                                Env& env,
                                Account const& a,
                                Account const& b,
                                int newTxns,
                                std::uint32_t ledgersToClose,
                                int numXRP = 10) {
            env.memoize(a);
            env.memoize(b);
            for (int i = 0; i < newTxns; ++i)
            {
                auto& from = (i % 2 == 0) ? a : b;
                auto& to = (i % 2 == 0) ? b : a;
                env(pay(from, to, jtx::XRP(numXRP)),
                    jtx::Seq(jtx::kAutofill),
                    jtx::Fee(jtx::kAutofill),
                    jtx::Sig(jtx::kAutofill));
            }
            for (int i = 0; i < ledgersToClose; ++i)
                BEAST_EXPECT(env.syncClose());
            return newTxns;
        };

        /*
         * Check if txHistoryVec has every item of accountVec,
         * and in the same order.
         * If sizeCompare is false, txHistoryVec is allowed to be larger.
         */
        auto hashCompare = [](IdxHashVec const& accountVec,
                              IdxHashVec const& txHistoryVec,
                              bool sizeCompare) -> bool {
            if (accountVec.empty() || txHistoryVec.empty())
                return false;
            if (sizeCompare && accountVec.size() != (txHistoryVec.size()))
                return false;

            hash_map<std::string, int> txHistoryMap;
            for (auto const& tx : txHistoryVec)
            {
                txHistoryMap.emplace(std::get<1>(tx), std::get<0>(tx));
            }

            auto getHistoryIndex = [&](std::size_t i) -> std::optional<int> {
                if (i >= accountVec.size())
                    return {};
                auto it = txHistoryMap.find(std::get<1>(accountVec[i]));
                if (it == txHistoryMap.end())
                    return {};
                return it->second;
            };

            auto firstHistoryIndex = getHistoryIndex(0);
            if (!firstHistoryIndex)
                return false;
            for (std::size_t i = 1; i < accountVec.size(); ++i)
            {
                if (auto idx = getHistoryIndex(i); !idx || *idx != *firstHistoryIndex + i)
                    return false;
            }
            return true;
        };

        // example of vector created from the return of `subscribe` rpc
        // with jss::accounts
        // boundary == true on last tx of ledger
        // ------------------------------------------------------------
        // (0, "E5B8B...", false, 4
        // (0, "39E1C...", false, 4
        // (0, "14EF1...", false, 4
        // (0, "386E6...", false, 4
        // (0, "00F3B...", true,  4
        // (0, "1DCDC...", false, 5
        // (0, "BD02A...", false, 5
        // (0, "D3E16...", false, 5
        // (0, "CB593...", false, 5
        // (0, "8F28B...", true,  5
        //
        // example of vector created from the return of `subscribe` rpc
        // with jss::account_history_tx_stream.
        // boundary == true on first tx of ledger
        // ------------------------------------------------------------
        // (-1, "8F28B...", false, 5
        // (-2, "CB593...", false, 5
        // (-3, "D3E16...", false, 5
        // (-4, "BD02A...", false, 5
        // (-5, "1DCDC...", true,  5
        // (-6, "00F3B...", false, 4
        // (-7, "386E6...", false, 4
        // (-8, "14EF1...", false, 4
        // (-9, "39E1C...", false, 4
        // (-10, "E5B8B...", true, 4

        auto checkBoundary = [](IdxHashVec const& vec, bool /* forward */) {
            size_t const numTx = vec.size();
            for (size_t i = 0; i < numTx; ++i)
            {
                auto [idx, hash, boundary, ledger] = vec[i];
                if ((i + 1 == numTx || ledger != std::get<3>(vec[i + 1])) != boundary)
                    return false;
            }
            return true;
        };

        ///////////////////////////////////////////////////////////////////

        {
            /*
             * subscribe to an account twice with same WS client,
             * the second should fail
             *
             * also test subscribe to the account before it is created
             */
            Env env(*this, singleThreadIo(envconfig()));
            auto wscTxHistory = makeWSClient(env.app().config());
            json::Value request;
            request[jss::account_history_tx_stream] = json::ValueType::Object;
            request[jss::account_history_tx_stream][jss::account] = alice.human();
            auto jv = wscTxHistory->invoke("subscribe", request);
            if (!BEAST_EXPECT(goodSubRPC(jv)))
                return;

            jv = wscTxHistory->invoke("subscribe", request);
            BEAST_EXPECT(!goodSubRPC(jv));

            /*
             * unsubscribe history only, future txns should still be streamed
             */
            request[jss::account_history_tx_stream][jss::stop_history_tx_only] = true;
            jv = wscTxHistory->invoke("unsubscribe", request);
            if (!BEAST_EXPECT(goodSubRPC(jv)))
                return;

            sendPayments(env, env.master, alice, 1, 1, 123456);

            IdxHashVec vec;
            auto r = getTxHash(*wscTxHistory, vec, 1);
            if (!BEAST_EXPECT(r.first && r.second))
                return;

            /*
             * unsubscribe, future txns should not be streamed
             */
            request[jss::account_history_tx_stream][jss::stop_history_tx_only] = false;
            jv = wscTxHistory->invoke("unsubscribe", request);
            BEAST_EXPECT(goodSubRPC(jv));

            sendPayments(env, env.master, alice, 1, 1);
            r = getTxHash(*wscTxHistory, vec, 1, 10ms);
            BEAST_EXPECT(!r.first);
        }
        {
            /*
             * subscribe genesis account tx history without txns
             * subscribe to bob's account after it is created
             */
            Env env(*this, singleThreadIo(envconfig()));
            auto wscTxHistory = makeWSClient(env.app().config());
            json::Value request;
            request[jss::account_history_tx_stream] = json::ValueType::Object;
            request[jss::account_history_tx_stream][jss::account] =
                "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            auto jv = wscTxHistory->invoke("subscribe", request);
            if (!BEAST_EXPECT(goodSubRPC(jv)))
                return;
            IdxHashVec genesisFullHistoryVec;
            BEAST_EXPECT(env.syncClose());
            if (!BEAST_EXPECT(!getTxHash(*wscTxHistory, genesisFullHistoryVec, 1, 10ms).first))
                return;

            /*
             * create bob's account with one tx
             * the two subscriptions should both stream it
             */
            sendPayments(env, env.master, bob, 1, 1, 654321);

            auto r = getTxHash(*wscTxHistory, genesisFullHistoryVec, 1);
            if (!BEAST_EXPECT(r.first && r.second))
                return;

            request[jss::account_history_tx_stream][jss::account] = bob.human();
            jv = wscTxHistory->invoke("subscribe", request);
            if (!BEAST_EXPECT(goodSubRPC(jv)))
                return;
            IdxHashVec bobFullHistoryVec;
            BEAST_EXPECT(env.syncClose());
            r = getTxHash(*wscTxHistory, bobFullHistoryVec, 1);
            if (!BEAST_EXPECT(r.first && r.second))
                return;
            BEAST_EXPECT(
                std::get<1>(bobFullHistoryVec.back()) == std::get<1>(genesisFullHistoryVec.back()));

            /*
             * unsubscribe to prepare next test
             */
            jv = wscTxHistory->invoke("unsubscribe", request);
            if (!BEAST_EXPECT(goodSubRPC(jv)))
                return;
            request[jss::account_history_tx_stream][jss::account] =
                "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            jv = wscTxHistory->invoke("unsubscribe", request);
            BEAST_EXPECT(goodSubRPC(jv));

            /*
             * add more txns, then subscribe bob tx history and
             * genesis account tx history. Their earliest txns should match.
             */
            sendPayments(env, env.master, bob, 30, 300);
            wscTxHistory = makeWSClient(env.app().config());
            request[jss::account_history_tx_stream][jss::account] = bob.human();
            jv = wscTxHistory->invoke("subscribe", request);

            bobFullHistoryVec.clear();
            BEAST_EXPECT(getTxHash(*wscTxHistory, bobFullHistoryVec, 31).second);
            jv = wscTxHistory->invoke("unsubscribe", request);

            request[jss::account_history_tx_stream][jss::account] =
                "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            jv = wscTxHistory->invoke("subscribe", request);
            genesisFullHistoryVec.clear();
            BEAST_EXPECT(env.syncClose());
            BEAST_EXPECT(getTxHash(*wscTxHistory, genesisFullHistoryVec, 31).second);
            jv = wscTxHistory->invoke("unsubscribe", request);

            BEAST_EXPECT(
                std::get<1>(bobFullHistoryVec.back()) == std::get<1>(genesisFullHistoryVec.back()));
        }

        {
            /*
             * subscribe account and subscribe account tx history
             * and compare txns streamed
             */
            Env env(*this, singleThreadIo(envconfig()));
            auto wscAccount = makeWSClient(env.app().config());
            auto wscTxHistory = makeWSClient(env.app().config());

            std::array<Account, 2> const accounts = {alice, bob};
            env.fund(XRP(222222), accounts);
            BEAST_EXPECT(env.syncClose());

            // subscribe account
            json::Value stream = json::ValueType::Object;
            stream[jss::accounts] = json::ValueType::Array;
            stream[jss::accounts].append(alice.human());
            auto jv = wscAccount->invoke("subscribe", stream);

            sendPayments(env, alice, bob, 5, 1);
            sendPayments(env, alice, bob, 5, 1);
            IdxHashVec accountVec;
            if (!BEAST_EXPECT(getTxHash(*wscAccount, accountVec, 10).first))
                return;

            // subscribe account tx history
            json::Value request;
            request[jss::account_history_tx_stream] = json::ValueType::Object;
            request[jss::account_history_tx_stream][jss::account] = alice.human();
            jv = wscTxHistory->invoke("subscribe", request);

            // compare historical txns
            IdxHashVec txHistoryVec;
            if (!BEAST_EXPECT(getTxHash(*wscTxHistory, txHistoryVec, 10).first))
                return;
            if (!BEAST_EXPECT(hashCompare(accountVec, txHistoryVec, true)))
                return;

            // check boundary tags
            // only account_history_tx_stream has ledger boundary information.
            if (!BEAST_EXPECT(checkBoundary(txHistoryVec, false)))
                return;

            {
                // take out all history txns from stream to prepare next test
                IdxHashVec initFundTxns;
                if (!BEAST_EXPECT(getTxHash(*wscTxHistory, initFundTxns, 10).second) ||
                    !BEAST_EXPECT(checkBoundary(initFundTxns, false)))
                    return;
            }

            // compare future txns
            sendPayments(env, alice, bob, 10, 1);
            if (!BEAST_EXPECT(getTxHash(*wscAccount, accountVec, 10).first))
                return;
            if (!BEAST_EXPECT(getTxHash(*wscTxHistory, txHistoryVec, 10).first))
                return;
            if (!BEAST_EXPECT(hashCompare(accountVec, txHistoryVec, true)))
                return;

            // check boundary tags
            // only account_history_tx_stream has ledger boundary information.
            if (!BEAST_EXPECT(checkBoundary(txHistoryVec, false)))
                return;

            wscTxHistory->invoke("unsubscribe", request);
            wscAccount->invoke("unsubscribe", stream);
        }

        {
            /*
             * alice issues USD to carol
             * mix USD and XRP payments
             */
            Env env(*this, singleThreadIo(envconfig()));
            auto const usdA = alice["USD"];

            std::array<Account, 2> const accounts = {alice, carol};
            env.fund(XRP(333333), accounts);
            env.trust(usdA(20000), carol);
            BEAST_EXPECT(env.syncClose());

            auto mixedPayments = [&]() -> int {
                sendPayments(env, alice, carol, 1, 0);
                env(pay(alice, carol, usdA(100)));
                BEAST_EXPECT(env.syncClose());
                return 2;
            };

            // subscribe
            json::Value request;
            request[jss::account_history_tx_stream] = json::ValueType::Object;
            request[jss::account_history_tx_stream][jss::account] = carol.human();
            auto ws = makeWSClient(env.app().config());
            auto jv = ws->invoke("subscribe", request);
            BEAST_EXPECT(env.syncClose());
            {
                // take out existing txns from the stream
                IdxHashVec tempVec;
                getTxHash(*ws, tempVec, 100, 1000ms);
            }

            auto count = mixedPayments();
            IdxHashVec vec1;
            if (!BEAST_EXPECT(getTxHash(*ws, vec1, count).first))
                return;
            ws->invoke("unsubscribe", request);
        }

        {
            /*
             * long transaction history
             */
            Env env(*this, singleThreadIo(envconfig()));
            std::array<Account, 2> const accounts = {alice, carol};
            env.fund(XRP(444444), accounts);
            BEAST_EXPECT(env.syncClose());

            // many payments, and close lots of ledgers
            auto oneRound = [&](int numPayments) {
                return sendPayments(env, alice, carol, numPayments, 300);
            };

            // subscribe
            json::Value request;
            request[jss::account_history_tx_stream] = json::ValueType::Object;
            request[jss::account_history_tx_stream][jss::account] = carol.human();
            auto wscLong = makeWSClient(env.app().config());
            auto jv = wscLong->invoke("subscribe", request);
            BEAST_EXPECT(env.syncClose());
            {
                // take out existing txns from the stream
                IdxHashVec tempVec;
                getTxHash(*wscLong, tempVec, 100, 1000ms);
            }

            // repeat the payments many rounds
            for (int kk = 2; kk < 10; ++kk)
            {
                auto count = oneRound(kk);
                IdxHashVec vec1;
                if (!BEAST_EXPECT(getTxHash(*wscLong, vec1, count).first))
                    return;

                // another subscribe, only for this round
                auto wscShort = makeWSClient(env.app().config());
                auto jv = wscShort->invoke("subscribe", request);
                IdxHashVec vec2;
                if (!BEAST_EXPECT(getTxHash(*wscShort, vec2, count).first))
                    return;
                if (!BEAST_EXPECT(hashCompare(vec1, vec2, true)))
                    return;
                wscShort->invoke("unsubscribe", request);
            }
        }
    }

    void
    testSubBookChanges()
    {
        testcase("SubBookChanges");
        using namespace jtx;
        using namespace std::chrono_literals;
        FeatureBitset const all{
            jtx::testableAmendments() | featurePermissionedDomains | featureCredentials |
            featurePermissionedDEX};

        Env env(*this, singleThreadIo(envconfig()), all);
        PermissionedDEX const permDex(env);
        auto const alice = permDex.alice;
        auto const bob = permDex.bob;
        auto const carol = permDex.carol;
        auto const domainID = permDex.domainID;
        auto const gw = permDex.gw;
        auto const usd = permDex.usd;

        auto wsc = makeWSClient(env.app().config());

        json::Value streams;
        streams[jss::streams] = json::ValueType::Array;
        streams[jss::streams][0u] = "book_changes";

        auto jv = wsc->invoke("subscribe", streams);
        if (!BEAST_EXPECT(jv[jss::status] == "success"))
            return;
        env(offer(alice, XRP(10), usd(10)), Domain(domainID), Txflags(tfHybrid));
        BEAST_EXPECT(env.syncClose());

        env(pay(bob, carol, usd(5)), Path(~usd), Sendmax(XRP(5)), Domain(domainID));
        BEAST_EXPECT(env.syncClose());

        BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
            if (jv[jss::changes].size() != 1)
                return false;

            auto const jrOffer = jv[jss::changes][0u];
            return (jv[jss::changes][0u][jss::domain]).asString() == strHex(domainID) &&
                jrOffer[jss::currency_a].asString() == "XRP_drops" &&
                jrOffer[jss::volume_a].asString() == "5000000" &&
                jrOffer[jss::currency_b].asString() == "rHUKYAZyUFn8PCZWbPfwHfbVQXTYrYKkHb/USD" &&
                jrOffer[jss::volume_b].asString() == "5";
        }));
    }

    void
    testNFToken(FeatureBitset features)
    {
        // `nftoken_id` is added for `transaction` stream in the `subscribe`
        // response for NFTokenMint and NFTokenAcceptOffer.
        //
        // `nftoken_ids` is added for `transaction` stream in the `subscribe`
        // response for NFTokenCancelOffer
        //
        // `offer_id` is added for `transaction` stream in the `subscribe`
        // response for NFTokenCreateOffer
        //
        // The values of these fields are dependent on the NFTokenID/OfferID
        // changed in its corresponding transaction. We want to validate each
        // response to make sure the synthetic fields hold the right values.

        testcase("Test synthetic fields from Subscribe response");

        using namespace test::jtx;
        using namespace std::chrono_literals;

        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const broker{"broker"};

        Env env{*this, singleThreadIo(envconfig()), features};
        env.fund(XRP(10000), alice, bob, broker);
        BEAST_EXPECT(env.syncClose());

        auto wsc = test::makeWSClient(env.app().config());
        json::Value stream;
        stream[jss::streams] = json::ValueType::Array;
        stream[jss::streams].append("transactions");
        auto jv = wsc->invoke("subscribe", stream);

        // Verify `nftoken_id` value equals to the NFTokenID that was
        // changed in the most recent NFTokenMint or NFTokenAcceptOffer
        // transaction
        auto verifyNFTokenID = [&](uint256 const& actualNftID) {
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                uint256 nftID;
                BEAST_EXPECT(nftID.parseHex(jv[jss::meta][jss::nftoken_id].asString()));
                return nftID == actualNftID;
            }));
        };

        // Verify `nftoken_ids` value equals to the NFTokenIDs that were
        // changed in the most recent NFTokenCancelOffer transaction
        auto verifyNFTokenIDsInCancelOffer = [&](std::vector<uint256> actualNftIDs) {
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                std::vector<uint256> metaIDs;
                std::transform(
                    jv[jss::meta][jss::nftoken_ids].begin(),
                    jv[jss::meta][jss::nftoken_ids].end(),
                    std::back_inserter(metaIDs),
                    [this](json::Value id) {
                        uint256 nftID;
                        BEAST_EXPECT(nftID.parseHex(id.asString()));
                        return nftID;
                    });
                // Sort both array to prepare for comparison
                std::ranges::sort(metaIDs);
                std::ranges::sort(actualNftIDs);

                // Make sure the expect number of NFTs is correct
                BEAST_EXPECT(metaIDs.size() == actualNftIDs.size());

                // Check the value of NFT ID in the meta with the
                // actual values
                for (size_t i = 0; i < metaIDs.size(); ++i)
                    BEAST_EXPECT(metaIDs[i] == actualNftIDs[i]);
                return true;
            }));
        };

        // Verify `offer_id` value equals to the offerID that was
        // changed in the most recent NFTokenCreateOffer tx
        auto verifyNFTokenOfferID = [&](uint256 const& offerID) {
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                uint256 metaOfferID;
                BEAST_EXPECT(metaOfferID.parseHex(jv[jss::meta][jss::offer_id].asString()));
                return metaOfferID == offerID;
            }));
        };

        // Check new fields in tx meta when for all NFTtransactions
        {
            // Alice mints 2 NFTs
            // Verify the NFTokenIDs are correct in the NFTokenMint tx meta
            uint256 const nftId1{token::getNextID(env, alice, 0u, tfTransferable)};
            env(token::mint(alice, 0u), Txflags(tfTransferable));
            BEAST_EXPECT(env.syncClose());
            verifyNFTokenID(nftId1);

            uint256 const nftId2{token::getNextID(env, alice, 0u, tfTransferable)};
            env(token::mint(alice, 0u), Txflags(tfTransferable));
            BEAST_EXPECT(env.syncClose());
            verifyNFTokenID(nftId2);

            // Alice creates one sell offer for each NFT
            // Verify the offer indexes are correct in the NFTokenCreateOffer tx
            // meta
            uint256 const aliceOfferIndex1 = keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, nftId1, drops(1)), Txflags(tfSellNFToken));
            BEAST_EXPECT(env.syncClose());
            verifyNFTokenOfferID(aliceOfferIndex1);

            uint256 const aliceOfferIndex2 = keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, nftId2, drops(1)), Txflags(tfSellNFToken));
            BEAST_EXPECT(env.syncClose());
            verifyNFTokenOfferID(aliceOfferIndex2);

            // Alice cancels two offers she created
            // Verify the NFTokenIDs are correct in the NFTokenCancelOffer tx
            // meta
            env(token::cancelOffer(alice, {aliceOfferIndex1, aliceOfferIndex2}));
            BEAST_EXPECT(env.syncClose());
            verifyNFTokenIDsInCancelOffer({nftId1, nftId2});

            // Bobs creates a buy offer for nftId1
            // Verify the offer id is correct in the NFTokenCreateOffer tx meta
            auto const bobBuyOfferIndex = keylet::nftoffer(bob, env.seq(bob)).key;
            env(token::createOffer(bob, nftId1, drops(1)), token::Owner(alice));
            BEAST_EXPECT(env.syncClose());
            verifyNFTokenOfferID(bobBuyOfferIndex);

            // Alice accepts bob's buy offer
            // Verify the NFTokenID is correct in the NFTokenAcceptOffer tx meta
            env(token::acceptBuyOffer(alice, bobBuyOfferIndex));
            BEAST_EXPECT(env.syncClose());
            verifyNFTokenID(nftId1);
        }

        // Check `nftoken_ids` in brokered mode
        {
            // Alice mints a NFT
            uint256 const nftId{token::getNextID(env, alice, 0u, tfTransferable)};
            env(token::mint(alice, 0u), Txflags(tfTransferable));
            BEAST_EXPECT(env.syncClose());
            verifyNFTokenID(nftId);

            // Alice creates sell offer and set broker as destination
            uint256 const offerAliceToBroker = keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, nftId, drops(1)),
                token::Destination(broker),
                Txflags(tfSellNFToken));
            BEAST_EXPECT(env.syncClose());
            verifyNFTokenOfferID(offerAliceToBroker);

            // Bob creates buy offer
            uint256 const offerBobToBroker = keylet::nftoffer(bob, env.seq(bob)).key;
            env(token::createOffer(bob, nftId, drops(1)), token::Owner(alice));
            BEAST_EXPECT(env.syncClose());
            verifyNFTokenOfferID(offerBobToBroker);

            // Check NFTokenID meta for NFTokenAcceptOffer in brokered mode
            env(token::brokerOffers(broker, offerBobToBroker, offerAliceToBroker));
            BEAST_EXPECT(env.syncClose());
            verifyNFTokenID(nftId);
        }

        // Check if there are no duplicate nft id in Cancel transactions where
        // multiple offers are cancelled for the same NFT
        {
            // Alice mints a NFT
            uint256 const nftId{token::getNextID(env, alice, 0u, tfTransferable)};
            env(token::mint(alice, 0u), Txflags(tfTransferable));
            BEAST_EXPECT(env.syncClose());
            verifyNFTokenID(nftId);

            // Alice creates 2 sell offers for the same NFT
            uint256 const aliceOfferIndex1 = keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, nftId, drops(1)), Txflags(tfSellNFToken));
            BEAST_EXPECT(env.syncClose());
            verifyNFTokenOfferID(aliceOfferIndex1);

            uint256 const aliceOfferIndex2 = keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, nftId, drops(1)), Txflags(tfSellNFToken));
            BEAST_EXPECT(env.syncClose());
            verifyNFTokenOfferID(aliceOfferIndex2);

            // Make sure the metadata only has 1 nft id, since both offers are
            // for the same nft
            env(token::cancelOffer(alice, {aliceOfferIndex1, aliceOfferIndex2}));
            BEAST_EXPECT(env.syncClose());
            verifyNFTokenIDsInCancelOffer({nftId});
        }

        if (features[featureNFTokenMintOffer])
        {
            uint256 const aliceMintWithOfferIndex1 = keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::mint(alice), token::Amount(XRP(0)));
            BEAST_EXPECT(env.syncClose());
            verifyNFTokenOfferID(aliceMintWithOfferIndex1);
        }
    }

    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{testableAmendments()};
        FeatureBitset const xrpFees{featureXRPFees};

        testServer();
        testLedger();
        testTransactionsAPIv1();
        testTransactionsAPIv2();
        testManifests();
        testValidations(all - xrpFees);
        testValidations(all);
        testSubErrors(true);
        testSubErrors(false);
        testSubByUrl();
        testHistoryTxStream();
        testSubBookChanges();
        testNFToken(all);
        testNFToken(all - featureNFTokenMintOffer);
    }
};

BEAST_DEFINE_TESTSUITE(Subscribe, rpc, xrpl);

}  // namespace xrpl::test
