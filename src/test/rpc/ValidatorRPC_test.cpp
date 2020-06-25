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

#include <ripple/app/main/BasicApp.h>
#include <ripple/app/misc/ValidatorSite.h>
#include <ripple/basics/base64.h>
#include <ripple/beast/unit_test.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/json/json_value.h>
#include <ripple/protocol/Sign.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>
#include <test/jtx/TrustedPublisherServer.h>

#include <set>

namespace ripple {

namespace test {

class ValidatorRPC_test : public beast::unit_test::suite
{
    using Validator = TrustedPublisherServer::Validator;

public:
    void
    testPrivileges()
    {
        using namespace test::jtx;

        for (bool const isAdmin : {true, false})
        {
            for (std::string cmd : {"validators", "validator_list_sites"})
            {
                Env env{*this, isAdmin ? envconfig() : envconfig(no_admin)};
                auto const jrr = env.rpc(cmd)[jss::result];
                if (isAdmin)
                {
                    BEAST_EXPECT(!jrr.isMember(jss::error));
                    BEAST_EXPECT(jrr[jss::status] == "success");
                }
                else
                {
                    // The current HTTP/S ServerHandler returns an HTTP 403
                    // error code here rather than a noPermission JSON error.
                    // The JSONRPCClient just eats that error and returns null
                    // result.
                    BEAST_EXPECT(jrr.isNull());
                }
            }

            {
                Env env{*this, isAdmin ? envconfig() : envconfig(no_admin)};
                auto const jrr = env.rpc("server_info")[jss::result];
                BEAST_EXPECT(jrr[jss::status] == "success");
                BEAST_EXPECT(
                    jrr[jss::info].isMember(jss::validator_list) == isAdmin);
            }

            {
                Env env{*this, isAdmin ? envconfig() : envconfig(no_admin)};
                auto const jrr = env.rpc("server_state")[jss::result];
                BEAST_EXPECT(jrr[jss::status] == "success");
                BEAST_EXPECT(
                    jrr[jss::state].isMember(jss::validator_list_expires) ==
                    isAdmin);
            }
        }
    }

    void
    testStaticUNL()
    {
        using namespace test::jtx;

        std::set<std::string> const keys = {
            "n949f75evCHwgyP4fPVgaHqNHxUVN15PsJEZ3B3HnXPcPjcZAoy7",
            "n9MD5h24qrQqiyBC8aeqqCWvpiBiYQ3jxSr91uiDvmrkyHRdYLUj"};
        Env env{
            *this,
            envconfig([&keys](std::unique_ptr<Config> cfg) {
                for (auto const& key : keys)
                    cfg->section(SECTION_VALIDATORS).append(key);
                return cfg;
            }),
        };

        // Server info reports maximum expiration since not dynamic
        {
            auto const jrr = env.rpc("server_info")[jss::result];
            BEAST_EXPECT(
                jrr[jss::info][jss::validator_list][jss::expiration] ==
                "never");
        }
        {
            auto const jrr = env.rpc("server_state")[jss::result];
            BEAST_EXPECT(
                jrr[jss::state][jss::validator_list_expires].asUInt() ==
                NetClock::time_point::max().time_since_epoch().count());
        }
        // All our keys are in the response
        {
            auto const jrr = env.rpc("validators")[jss::result];
            BEAST_EXPECT(jrr[jss::validator_list][jss::expiration] == "never");
            BEAST_EXPECT(jrr[jss::validation_quorum].asUInt() == keys.size());
            BEAST_EXPECT(
                jrr[jss::trusted_validator_keys].size() == keys.size());
            BEAST_EXPECT(jrr[jss::publisher_lists].size() == 0);
            BEAST_EXPECT(jrr[jss::local_static_keys].size() == keys.size());
            for (auto const& jKey : jrr[jss::local_static_keys])
            {
                BEAST_EXPECT(keys.count(jKey.asString()) == 1);
            }
            BEAST_EXPECT(jrr[jss::signing_keys].size() == 0);
        }
        // No validator sites configured
        {
            auto const jrr = env.rpc("validator_list_sites")[jss::result];
            BEAST_EXPECT(jrr[jss::validator_sites].size() == 0);
        }
        // Negative UNL empty
        {
            auto const jrr = env.rpc("validators")[jss::result];
            BEAST_EXPECT(jrr[jss::NegativeUNL].isNull());
        }
        // Negative UNL update
        {
            hash_set<PublicKey> disabledKeys;
            auto k1 = randomKeyPair(KeyType::ed25519).first;
            auto k2 = randomKeyPair(KeyType::ed25519).first;
            disabledKeys.insert(k1);
            disabledKeys.insert(k2);
            env.app().validators().setNegativeUnl(disabledKeys);

            auto const jrr = env.rpc("validators")[jss::result];
            auto& jrrnUnl = jrr[jss::NegativeUNL];
            auto jrrnUnlSize = jrrnUnl.size();
            BEAST_EXPECT(jrrnUnlSize == 2);
            for (std::uint32_t x = 0; x < jrrnUnlSize; ++x)
            {
                auto parsedKey = parseBase58<PublicKey>(
                    TokenType::NodePublic, jrrnUnl[x].asString());
                BEAST_EXPECT(parsedKey);
                if (parsedKey)
                    BEAST_EXPECT(
                        disabledKeys.find(*parsedKey) != disabledKeys.end());
            }

            disabledKeys.clear();
            env.app().validators().setNegativeUnl(disabledKeys);
            auto const jrrUpdated = env.rpc("validators")[jss::result];
            BEAST_EXPECT(jrrUpdated[jss::NegativeUNL].isNull());
        }
    }

