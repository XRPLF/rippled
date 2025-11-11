#include <test/jtx.h>
#include <test/jtx/WSClient.h>
#include <test/jtx/envconfig.h>

#include <xrpld/app/main/LoadManager.h>
#include <xrpld/app/misc/LoadFeeTrack.h>
#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/core/ConfigSections.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/jss.h>

#include <tuple>

namespace ripple {
namespace test {

class Subscribe_test : public beast::unit_test::suite
{
public:
    void
    testServer()
    {
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
        auto wsc = makeWSClient(env.app().config());
        Json::Value stream;

        {
            // RPC subscribe to server stream
            stream[jss::streams] = Json::arrayValue;
            stream[jss::streams].append("server");
            auto jv = wsc->invoke("subscribe", stream);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(
                    jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
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
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                return jv[jss::type] == "serverStatus";
            }));
        }

        {
            // RPC unsubscribe
            auto jv = wsc->invoke("unsubscribe", stream);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(
                    jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
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
        Env env(*this);
        auto wsc = makeWSClient(env.app().config());
        Json::Value stream;

        {
            // RPC subscribe to ledger stream
            stream[jss::streams] = Json::arrayValue;
            stream[jss::streams].append("ledger");
            auto jv = wsc->invoke("subscribe", stream);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(
                    jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::result][jss::ledger_index] == 2);
            BEAST_EXPECT(
                jv[jss::result][jss::network_id] ==
                env.app().config().NETWORK_ID);
        }

        {
            // Accept a ledger
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                return jv[jss::ledger_index] == 3 &&
                    jv[jss::network_id] == env.app().config().NETWORK_ID;
            }));
        }

        {
            // Accept another ledger
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                return jv[jss::ledger_index] == 4 &&
                    jv[jss::network_id] == env.app().config().NETWORK_ID;
            }));
        }

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", stream);
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(
                jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
        }
        BEAST_EXPECT(jv[jss::status] == "success");
    }

    void
    testTransactions_APIv1()
    {
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
        auto baseFee = env.current()->fees().base.drops();
        auto wsc = makeWSClient(env.app().config());
        Json::Value stream;

        {
            // RPC subscribe to transactions stream
            stream[jss::streams] = Json::arrayValue;
            stream[jss::streams].append("transactions");
            auto jv = wsc->invoke("subscribe", stream);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(
                    jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::status] == "success");
        }

        {
            env.fund(XRP(10000), "alice");
            env.close();

            // Check stream update for payment transaction
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                return jv[jss::meta]["AffectedNodes"][1u]["CreatedNode"]
                         ["NewFields"][jss::Account]  //
                    == Account("alice").human() &&
                    jv[jss::transaction][jss::TransactionType]  //
                    == jss::Payment &&
                    jv[jss::transaction][jss::DeliverMax]  //
                    == std::to_string(10000000000 + baseFee) &&
                    jv[jss::transaction][jss::Fee]  //
                    == std::to_string(baseFee) &&
                    jv[jss::transaction][jss::Sequence]  //
                    == 1;
            }));

            // Check stream update for accountset transaction
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                return jv[jss::meta]["AffectedNodes"][0u]["ModifiedNode"]
                         ["FinalFields"][jss::Account] ==
                    Account("alice").human();
            }));

            env.fund(XRP(10000), "bob");
            env.close();

            // Check stream update for payment transaction
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                return jv[jss::meta]["AffectedNodes"][1u]["CreatedNode"]
                         ["NewFields"][jss::Account]  //
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
                return jv[jss::meta]["AffectedNodes"][0u]["ModifiedNode"]
                         ["FinalFields"][jss::Account] ==
                    Account("bob").human();
            }));
        }

        {
            // RPC unsubscribe
            auto jv = wsc->invoke("unsubscribe", stream);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(
                    jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::status] == "success");
        }

        {
            // RPC subscribe to accounts stream
            stream = Json::objectValue;
            stream[jss::accounts] = Json::arrayValue;
            stream[jss::accounts].append(Account("alice").human());
            auto jv = wsc->invoke("subscribe", stream);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(
                    jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::status] == "success");
        }

        {
            // Transaction that does not affect stream
            env.fund(XRP(10000), "carol");
            env.close();
            BEAST_EXPECT(!wsc->getMsg(10ms));

            // Transactions concerning alice
            env.trust(Account("bob")["USD"](100), "alice");
            env.close();

            // Check stream updates
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                return jv[jss::meta]["AffectedNodes"][1u]["ModifiedNode"]
                         ["FinalFields"][jss::Account] ==
                    Account("alice").human();
            }));

            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                return jv[jss::meta]["AffectedNodes"][1u]["CreatedNode"]
                         ["NewFields"]["LowLimit"][jss::issuer] ==
                    Account("alice").human();
            }));
        }

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", stream);
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(
                jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
        }
        BEAST_EXPECT(jv[jss::status] == "success");
    }

    void
    testTransactions_APIv2()
    {
        testcase("transactions API version 2");

        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->FEES.reference_fee = 10;
            return cfg;
        }));
        auto wsc = makeWSClient(env.app().config());
        Json::Value stream{Json::objectValue};

        {
            // RPC subscribe to transactions stream
            stream[jss::api_version] = 2;
            stream[jss::streams] = Json::arrayValue;
            stream[jss::streams].append("transactions");
            auto jv = wsc->invoke("subscribe", stream);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(
                    jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::status] == "success");
        }

        {
            env.fund(XRP(10000), "alice");
            env.close();

            // Check stream update for payment transaction
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                return jv[jss::meta]["AffectedNodes"][1u]["CreatedNode"]
                         ["NewFields"][jss::Account]  //
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
                return jv[jss::meta]["AffectedNodes"][0u]["ModifiedNode"]
                         ["FinalFields"][jss::Account] ==
                    Account("alice").human();
            }));
        }

        {
            // RPC unsubscribe
            auto jv = wsc->invoke("unsubscribe", stream);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(
                    jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::status] == "success");
        }
    }

    void
    testManifests()
    {
        using namespace jtx;
        Env env(*this);
        auto wsc = makeWSClient(env.app().config());
        Json::Value stream;

        {
            // RPC subscribe to manifests stream
            stream[jss::streams] = Json::arrayValue;
            stream[jss::streams].append("manifests");
            auto jv = wsc->invoke("subscribe", stream);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(
                    jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::status] == "success");
        }

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", stream);
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(
                jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
        }
        BEAST_EXPECT(jv[jss::status] == "success");
    }

    void
    testValidations(FeatureBitset features)
    {
        using namespace jtx;

        Env env{*this, envconfig(validator, ""), features};
        auto& cfg = env.app().config();
        if (!BEAST_EXPECT(cfg.section(SECTION_VALIDATION_SEED).empty()))
            return;
        auto const parsedseed =
            parseBase58<Seed>(cfg.section(SECTION_VALIDATION_SEED).values()[0]);
        if (!BEAST_EXPECT(parsedseed))
            return;

        std::string const valPublicKey = toBase58(
            TokenType::NodePublic,
            derivePublicKey(
                KeyType::secp256k1,
                generateSecretKey(KeyType::secp256k1, *parsedseed)));

        auto wsc = makeWSClient(env.app().config());
        Json::Value stream;

        {
            // RPC subscribe to validations stream
            stream[jss::streams] = Json::arrayValue;
            stream[jss::streams].append("validations");
            auto jv = wsc->invoke("subscribe", stream);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(
                    jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::status] == "success");
        }

        {
            // Lambda to check ledger validations from the stream.
            auto validValidationFields = [&env, &valPublicKey](
                                             Json::Value const& jv) {
                if (jv[jss::type] != "validationReceived")
                    return false;

                if (jv[jss::validation_public_key].asString() != valPublicKey)
                    return false;

                if (jv[jss::ledger_hash] !=
                    to_string(env.closed()->info().hash))
                    return false;

                if (jv[jss::ledger_index] !=
                    std::to_string(env.closed()->info().seq))
                    return false;

                if (jv[jss::flags] != (vfFullyCanonicalSig | vfFullValidation))
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

                uint32_t netID = env.app().config().NETWORK_ID;
                if (!jv.isMember(jss::network_id) ||
                    jv[jss::network_id] != netID)
                    return false;

                // Certain fields are only added on a flag ledger.
                bool const isFlagLedger =
                    (env.closed()->info().seq + 1) % 256 == 0;

                if (jv.isMember(jss::server_version) != isFlagLedger)
                    return false;

                if (jv.isMember(jss::reserve_base) != isFlagLedger)
                    return false;

                if (jv.isMember(jss::reserve_inc) != isFlagLedger)
                    return false;

                if (env.closed()->rules().enabled(featureSmartEscrow))
                {
                    if (jv.isMember(jss::extension_compute) != isFlagLedger)
                        return false;

                    if (jv.isMember(jss::extension_size) != isFlagLedger)
                        return false;
                }
                else
                {
                    if (jv.isMember(jss::extension_compute))
                        return false;

                    if (jv.isMember(jss::extension_size))
                        return false;
                }
                return true;
            };

            // Check stream update.  Look at enough stream entries so we see
            // at least one flag ledger.
            while (env.closed()->info().seq < 300)
            {
                env.close();
                using namespace std::chrono_literals;
                BEAST_EXPECT(wsc->findMsg(5s, validValidationFields));
            }
        }

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", stream);
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(
                jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
        }
        BEAST_EXPECT(jv[jss::status] == "success");
    }

    void
    testSubByUrl()
    {
        using namespace jtx;
        testcase("Subscribe by url");
        Env env{*this};

        Json::Value jv;
        jv[jss::url] = "http://localhost/events";
        jv[jss::url_username] = "admin";
        jv[jss::url_password] = "password";
        jv[jss::streams] = Json::arrayValue;
        jv[jss::streams][0u] = "validations";
        auto jr = env.rpc("json", "subscribe", to_string(jv))[jss::result];
        BEAST_EXPECT(jr[jss::status] == "success");

        jv[jss::streams][0u] = "ledger";
        jr = env.rpc("json", "subscribe", to_string(jv))[jss::result];
        BEAST_EXPECT(jr[jss::status] == "success");
        BEAST_EXPECT(jr[jss::network_id] == env.app().config().NETWORK_ID);

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

        Env env{*this};
        auto wsc = makeWSClient(env.app().config());

        {
            auto jr = env.rpc("json", method, "{}")[jss::result];
            BEAST_EXPECT(jr[jss::error] == "invalidParams");
            BEAST_EXPECT(jr[jss::error_message] == "Invalid parameters.");
        }

        {
            Json::Value jv;
            jv[jss::url] = "not-a-url";
            jv[jss::username] = "admin";
            jv[jss::password] = "password";
            auto jr = env.rpc("json", method, to_string(jv))[jss::result];
            if (subscribe)
            {
                BEAST_EXPECT(jr[jss::error] == "invalidParams");
                BEAST_EXPECT(jr[jss::error_message] == "Failed to parse url.");
            }
            // else TODO: why isn't this an error for unsubscribe ?
            // (findRpcSub returns null)
        }

        {
            Json::Value jv;
            jv[jss::url] = "ftp://scheme.not.supported.tld";
            auto jr = env.rpc("json", method, to_string(jv))[jss::result];
            if (subscribe)
            {
                BEAST_EXPECT(jr[jss::error] == "invalidParams");
                BEAST_EXPECT(
                    jr[jss::error_message] ==
                    "Only http and https is supported.");
            }
        }

        {
            Env env_nonadmin{*this, no_admin(envconfig())};
            Json::Value jv;
            jv[jss::url] = "no-url";
            auto jr =
                env_nonadmin.rpc("json", method, to_string(jv))[jss::result];
            BEAST_EXPECT(jr[jss::error] == "noPermission");
            BEAST_EXPECT(
                jr[jss::error_message] ==
                "You don't have permission for this command.");
        }

        std::initializer_list<Json::Value> const nonArrays{
            Json::nullValue,
            Json::intValue,
            Json::uintValue,
            Json::realValue,
            "",
            Json::booleanValue,
            Json::objectValue};

        for (auto const& f : {jss::accounts_proposed, jss::accounts})
        {
            for (auto const& nonArray : nonArrays)
            {
                Json::Value jv;
                jv[f] = nonArray;
                auto jr = wsc->invoke(method, jv)[jss::result];
                BEAST_EXPECT(jr[jss::error] == "invalidParams");
                BEAST_EXPECT(jr[jss::error_message] == "Invalid parameters.");
            }

            {
                Json::Value jv;
                jv[f] = Json::arrayValue;
                auto jr = wsc->invoke(method, jv)[jss::result];
                BEAST_EXPECT(jr[jss::error] == "actMalformed");
                BEAST_EXPECT(jr[jss::error_message] == "Account malformed.");
            }
        }

        for (auto const& nonArray : nonArrays)
        {
            Json::Value jv;
            jv[jss::books] = nonArray;
            auto jr = wsc->invoke(method, jv)[jss::result];
            BEAST_EXPECT(jr[jss::error] == "invalidParams");
            BEAST_EXPECT(jr[jss::error_message] == "Invalid parameters.");
        }

        {
            Json::Value jv;
            jv[jss::books] = Json::arrayValue;
            jv[jss::books][0u] = 1;
            auto jr = wsc->invoke(method, jv)[jss::result];
            BEAST_EXPECT(jr[jss::error] == "invalidParams");
            BEAST_EXPECT(jr[jss::error_message] == "Invalid parameters.");
        }

        {
            Json::Value jv;
            jv[jss::books] = Json::arrayValue;
            jv[jss::books][0u] = Json::objectValue;
            jv[jss::books][0u][jss::taker_gets] = Json::objectValue;
            jv[jss::books][0u][jss::taker_pays] = Json::objectValue;
            auto jr = wsc->invoke(method, jv)[jss::result];
            BEAST_EXPECT(jr[jss::error] == "srcCurMalformed");
            BEAST_EXPECT(
                jr[jss::error_message] == "Source currency is malformed.");
        }

        {
            Json::Value jv;
            jv[jss::books] = Json::arrayValue;
            jv[jss::books][0u] = Json::objectValue;
            jv[jss::books][0u][jss::taker_gets] = Json::objectValue;
            jv[jss::books][0u][jss::taker_pays] = Json::objectValue;
            jv[jss::books][0u][jss::taker_pays][jss::currency] = "ZZZZ";
            auto jr = wsc->invoke(method, jv)[jss::result];
            BEAST_EXPECT(jr[jss::error] == "srcCurMalformed");
            BEAST_EXPECT(
                jr[jss::error_message] == "Source currency is malformed.");
        }

        {
            Json::Value jv;
            jv[jss::books] = Json::arrayValue;
            jv[jss::books][0u] = Json::objectValue;
            jv[jss::books][0u][jss::taker_gets] = Json::objectValue;
            jv[jss::books][0u][jss::taker_pays] = Json::objectValue;
            jv[jss::books][0u][jss::taker_pays][jss::currency] = "USD";
            jv[jss::books][0u][jss::taker_pays][jss::issuer] = 1;
            auto jr = wsc->invoke(method, jv)[jss::result];
            BEAST_EXPECT(jr[jss::error] == "srcIsrMalformed");
            BEAST_EXPECT(
                jr[jss::error_message] == "Source issuer is malformed.");
        }

        {
            Json::Value jv;
            jv[jss::books] = Json::arrayValue;
            jv[jss::books][0u] = Json::objectValue;
            jv[jss::books][0u][jss::taker_gets] = Json::objectValue;
            jv[jss::books][0u][jss::taker_pays] = Json::objectValue;
            jv[jss::books][0u][jss::taker_pays][jss::currency] = "USD";
            jv[jss::books][0u][jss::taker_pays][jss::issuer] =
                Account{"gateway"}.human() + "DEAD";
            auto jr = wsc->invoke(method, jv)[jss::result];
            BEAST_EXPECT(jr[jss::error] == "srcIsrMalformed");
            BEAST_EXPECT(
                jr[jss::error_message] == "Source issuer is malformed.");
        }

        {
            Json::Value jv;
            jv[jss::books] = Json::arrayValue;
            jv[jss::books][0u] = Json::objectValue;
            jv[jss::books][0u][jss::taker_pays] =
                Account{"gateway"}["USD"](1).value().getJson(
                    JsonOptions::include_date);
            jv[jss::books][0u][jss::taker_gets] = Json::objectValue;
            auto jr = wsc->invoke(method, jv)[jss::result];
            // NOTE: this error is slightly incongruous with the
            // equivalent source currency error
            BEAST_EXPECT(jr[jss::error] == "dstAmtMalformed");
            BEAST_EXPECT(
                jr[jss::error_message] ==
                "Destination amount/currency/issuer is malformed.");
        }

        {
            Json::Value jv;
            jv[jss::books] = Json::arrayValue;
            jv[jss::books][0u] = Json::objectValue;
            jv[jss::books][0u][jss::taker_pays] =
                Account{"gateway"}["USD"](1).value().getJson(
                    JsonOptions::include_date);
            jv[jss::books][0u][jss::taker_gets][jss::currency] = "ZZZZ";
            auto jr = wsc->invoke(method, jv)[jss::result];
            // NOTE: this error is slightly incongruous with the
            // equivalent source currency error
            BEAST_EXPECT(jr[jss::error] == "dstAmtMalformed");
            BEAST_EXPECT(
                jr[jss::error_message] ==
                "Destination amount/currency/issuer is malformed.");
        }

        {
            Json::Value jv;
            jv[jss::books] = Json::arrayValue;
            jv[jss::books][0u] = Json::objectValue;
            jv[jss::books][0u][jss::taker_pays] =
                Account{"gateway"}["USD"](1).value().getJson(
                    JsonOptions::include_date);
            jv[jss::books][0u][jss::taker_gets][jss::currency] = "USD";
            jv[jss::books][0u][jss::taker_gets][jss::issuer] = 1;
            auto jr = wsc->invoke(method, jv)[jss::result];
            BEAST_EXPECT(jr[jss::error] == "dstIsrMalformed");
            BEAST_EXPECT(
                jr[jss::error_message] == "Destination issuer is malformed.");
        }

        {
            Json::Value jv;
            jv[jss::books] = Json::arrayValue;
            jv[jss::books][0u] = Json::objectValue;
            jv[jss::books][0u][jss::taker_pays] =
                Account{"gateway"}["USD"](1).value().getJson(
                    JsonOptions::include_date);
            jv[jss::books][0u][jss::taker_gets][jss::currency] = "USD";
            jv[jss::books][0u][jss::taker_gets][jss::issuer] =
                Account{"gateway"}.human() + "DEAD";
            auto jr = wsc->invoke(method, jv)[jss::result];
            BEAST_EXPECT(jr[jss::error] == "dstIsrMalformed");
            BEAST_EXPECT(
                jr[jss::error_message] == "Destination issuer is malformed.");
        }

        {
            Json::Value jv;
            jv[jss::books] = Json::arrayValue;
            jv[jss::books][0u] = Json::objectValue;
            jv[jss::books][0u][jss::taker_pays] =
                Account{"gateway"}["USD"](1).value().getJson(
                    JsonOptions::include_date);
            jv[jss::books][0u][jss::taker_gets] =
                Account{"gateway"}["USD"](1).value().getJson(
                    JsonOptions::include_date);
            auto jr = wsc->invoke(method, jv)[jss::result];
            BEAST_EXPECT(jr[jss::error] == "badMarket");
            BEAST_EXPECT(jr[jss::error_message] == "No such market.");
        }

        for (auto const& nonArray : nonArrays)
        {
            Json::Value jv;
            jv[jss::streams] = nonArray;
            auto jr = wsc->invoke(method, jv)[jss::result];
            BEAST_EXPECT(jr[jss::error] == "invalidParams");
            BEAST_EXPECT(jr[jss::error_message] == "Invalid parameters.");
        }

        {
            Json::Value jv;
            jv[jss::streams] = Json::arrayValue;
            jv[jss::streams][0u] = 1;
            auto jr = wsc->invoke(method, jv)[jss::result];
            BEAST_EXPECT(jr[jss::error] == "malformedStream");
            BEAST_EXPECT(jr[jss::error_message] == "Stream malformed.");
        }

        {
            Json::Value jv;
            jv[jss::streams] = Json::arrayValue;
            jv[jss::streams][0u] = "not_a_stream";
            auto jr = wsc->invoke(method, jv)[jss::result];
            BEAST_EXPECT(jr[jss::error] == "malformedStream");
            BEAST_EXPECT(jr[jss::error_message] == "Stream malformed.");
        }
    }

    void
    testHistoryTxStream()
    {
        testcase("HistoryTxStream");

        using namespace std::chrono_literals;
        using namespace jtx;
        using IdxHashVec = std::vector<std::tuple<int, std::string, bool, int>>;

        Account alice("alice");
        Account bob("bob");
        Account carol("carol");
        Account david("david");
        ///////////////////////////////////////////////////////////////////

        /*
         * return true if the subscribe or unsubscribe result is a success
         */
        auto goodSubRPC = [](Json::Value const& subReply) -> bool {
            return subReply.isMember(jss::result) &&
                subReply[jss::result].isMember(jss::status) &&
                subReply[jss::result][jss::status] == jss::success;
        };

        /*
         * try to receive txns from the tx stream subscription via the WSClient.
         * return {true, true} if received numReplies replies and also
         * received a tx with the account_history_tx_first == true
         */
        auto getTxHash = [](WSClient& wsc,
                            IdxHashVec& v,
                            int numReplies) -> std::pair<bool, bool> {
            bool first_flag = false;

            for (int i = 0; i < numReplies; ++i)
            {
                std::uint32_t idx{0};
                auto reply = wsc.getMsg(100ms);
                if (reply)
                {
                    auto r = *reply;
                    if (r.isMember(jss::account_history_tx_index))
                        idx = r[jss::account_history_tx_index].asInt();
                    if (r.isMember(jss::account_history_tx_first))
                        first_flag = true;
                    bool boundary = r.isMember(jss::account_history_boundary);
                    int ledger_idx = r[jss::ledger_index].asInt();
                    if (r.isMember(jss::transaction) &&
                        r[jss::transaction].isMember(jss::hash))
                    {
                        auto t{r[jss::transaction]};
                        v.emplace_back(
                            idx, t[jss::hash].asString(), boundary, ledger_idx);
                        continue;
                    }
                }
                return {false, first_flag};
            }

            return {true, first_flag};
        };

        /*
         * send payments between the two accounts a and b,
         * and close ledgersToClose ledgers
         */
        auto sendPayments = [](Env& env,
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
                env.apply(
                    pay(from, to, jtx::XRP(numXRP)),
                    jtx::seq(jtx::autofill),
                    jtx::fee(jtx::autofill),
                    jtx::sig(jtx::autofill));
            }
            for (int i = 0; i < ledgersToClose; ++i)
                env.close();
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
                if (auto idx = getHistoryIndex(i);
                    !idx || *idx != *firstHistoryIndex + i)
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
            size_t num_tx = vec.size();
            for (size_t i = 0; i < num_tx; ++i)
            {
                auto [idx, hash, boundary, ledger] = vec[i];
                if ((i + 1 == num_tx || ledger != std::get<3>(vec[i + 1])) !=
                    boundary)
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
            Env env(*this);
            auto wscTxHistory = makeWSClient(env.app().config());
            Json::Value request;
            request[jss::account_history_tx_stream] = Json::objectValue;
            request[jss::account_history_tx_stream][jss::account] =
                alice.human();
            auto jv = wscTxHistory->invoke("subscribe", request);
            if (!BEAST_EXPECT(goodSubRPC(jv)))
                return;

            jv = wscTxHistory->invoke("subscribe", request);
            BEAST_EXPECT(!goodSubRPC(jv));

            /*
             * unsubscribe history only, future txns should still be streamed
             */
            request[jss::account_history_tx_stream][jss::stop_history_tx_only] =
                true;
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
            request[jss::account_history_tx_stream][jss::stop_history_tx_only] =
                false;
            jv = wscTxHistory->invoke("unsubscribe", request);
            BEAST_EXPECT(goodSubRPC(jv));

            sendPayments(env, env.master, alice, 1, 1);
            r = getTxHash(*wscTxHistory, vec, 1);
            BEAST_EXPECT(!r.first);
        }
        {
            /*
             * subscribe genesis account tx history without txns
             * subscribe to bob's account after it is created
             */
            Env env(*this);
            auto wscTxHistory = makeWSClient(env.app().config());
            Json::Value request;
            request[jss::account_history_tx_stream] = Json::objectValue;
            request[jss::account_history_tx_stream][jss::account] =
                "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            auto jv = wscTxHistory->invoke("subscribe", request);
            if (!BEAST_EXPECT(goodSubRPC(jv)))
                return;
            IdxHashVec genesisFullHistoryVec;
            if (!BEAST_EXPECT(
                    !getTxHash(*wscTxHistory, genesisFullHistoryVec, 1).first))
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
            r = getTxHash(*wscTxHistory, bobFullHistoryVec, 1);
            if (!BEAST_EXPECT(r.first && r.second))
                return;
            BEAST_EXPECT(
                std::get<1>(bobFullHistoryVec.back()) ==
                std::get<1>(genesisFullHistoryVec.back()));

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
            BEAST_EXPECT(
                getTxHash(*wscTxHistory, bobFullHistoryVec, 31).second);
            jv = wscTxHistory->invoke("unsubscribe", request);

            request[jss::account_history_tx_stream][jss::account] =
                "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            jv = wscTxHistory->invoke("subscribe", request);
            genesisFullHistoryVec.clear();
            BEAST_EXPECT(
                getTxHash(*wscTxHistory, genesisFullHistoryVec, 31).second);
            jv = wscTxHistory->invoke("unsubscribe", request);

            BEAST_EXPECT(
                std::get<1>(bobFullHistoryVec.back()) ==
                std::get<1>(genesisFullHistoryVec.back()));
        }

        {
            /*
             * subscribe account and subscribe account tx history
             * and compare txns streamed
             */
            Env env(*this);
            auto wscAccount = makeWSClient(env.app().config());
            auto wscTxHistory = makeWSClient(env.app().config());

            std::array<Account, 2> accounts = {alice, bob};
            env.fund(XRP(222222), accounts);
            env.close();

            // subscribe account
            Json::Value stream = Json::objectValue;
            stream[jss::accounts] = Json::arrayValue;
            stream[jss::accounts].append(alice.human());
            auto jv = wscAccount->invoke("subscribe", stream);

            sendPayments(env, alice, bob, 5, 1);
            sendPayments(env, alice, bob, 5, 1);
            IdxHashVec accountVec;
            if (!BEAST_EXPECT(getTxHash(*wscAccount, accountVec, 10).first))
                return;

            // subscribe account tx history
            Json::Value request;
            request[jss::account_history_tx_stream] = Json::objectValue;
            request[jss::account_history_tx_stream][jss::account] =
                alice.human();
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
                if (!BEAST_EXPECT(
                        getTxHash(*wscTxHistory, initFundTxns, 10).second) ||
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
            Env env(*this);
            auto const USD_a = alice["USD"];

            std::array<Account, 2> accounts = {alice, carol};
            env.fund(XRP(333333), accounts);
            env.trust(USD_a(20000), carol);
            env.close();

            auto mixedPayments = [&]() -> int {
                sendPayments(env, alice, carol, 1, 0);
                env(pay(alice, carol, USD_a(100)));
                env.close();
                return 2;
            };

            // subscribe
            Json::Value request;
            request[jss::account_history_tx_stream] = Json::objectValue;
            request[jss::account_history_tx_stream][jss::account] =
                carol.human();
            auto ws = makeWSClient(env.app().config());
            auto jv = ws->invoke("subscribe", request);
            {
                // take out existing txns from the stream
                IdxHashVec tempVec;
                getTxHash(*ws, tempVec, 100);
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
            Env env(*this);
            std::array<Account, 2> accounts = {alice, carol};
            env.fund(XRP(444444), accounts);
            env.close();

            // many payments, and close lots of ledgers
            auto oneRound = [&](int numPayments) {
                return sendPayments(env, alice, carol, numPayments, 300);
            };

            // subscribe
            Json::Value request;
            request[jss::account_history_tx_stream] = Json::objectValue;
            request[jss::account_history_tx_stream][jss::account] =
                carol.human();
            auto wscLong = makeWSClient(env.app().config());
            auto jv = wscLong->invoke("subscribe", request);
            {
                // take out existing txns from the stream
                IdxHashVec tempVec;
                getTxHash(*wscLong, tempVec, 100);
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
            jtx::testable_amendments() | featurePermissionedDomains |
            featureCredentials | featurePermissionedDEX};

        Env env(*this, all);
        PermissionedDEX permDex(env);
        auto const alice = permDex.alice;
        auto const bob = permDex.bob;
        auto const carol = permDex.carol;
        auto const domainID = permDex.domainID;
        auto const gw = permDex.gw;
        auto const USD = permDex.USD;

        auto wsc = makeWSClient(env.app().config());

        Json::Value streams;
        streams[jss::streams] = Json::arrayValue;
        streams[jss::streams][0u] = "book_changes";

        auto jv = wsc->invoke("subscribe", streams);
        if (!BEAST_EXPECT(jv[jss::status] == "success"))
            return;
        env(offer(alice, XRP(10), USD(10)),
            domain(domainID),
            txflags(tfHybrid));
        env.close();

        env(pay(bob, carol, USD(5)),
            path(~USD),
            sendmax(XRP(5)),
            domain(domainID));
        env.close();

        BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
            if (jv[jss::changes].size() != 1)
                return false;

            auto const jrOffer = jv[jss::changes][0u];
            return (jv[jss::changes][0u][jss::domain]).asString() ==
                strHex(domainID) &&
                jrOffer[jss::currency_a].asString() == "XRP_drops" &&
                jrOffer[jss::volume_a].asString() == "5000000" &&
                jrOffer[jss::currency_b].asString() ==
                "rHUKYAZyUFn8PCZWbPfwHfbVQXTYrYKkHb/USD" &&
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
        // response to make sure the synethic fields hold the right values.

        testcase("Test synthetic fields from Subscribe response");

        using namespace test::jtx;
        using namespace std::chrono_literals;

        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const broker{"broker"};

        Env env{*this, features};
        env.fund(XRP(10000), alice, bob, broker);
        env.close();

        auto wsc = test::makeWSClient(env.app().config());
        Json::Value stream;
        stream[jss::streams] = Json::arrayValue;
        stream[jss::streams].append("transactions");
        auto jv = wsc->invoke("subscribe", stream);

        // Verify `nftoken_id` value equals to the NFTokenID that was
        // changed in the most recent NFTokenMint or NFTokenAcceptOffer
        // transaction
        auto verifyNFTokenID = [&](uint256 const& actualNftID) {
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                uint256 nftID;
                BEAST_EXPECT(
                    nftID.parseHex(jv[jss::meta][jss::nftoken_id].asString()));
                return nftID == actualNftID;
            }));
        };

        // Verify `nftoken_ids` value equals to the NFTokenIDs that were
        // changed in the most recent NFTokenCancelOffer transaction
        auto verifyNFTokenIDsInCancelOffer =
            [&](std::vector<uint256> actualNftIDs) {
                BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                    std::vector<uint256> metaIDs;
                    std::transform(
                        jv[jss::meta][jss::nftoken_ids].begin(),
                        jv[jss::meta][jss::nftoken_ids].end(),
                        std::back_inserter(metaIDs),
                        [this](Json::Value id) {
                            uint256 nftID;
                            BEAST_EXPECT(nftID.parseHex(id.asString()));
                            return nftID;
                        });
                    // Sort both array to prepare for comparison
                    std::sort(metaIDs.begin(), metaIDs.end());
                    std::sort(actualNftIDs.begin(), actualNftIDs.end());

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
                BEAST_EXPECT(metaOfferID.parseHex(
                    jv[jss::meta][jss::offer_id].asString()));
                return metaOfferID == offerID;
            }));
        };

        // Check new fields in tx meta when for all NFTtransactions
        {
            // Alice mints 2 NFTs
            // Verify the NFTokenIDs are correct in the NFTokenMint tx meta
            uint256 const nftId1{
                token::getNextID(env, alice, 0u, tfTransferable)};
            env(token::mint(alice, 0u), txflags(tfTransferable));
            env.close();
            verifyNFTokenID(nftId1);

            uint256 const nftId2{
                token::getNextID(env, alice, 0u, tfTransferable)};
            env(token::mint(alice, 0u), txflags(tfTransferable));
            env.close();
            verifyNFTokenID(nftId2);

            // Alice creates one sell offer for each NFT
            // Verify the offer indexes are correct in the NFTokenCreateOffer tx
            // meta
            uint256 const aliceOfferIndex1 =
                keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, nftId1, drops(1)),
                txflags(tfSellNFToken));
            env.close();
            verifyNFTokenOfferID(aliceOfferIndex1);

            uint256 const aliceOfferIndex2 =
                keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, nftId2, drops(1)),
                txflags(tfSellNFToken));
            env.close();
            verifyNFTokenOfferID(aliceOfferIndex2);

            // Alice cancels two offers she created
            // Verify the NFTokenIDs are correct in the NFTokenCancelOffer tx
            // meta
            env(token::cancelOffer(
                alice, {aliceOfferIndex1, aliceOfferIndex2}));
            env.close();
            verifyNFTokenIDsInCancelOffer({nftId1, nftId2});

            // Bobs creates a buy offer for nftId1
            // Verify the offer id is correct in the NFTokenCreateOffer tx meta
            auto const bobBuyOfferIndex =
                keylet::nftoffer(bob, env.seq(bob)).key;
            env(token::createOffer(bob, nftId1, drops(1)), token::owner(alice));
            env.close();
            verifyNFTokenOfferID(bobBuyOfferIndex);

            // Alice accepts bob's buy offer
            // Verify the NFTokenID is correct in the NFTokenAcceptOffer tx meta
            env(token::acceptBuyOffer(alice, bobBuyOfferIndex));
            env.close();
            verifyNFTokenID(nftId1);
        }

        // Check `nftoken_ids` in brokered mode
        {
            // Alice mints a NFT
            uint256 const nftId{
                token::getNextID(env, alice, 0u, tfTransferable)};
            env(token::mint(alice, 0u), txflags(tfTransferable));
            env.close();
            verifyNFTokenID(nftId);

            // Alice creates sell offer and set broker as destination
            uint256 const offerAliceToBroker =
                keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, nftId, drops(1)),
                token::destination(broker),
                txflags(tfSellNFToken));
            env.close();
            verifyNFTokenOfferID(offerAliceToBroker);

            // Bob creates buy offer
            uint256 const offerBobToBroker =
                keylet::nftoffer(bob, env.seq(bob)).key;
            env(token::createOffer(bob, nftId, drops(1)), token::owner(alice));
            env.close();
            verifyNFTokenOfferID(offerBobToBroker);

            // Check NFTokenID meta for NFTokenAcceptOffer in brokered mode
            env(token::brokerOffers(
                broker, offerBobToBroker, offerAliceToBroker));
            env.close();
            verifyNFTokenID(nftId);
        }

        // Check if there are no duplicate nft id in Cancel transactions where
        // multiple offers are cancelled for the same NFT
        {
            // Alice mints a NFT
            uint256 const nftId{
                token::getNextID(env, alice, 0u, tfTransferable)};
            env(token::mint(alice, 0u), txflags(tfTransferable));
            env.close();
            verifyNFTokenID(nftId);

            // Alice creates 2 sell offers for the same NFT
            uint256 const aliceOfferIndex1 =
                keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, nftId, drops(1)),
                txflags(tfSellNFToken));
            env.close();
            verifyNFTokenOfferID(aliceOfferIndex1);

            uint256 const aliceOfferIndex2 =
                keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, nftId, drops(1)),
                txflags(tfSellNFToken));
            env.close();
            verifyNFTokenOfferID(aliceOfferIndex2);

            // Make sure the metadata only has 1 nft id, since both offers are
            // for the same nft
            env(token::cancelOffer(
                alice, {aliceOfferIndex1, aliceOfferIndex2}));
            env.close();
            verifyNFTokenIDsInCancelOffer({nftId});
        }

        if (features[featureNFTokenMintOffer])
        {
            uint256 const aliceMintWithOfferIndex1 =
                keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::mint(alice), token::amount(XRP(0)));
            env.close();
            verifyNFTokenOfferID(aliceMintWithOfferIndex1);
        }
    }

    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{testable_amendments()};
        FeatureBitset const xrpFees{featureXRPFees};

        testServer();
        testLedger();
        testTransactions_APIv1();
        testTransactions_APIv2();
        testManifests();
        testValidations(all - featureXRPFees - featureSmartEscrow);
        testValidations(all - featureSmartEscrow);
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

BEAST_DEFINE_TESTSUITE(Subscribe, rpc, ripple);

}  // namespace test
}  // namespace ripple
