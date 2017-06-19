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

#include <BeastConfig.h>
#include <ripple/app/misc/LoadFeeTrack.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/protocol/JsonFields.h>
#include <test/jtx/WSClient.h>
#include <test/jtx/envconfig.h>
#include <test/jtx.h>
#include <ripple/beast/unit_test.h>

namespace ripple {
namespace test {

class Subscribe_test : public beast::unit_test::suite
{
public:
    void testServer()
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
                BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::status] == "success");
        }

        {
            // Raise fee to cause an update
            auto& feeTrack = env.app().getFeeTrack();
            for(int i = 0; i < 5; ++i)
                feeTrack.raiseLocalFee();
            env.app().getOPs().reportFeeChange();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    return jv[jss::type] == "serverStatus";
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
            // Raise fee to cause an update
            auto& feeTrack = env.app().getFeeTrack();
            for (int i = 0; i < 5; ++i)
                feeTrack.raiseLocalFee();
            env.app().getOPs().reportFeeChange();

            // Check stream update
            BEAST_EXPECT(! wsc->getMsg(10ms));
        }
    }

    void testLedger()
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
                BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::result][jss::ledger_index] == 2);
        }

        {
            // Accept a ledger
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    return jv[jss::ledger_index] == 3;
                }));
        }

        {
            // Accept another ledger
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    return jv[jss::ledger_index] == 4;
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

    void testTransactions()
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
                BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::status] == "success");
        }

        {
            env.fund(XRP(10000), "alice");
            env.close();

            // Check stream update for payment transaction
            BEAST_EXPECT(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    return jv[jss::meta]["AffectedNodes"][1u]
                        ["CreatedNode"]["NewFields"][jss::Account] ==
                            Account("alice").human();
                }));

            // Check stream update for accountset transaction
            BEAST_EXPECT(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    return jv[jss::meta]["AffectedNodes"][0u]
                        ["ModifiedNode"]["FinalFields"][jss::Account] ==
                            Account("alice").human();
                }));

            env.fund(XRP(10000), "bob");
            env.close();

            // Check stream update for payment transaction
            BEAST_EXPECT(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    return jv[jss::meta]["AffectedNodes"][1u]
                        ["CreatedNode"]["NewFields"][jss::Account] ==
                            Account("bob").human();
                }));

            // Check stream update for accountset transaction
            BEAST_EXPECT(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    return jv[jss::meta]["AffectedNodes"][0u]
                        ["ModifiedNode"]["FinalFields"][jss::Account] ==
                            Account("bob").human();
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
            stream = Json::objectValue;
            stream[jss::accounts] = Json::arrayValue;
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
            env.close();
            BEAST_EXPECT(! wsc->getMsg(10ms));

            // Transactions concerning alice
            env.trust(Account("bob")["USD"](100), "alice");
            env.close();

            // Check stream updates
            BEAST_EXPECT(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    return jv[jss::meta]["AffectedNodes"][1u]
                        ["ModifiedNode"]["FinalFields"][jss::Account] ==
                            Account("alice").human();
                }));

            BEAST_EXPECT(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    return jv[jss::meta]["AffectedNodes"][1u]
                        ["CreatedNode"]["NewFields"]["LowLimit"]
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

    void testManifests()
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

    void testValidations()
    {
        using namespace jtx;

        Env env {*this, envconfig(validator, "")};
        auto& cfg = env.app().config();
        if(! BEAST_EXPECT(cfg.section(SECTION_VALIDATION_SEED).empty()))
            return;
        auto const parsedseed = parseBase58<Seed>(
            cfg.section(SECTION_VALIDATION_SEED).values()[0]);
        if(! BEAST_EXPECT(parsedseed))
            return;

        std::string const valPublicKey =
            toBase58 (TokenType::TOKEN_NODE_PUBLIC,
                derivePublicKey (KeyType::secp256k1,
                    generateSecretKey (KeyType::secp256k1, *parsedseed)));

        auto wsc = makeWSClient(env.app().config());
        Json::Value stream;

        {
            // RPC subscribe to validations stream
            stream[jss::streams] = Json::arrayValue;
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
            // Accept a ledger
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    return jv[jss::type] == "validationReceived" &&
                        jv[jss::validation_public_key].asString() ==
                            valPublicKey &&
                        jv[jss::ledger_hash] ==
                            to_string(env.closed()->info().hash) &&
                        jv[jss::ledger_index] ==
                            to_string(env.closed()->info().seq) &&
                        jv[jss::flags] ==
                            (vfFullyCanonicalSig | STValidation::kFullFlag) &&
                        jv[jss::full] == true &&
                        !jv.isMember(jss::load_fee) &&
                        jv[jss::signature] &&
                        jv[jss::signing_time];
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
    testSubByUrl()
    {
        using namespace jtx;
        testcase("Subscribe by url");
        Env env {*this};

        Json::Value jv;
        jv[jss::url] = "http://localhost/events";
        jv[jss::url_username] = "admin";
        jv[jss::url_password] = "password";
        jv[jss::streams] = Json::arrayValue;
        jv[jss::streams][0u] = "validations";
        auto jr = env.rpc("json", "subscribe", to_string(jv)) [jss::result];
        BEAST_EXPECT(jr[jss::status] == "success");

        jv[jss::streams][0u] = "ledger";
        jr = env.rpc("json", "subscribe", to_string(jv)) [jss::result];
        BEAST_EXPECT(jr[jss::status] == "success");

        jr = env.rpc("json", "unsubscribe", to_string(jv)) [jss::result];
        BEAST_EXPECT(jr[jss::status] == "success");

        jv[jss::streams][0u] = "validations";
        jr = env.rpc("json", "unsubscribe", to_string(jv)) [jss::result];
        BEAST_EXPECT(jr[jss::status] == "success");
    }

    void
    testSubErrors(bool subscribe)
    {
        using namespace jtx;
        auto const method = subscribe ? "subscribe" : "unsubscribe";
        testcase << "Error cases for " << method;

        Env env {*this};
        auto wsc = makeWSClient(env.app().config());

        {
            auto jr = env.rpc("json", method, "{}") [jss::result];
            BEAST_EXPECT(jr[jss::error] == "invalidParams");
            BEAST_EXPECT(jr[jss::error_message] == "Invalid parameters.");
        }

        {
            Json::Value jv;
            jv[jss::url] = "not-a-url";
            jv[jss::username] = "admin";
            jv[jss::password] = "password";
            auto jr = env.rpc("json", method, to_string(jv)) [jss::result];
            if (subscribe)
            {
                BEAST_EXPECT(jr[jss::error] == "internal");
                BEAST_EXPECT(jr[jss::error_message] == "Internal error.");
            }
            // else TODO: why isn't this an error for unsubscribe ?
            // (findRpcSub returns null)
        }

        {
            Json::Value jv;
            jv[jss::url] = "ftp://scheme.not.supported.tld";
            auto jr = env.rpc("json", method, to_string(jv)) [jss::result];
            if (subscribe)
            {
                BEAST_EXPECT(jr[jss::error] == "internal");
                BEAST_EXPECT(jr[jss::error_message] == "Internal error.");
            }
        }

        {
            Env env_nonadmin {*this, no_admin(envconfig(port_increment, 2))};
            Json::Value jv;
            jv[jss::url] = "no-url";
            auto jr = env_nonadmin.rpc("json", method, to_string(jv)) [jss::result];
            BEAST_EXPECT(jr[jss::error] == "noPermission");
            BEAST_EXPECT(jr[jss::error_message] == "You don't have permission for this command.");
        }

        for (auto const& f : {jss::accounts_proposed, jss::accounts})
        {
            {
                Json::Value jv;
                jv[f] = "";
                auto jr = wsc->invoke(method, jv) [jss::result];
                BEAST_EXPECT(jr[jss::error] == "invalidParams");
                BEAST_EXPECT(jr[jss::error_message] == "Invalid parameters.");
            }

            {
                Json::Value jv;
                jv[f] = Json::arrayValue;
                auto jr = wsc->invoke(method, jv) [jss::result];
                BEAST_EXPECT(jr[jss::error] == "actMalformed");
                BEAST_EXPECT(jr[jss::error_message] == "Account malformed.");
            }
        }

        {
            Json::Value jv;
            jv[jss::books] = "";
            auto jr = wsc->invoke(method, jv) [jss::result];
            BEAST_EXPECT(jr[jss::error] == "invalidParams");
            BEAST_EXPECT(jr[jss::error_message] == "Invalid parameters.");
        }

        {
            Json::Value jv;
            jv[jss::books] = Json::arrayValue;
            jv[jss::books][0u] = 1;
            auto jr = wsc->invoke(method, jv) [jss::result];
            BEAST_EXPECT(jr[jss::error] == "invalidParams");
            BEAST_EXPECT(jr[jss::error_message] == "Invalid parameters.");
        }

        {
            Json::Value jv;
            jv[jss::books] = Json::arrayValue;
            jv[jss::books][0u] = Json::objectValue;
            jv[jss::books][0u][jss::taker_gets] = Json::objectValue;
            jv[jss::books][0u][jss::taker_pays] = Json::objectValue;
            auto jr = wsc->invoke(method, jv) [jss::result];
            BEAST_EXPECT(jr[jss::error] == "srcCurMalformed");
            BEAST_EXPECT(jr[jss::error_message] == "Source currency is malformed.");
        }

        {
            Json::Value jv;
            jv[jss::books] = Json::arrayValue;
            jv[jss::books][0u] = Json::objectValue;
            jv[jss::books][0u][jss::taker_gets] = Json::objectValue;
            jv[jss::books][0u][jss::taker_pays] = Json::objectValue;
            jv[jss::books][0u][jss::taker_pays][jss::currency] = "ZZZZ";
            auto jr = wsc->invoke(method, jv) [jss::result];
            BEAST_EXPECT(jr[jss::error] == "srcCurMalformed");
            BEAST_EXPECT(jr[jss::error_message] == "Source currency is malformed.");
        }

        {
            Json::Value jv;
            jv[jss::books] = Json::arrayValue;
            jv[jss::books][0u] = Json::objectValue;
            jv[jss::books][0u][jss::taker_gets] = Json::objectValue;
            jv[jss::books][0u][jss::taker_pays] = Json::objectValue;
            jv[jss::books][0u][jss::taker_pays][jss::currency] = "USD";
            jv[jss::books][0u][jss::taker_pays][jss::issuer] = 1;
            auto jr = wsc->invoke(method, jv) [jss::result];
            BEAST_EXPECT(jr[jss::error] == "srcIsrMalformed");
            BEAST_EXPECT(jr[jss::error_message] == "Source issuer is malformed.");
        }

        {
            Json::Value jv;
            jv[jss::books] = Json::arrayValue;
            jv[jss::books][0u] = Json::objectValue;
            jv[jss::books][0u][jss::taker_gets] = Json::objectValue;
            jv[jss::books][0u][jss::taker_pays] = Json::objectValue;
            jv[jss::books][0u][jss::taker_pays][jss::currency] = "USD";
            jv[jss::books][0u][jss::taker_pays][jss::issuer] = Account{"gateway"}.human() + "DEAD";
            auto jr = wsc->invoke(method, jv) [jss::result];
            BEAST_EXPECT(jr[jss::error] == "srcIsrMalformed");
            BEAST_EXPECT(jr[jss::error_message] == "Source issuer is malformed.");
        }

        {
            Json::Value jv;
            jv[jss::books] = Json::arrayValue;
            jv[jss::books][0u] = Json::objectValue;
            jv[jss::books][0u][jss::taker_pays] = Account{"gateway"}["USD"](1).value().getJson(1);
            jv[jss::books][0u][jss::taker_gets] = Json::objectValue;
            auto jr = wsc->invoke(method, jv) [jss::result];
            // NOTE: this error is slightly incongruous with the
            // equivalent source currency error
            BEAST_EXPECT(jr[jss::error] == "dstAmtMalformed");
            BEAST_EXPECT(jr[jss::error_message] == "Destination amount/currency/issuer is malformed.");
        }

        {
            Json::Value jv;
            jv[jss::books] = Json::arrayValue;
            jv[jss::books][0u] = Json::objectValue;
            jv[jss::books][0u][jss::taker_pays] = Account{"gateway"}["USD"](1).value().getJson(1);
            jv[jss::books][0u][jss::taker_gets][jss::currency] = "ZZZZ";
            auto jr = wsc->invoke(method, jv) [jss::result];
            // NOTE: this error is slightly incongruous with the
            // equivalent source currency error
            BEAST_EXPECT(jr[jss::error] == "dstAmtMalformed");
            BEAST_EXPECT(jr[jss::error_message] == "Destination amount/currency/issuer is malformed.");
        }

        {
            Json::Value jv;
            jv[jss::books] = Json::arrayValue;
            jv[jss::books][0u] = Json::objectValue;
            jv[jss::books][0u][jss::taker_pays] = Account{"gateway"}["USD"](1).value().getJson(1);
            jv[jss::books][0u][jss::taker_gets][jss::currency] = "USD";
            jv[jss::books][0u][jss::taker_gets][jss::issuer] = 1;
            auto jr = wsc->invoke(method, jv) [jss::result];
            BEAST_EXPECT(jr[jss::error] == "dstIsrMalformed");
            BEAST_EXPECT(jr[jss::error_message] == "Destination issuer is malformed.");
        }

        {
            Json::Value jv;
            jv[jss::books] = Json::arrayValue;
            jv[jss::books][0u] = Json::objectValue;
            jv[jss::books][0u][jss::taker_pays] = Account{"gateway"}["USD"](1).value().getJson(1);
            jv[jss::books][0u][jss::taker_gets][jss::currency] = "USD";
            jv[jss::books][0u][jss::taker_gets][jss::issuer] = Account{"gateway"}.human() + "DEAD";
            auto jr = wsc->invoke(method, jv) [jss::result];
            BEAST_EXPECT(jr[jss::error] == "dstIsrMalformed");
            BEAST_EXPECT(jr[jss::error_message] == "Destination issuer is malformed.");
        }

        {
            Json::Value jv;
            jv[jss::books] = Json::arrayValue;
            jv[jss::books][0u] = Json::objectValue;
            jv[jss::books][0u][jss::taker_pays] = Account{"gateway"}["USD"](1).value().getJson(1);
            jv[jss::books][0u][jss::taker_gets] = Account{"gateway"}["USD"](1).value().getJson(1);
            auto jr = wsc->invoke(method, jv) [jss::result];
            BEAST_EXPECT(jr[jss::error] == "badMarket");
            BEAST_EXPECT(jr[jss::error_message] == "No such market.");
        }

        {
            Json::Value jv;
            jv[jss::streams] = "";
            auto jr = wsc->invoke(method, jv) [jss::result];
            BEAST_EXPECT(jr[jss::error] == "invalidParams");
            BEAST_EXPECT(jr[jss::error_message] == "Invalid parameters.");
        }

        {
            Json::Value jv;
            jv[jss::streams] = Json::arrayValue;
            jv[jss::streams][0u] = 1;
            auto jr = wsc->invoke(method, jv) [jss::result];
            BEAST_EXPECT(jr[jss::error] == "malformedStream");
            BEAST_EXPECT(jr[jss::error_message] == "Stream malformed.");
        }

        {
            Json::Value jv;
            jv[jss::streams] = Json::arrayValue;
            jv[jss::streams][0u] = "not_a_stream";
            auto jr = wsc->invoke(method, jv) [jss::result];
            BEAST_EXPECT(jr[jss::error] == "malformedStream");
            BEAST_EXPECT(jr[jss::error_message] == "Stream malformed.");
        }

    }

    void run() override
    {
        testServer();
        testLedger();
        testTransactions();
        testManifests();
        testValidations();
        testSubErrors(true);
        testSubErrors(false);
        testSubByUrl();
    }
};

BEAST_DEFINE_TESTSUITE(Subscribe,app,ripple);

} // test
} // ripple
