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

#include <ripple/app/main/LoadManager.h>
#include <ripple/app/misc/LoadFeeTrack.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/beast/unit_test.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>
#include <test/jtx/WSClient.h>
#include <test/jtx/envconfig.h>
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
        }

        {
            // Accept a ledger
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                return jv[jss::ledger_index] == 3;
            }));
        }

        {
            // Accept another ledger
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                return jv[jss::ledger_index] == 4;
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
    testTransactions()
    {
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
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
                         ["NewFields"][jss::Account] ==
                    Account("alice").human();
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
                         ["NewFields"][jss::Account] == Account("bob").human();
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

                // Certain fields are only added on a flag ledger.
                bool const isFlagLedger =
                    (env.closed()->info().seq + 1) % 256 == 0;

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
            Env env_nonadmin{*this, no_admin(envconfig(port_increment, 3))};
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
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{supported_amendments()};
        FeatureBitset const xrpFees{featureXRPFees};

        testServer();
        testLedger();
        testTransactions();
        testManifests();
        testValidations(all - xrpFees);
        testValidations(all);
        testSubErrors(true);
        testSubErrors(false);
        testSubByUrl();
        testHistoryTxStream();
    }
};

BEAST_DEFINE_TESTSUITE(Subscribe, app, ripple);

}  // namespace test
}  // namespace ripple