    void
    testDynamicUNL()
    {
        using namespace test::jtx;

        auto toStr = [](PublicKey const& publicKey) {
            return toBase58(TokenType::NodePublic, publicKey);
        };

        // Validator keys that will be in the published list
        std::vector<Validator> validators = {
            TrustedPublisherServer::randomValidator(),
            TrustedPublisherServer::randomValidator()};
        std::set<std::string> expectedKeys;
        for (auto const& val : validators)
            expectedKeys.insert(toStr(val.masterPublic));

        // Manage single-thread io_service for server.
        BasicApp worker{1};
        using namespace std::chrono_literals;
        NetClock::time_point const expiration{3600s};
        TrustedPublisherServer server{
            worker.get_io_service(), validators, expiration, false, 1, false};

        //----------------------------------------------------------------------
        // Publisher list site unavailable
        {
            // Publisher site information
            using namespace std::string_literals;
            std::string siteURI =
                "http://"s + getEnvLocalhostAddr() + ":1234/validators";

            Env env{
                *this,
                envconfig([&](std::unique_ptr<Config> cfg) {
                    cfg->section(SECTION_VALIDATOR_LIST_SITES).append(siteURI);
                    cfg->section(SECTION_VALIDATOR_LIST_KEYS)
                        .append(strHex(server.publisherPublic()));
                    return cfg;
                }),
            };

            env.app().validatorSites().start();
            env.app().validatorSites().join();

            {
                auto const jrr = env.rpc("server_info")[jss::result];
                BEAST_EXPECT(
                    jrr[jss::info][jss::validator_list][jss::expiration] ==
                    "unknown");
            }
            {
                auto const jrr = env.rpc("server_state")[jss::result];
                BEAST_EXPECT(
                    jrr[jss::state][jss::validator_list_expires].asInt() == 0);
            }
            {
                auto const jrr = env.rpc("validators")[jss::result];
                BEAST_EXPECT(
                    jrr[jss::validation_quorum].asUInt() ==
                    std::numeric_limits<std::uint32_t>::max());
                BEAST_EXPECT(jrr[jss::local_static_keys].size() == 0);
                BEAST_EXPECT(jrr[jss::trusted_validator_keys].size() == 0);
                BEAST_EXPECT(
                    jrr[jss::validator_list][jss::expiration] == "unknown");

                if (BEAST_EXPECT(jrr[jss::publisher_lists].size() == 1))
                {
                    auto jp = jrr[jss::publisher_lists][0u];
                    BEAST_EXPECT(jp[jss::available] == false);
                    BEAST_EXPECT(jp[jss::list].size() == 0);
                    BEAST_EXPECT(!jp.isMember(jss::seq));
                    BEAST_EXPECT(!jp.isMember(jss::expiration));
                    BEAST_EXPECT(!jp.isMember(jss::version));
                    BEAST_EXPECT(
                        jp[jss::pubkey_publisher] ==
                        strHex(server.publisherPublic()));
                }
                BEAST_EXPECT(jrr[jss::signing_keys].size() == 0);
            }
            {
                auto const jrr = env.rpc("validator_list_sites")[jss::result];
                if (BEAST_EXPECT(jrr[jss::validator_sites].size() == 1))
                {
                    auto js = jrr[jss::validator_sites][0u];
                    BEAST_EXPECT(js[jss::refresh_interval_min].asUInt() == 5);
                    BEAST_EXPECT(js[jss::uri] == siteURI);
                    BEAST_EXPECT(js.isMember(jss::last_refresh_time));
                    BEAST_EXPECT(js[jss::last_refresh_status] == "invalid");
                }
            }
        }
        //----------------------------------------------------------------------
        // Publisher list site available
        {
            server.start();

            std::stringstream uri;
            uri << "http://" << server.local_endpoint() << "/validators";
            auto siteURI = uri.str();

            Env env{
                *this,
                envconfig([&](std::unique_ptr<Config> cfg) {
                    cfg->section(SECTION_VALIDATOR_LIST_SITES).append(siteURI);
                    cfg->section(SECTION_VALIDATOR_LIST_KEYS)
                        .append(strHex(server.publisherPublic()));
                    return cfg;
                }),
            };

            env.app().validatorSites().start();
            env.app().validatorSites().join();
            hash_set<NodeID> startKeys;
            for (auto const& val : validators)
                startKeys.insert(calcNodeID(val.masterPublic));

            env.app().validators().updateTrusted(startKeys);

            {
                auto const jrr = env.rpc("server_info")[jss::result];
                BEAST_EXPECT(
                    jrr[jss::info][jss::validator_list][jss::expiration] ==
                    to_string(expiration));
            }
            {
                auto const jrr = env.rpc("server_state")[jss::result];
                BEAST_EXPECT(
                    jrr[jss::state][jss::validator_list_expires].asUInt() ==
                    expiration.time_since_epoch().count());
            }
            {
                auto const jrr = env.rpc("validators")[jss::result];
                BEAST_EXPECT(jrr[jss::validation_quorum].asUInt() == 2);
                BEAST_EXPECT(
                    jrr[jss::validator_list][jss::expiration] ==
                    to_string(expiration));
                BEAST_EXPECT(jrr[jss::local_static_keys].size() == 0);

                BEAST_EXPECT(
                    jrr[jss::trusted_validator_keys].size() ==
                    expectedKeys.size());
                for (auto const& jKey : jrr[jss::trusted_validator_keys])
                {
                    BEAST_EXPECT(expectedKeys.count(jKey.asString()) == 1);
                }

                if (BEAST_EXPECT(jrr[jss::publisher_lists].size() == 1))
                {
                    auto jp = jrr[jss::publisher_lists][0u];
                    BEAST_EXPECT(jp[jss::available] == true);
                    if (BEAST_EXPECT(jp[jss::list].size() == 2))
                    {
                        // check entries
                        std::set<std::string> foundKeys;
                        for (auto const& k : jp[jss::list])
                        {
                            foundKeys.insert(k.asString());
                        }
                        BEAST_EXPECT(foundKeys == expectedKeys);
                    }
                    BEAST_EXPECT(jp[jss::seq].asUInt() == 1);
                    BEAST_EXPECT(
                        jp[jss::pubkey_publisher] ==
                        strHex(server.publisherPublic()));
                    BEAST_EXPECT(jp[jss::expiration] == to_string(expiration));
                    BEAST_EXPECT(jp[jss::version] == 1);
                }
                auto jsk = jrr[jss::signing_keys];
                BEAST_EXPECT(jsk.size() == 2);
                for (auto const& val : validators)
                {
                    BEAST_EXPECT(jsk.isMember(toStr(val.masterPublic)));
                    BEAST_EXPECT(
                        jsk[toStr(val.masterPublic)] ==
                        toStr(val.signingPublic));
                }
            }
            {
                auto const jrr = env.rpc("validator_list_sites")[jss::result];
                if (BEAST_EXPECT(jrr[jss::validator_sites].size() == 1))
                {
                    auto js = jrr[jss::validator_sites][0u];
                    BEAST_EXPECT(js[jss::refresh_interval_min].asUInt() == 5);
                    BEAST_EXPECT(js[jss::uri] == siteURI);
                    BEAST_EXPECT(js[jss::last_refresh_status] == "accepted");
                    // The actual time of the update will vary run to run, so
                    // just verify the time is there
                    BEAST_EXPECT(js.isMember(jss::last_refresh_time));
                }
            }
        }
    }

    void
    test_validation_create()
    {
        using namespace test::jtx;
        Env env{*this};
        auto result = env.rpc("validation_create");
        BEAST_EXPECT(
            result.isMember(jss::result) &&
            result[jss::result][jss::status] == "success");
        result = env.rpc(
            "validation_create",
            "BAWL MAN JADE MOON DOVE GEM SON NOW HAD ADEN GLOW TIRE");
        BEAST_EXPECT(
            result.isMember(jss::result) &&
            result[jss::result][jss::status] == "success");
    }

    void
    run() override
    {
        testPrivileges();
        testStaticUNL();
        testDynamicUNL();
        test_validation_create();
    }
};

BEAST_DEFINE_TESTSUITE(ValidatorRPC, app, ripple);

}  // namespace test
}  // namespace ripple
