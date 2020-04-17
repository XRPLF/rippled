//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>

#include <ripple/resource/Charge.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/GRPCHandlers.h>
#include <test/jtx/WSClient.h>
#include <test/rpc/GRPCTestClientBase.h>

namespace ripple {
namespace test {

class AccountInfo_test : public beast::unit_test::suite
{
public:
    void
    testErrors()
    {
        using namespace jtx;
        Env env(*this);
        {
            // account_info with no account.
            auto const info = env.rpc("json", "account_info", "{ }");
            BEAST_EXPECT(
                info[jss::result][jss::error_message] ==
                "Missing field 'account'.");
        }
        {
            // account_info with a malformed account sting.
            auto const info = env.rpc(
                "json",
                "account_info",
                "{\"account\": "
                "\"n94JNrQYkDrpt62bbSR7nVEhdyAvcJXRAsjEkFYyqRkh9SUTYEqV\"}");
            BEAST_EXPECT(
                info[jss::result][jss::error_message] == "Disallowed seed.");
        }
        {
            // account_info with an account that's not in the ledger.
            Account const bogie{"bogie"};
            auto const info = env.rpc(
                "json",
                "account_info",
                std::string("{ ") + "\"account\": \"" + bogie.human() + "\"}");
            BEAST_EXPECT(
                info[jss::result][jss::error_message] == "Account not found.");
        }
    }

    // Test the "signer_lists" argument in account_info.
    void
    testSignerLists()
    {
        using namespace jtx;
        Env env(*this);
        Account const alice{"alice"};
        env.fund(XRP(1000), alice);

        auto const withoutSigners =
            std::string("{ ") + "\"account\": \"" + alice.human() + "\"}";

        auto const withSigners = std::string("{ ") + "\"account\": \"" +
            alice.human() + "\", " + "\"signer_lists\": true }";

        // Alice has no SignerList yet.
        {
            // account_info without the "signer_lists" argument.
            auto const info = env.rpc("json", "account_info", withoutSigners);
            BEAST_EXPECT(
                info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            BEAST_EXPECT(!info[jss::result][jss::account_data].isMember(
                jss::signer_lists));
        }
        {
            // account_info with the "signer_lists" argument.
            auto const info = env.rpc("json", "account_info", withSigners);
            BEAST_EXPECT(
                info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            auto const& data = info[jss::result][jss::account_data];
            BEAST_EXPECT(data.isMember(jss::signer_lists));
            auto const& signerLists = data[jss::signer_lists];
            BEAST_EXPECT(signerLists.isArray());
            BEAST_EXPECT(signerLists.size() == 0);
        }

        // Give alice a SignerList.
        Account const bogie{"bogie"};

        Json::Value const smallSigners = signers(alice, 2, {{bogie, 3}});
        env(smallSigners);
        {
            // account_info without the "signer_lists" argument.
            auto const info = env.rpc("json", "account_info", withoutSigners);
            BEAST_EXPECT(
                info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            BEAST_EXPECT(!info[jss::result][jss::account_data].isMember(
                jss::signer_lists));
        }
        {
            // account_info with the "signer_lists" argument.
            auto const info = env.rpc("json", "account_info", withSigners);
            BEAST_EXPECT(
                info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            auto const& data = info[jss::result][jss::account_data];
            BEAST_EXPECT(data.isMember(jss::signer_lists));
            auto const& signerLists = data[jss::signer_lists];
            BEAST_EXPECT(signerLists.isArray());
            BEAST_EXPECT(signerLists.size() == 1);
            auto const& signers = signerLists[0u];
            BEAST_EXPECT(signers.isObject());
            BEAST_EXPECT(signers[sfSignerQuorum.jsonName] == 2);
            auto const& signerEntries = signers[sfSignerEntries.jsonName];
            BEAST_EXPECT(signerEntries.size() == 1);
            auto const& entry0 = signerEntries[0u][sfSignerEntry.jsonName];
            BEAST_EXPECT(entry0[sfSignerWeight.jsonName] == 3);
        }

        // Give alice a big signer list
        Account const demon{"demon"};
        Account const ghost{"ghost"};
        Account const haunt{"haunt"};
        Account const jinni{"jinni"};
        Account const phase{"phase"};
        Account const shade{"shade"};
        Account const spook{"spook"};

        Json::Value const bigSigners = signers(
            alice,
            4,
            {
                {bogie, 1},
                {demon, 1},
                {ghost, 1},
                {haunt, 1},
                {jinni, 1},
                {phase, 1},
                {shade, 1},
                {spook, 1},
            });
        env(bigSigners);
        {
            // account_info with the "signer_lists" argument.
            auto const info = env.rpc("json", "account_info", withSigners);
            BEAST_EXPECT(
                info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            auto const& data = info[jss::result][jss::account_data];
            BEAST_EXPECT(data.isMember(jss::signer_lists));
            auto const& signerLists = data[jss::signer_lists];
            BEAST_EXPECT(signerLists.isArray());
            BEAST_EXPECT(signerLists.size() == 1);
            auto const& signers = signerLists[0u];
            BEAST_EXPECT(signers.isObject());
            BEAST_EXPECT(signers[sfSignerQuorum.jsonName] == 4);
            auto const& signerEntries = signers[sfSignerEntries.jsonName];
            BEAST_EXPECT(signerEntries.size() == 8);
            for (unsigned i = 0u; i < 8; ++i)
            {
                auto const& entry = signerEntries[i][sfSignerEntry.jsonName];
                BEAST_EXPECT(entry.size() == 2);
                BEAST_EXPECT(entry.isMember(sfAccount.jsonName));
                BEAST_EXPECT(entry[sfSignerWeight.jsonName] == 1);
            }
        }
    }

    // Test the "signer_lists" argument in account_info, version 2 API.
    void
    testSignerListsV2()
    {
        using namespace jtx;
        Env env(*this);
        Account const alice{"alice"};
        env.fund(XRP(1000), alice);

        auto const withoutSigners = std::string("{ ") +
            "\"jsonrpc\": \"2.0\", "
            "\"ripplerpc\": \"2.0\", "
            "\"id\": 5, "
            "\"method\": \"account_info\", "
            "\"params\": { "
            "\"account\": \"" +
            alice.human() + "\"}}";

        auto const withSigners = std::string("{ ") +
            "\"jsonrpc\": \"2.0\", "
            "\"ripplerpc\": \"2.0\", "
            "\"id\": 6, "
            "\"method\": \"account_info\", "
            "\"params\": { "
            "\"account\": \"" +
            alice.human() + "\", " + "\"signer_lists\": true }}";
        // Alice has no SignerList yet.
        {
            // account_info without the "signer_lists" argument.
            auto const info = env.rpc("json2", withoutSigners);
            BEAST_EXPECT(
                info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            BEAST_EXPECT(!info[jss::result][jss::account_data].isMember(
                jss::signer_lists));
            BEAST_EXPECT(
                info.isMember(jss::jsonrpc) && info[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                info.isMember(jss::ripplerpc) && info[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(info.isMember(jss::id) && info[jss::id] == 5);
        }
        {
            // account_info with the "signer_lists" argument.
            auto const info = env.rpc("json2", withSigners);
            BEAST_EXPECT(
                info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            auto const& data = info[jss::result][jss::account_data];
            BEAST_EXPECT(data.isMember(jss::signer_lists));
            auto const& signerLists = data[jss::signer_lists];
            BEAST_EXPECT(signerLists.isArray());
            BEAST_EXPECT(signerLists.size() == 0);
            BEAST_EXPECT(
                info.isMember(jss::jsonrpc) && info[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                info.isMember(jss::ripplerpc) && info[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(info.isMember(jss::id) && info[jss::id] == 6);
        }
        {
            // Do both of the above as a batch job
            auto const info = env.rpc(
                "json2", '[' + withoutSigners + ", " + withSigners + ']');
            BEAST_EXPECT(
                info[0u].isMember(jss::result) &&
                info[0u][jss::result].isMember(jss::account_data));
            BEAST_EXPECT(!info[0u][jss::result][jss::account_data].isMember(
                jss::signer_lists));
            BEAST_EXPECT(
                info[0u].isMember(jss::jsonrpc) &&
                info[0u][jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                info[0u].isMember(jss::ripplerpc) &&
                info[0u][jss::ripplerpc] == "2.0");
            BEAST_EXPECT(info[0u].isMember(jss::id) && info[0u][jss::id] == 5);

            BEAST_EXPECT(
                info[1u].isMember(jss::result) &&
                info[1u][jss::result].isMember(jss::account_data));
            auto const& data = info[1u][jss::result][jss::account_data];
            BEAST_EXPECT(data.isMember(jss::signer_lists));
            auto const& signerLists = data[jss::signer_lists];
            BEAST_EXPECT(signerLists.isArray());
            BEAST_EXPECT(signerLists.size() == 0);
            BEAST_EXPECT(
                info[1u].isMember(jss::jsonrpc) &&
                info[1u][jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                info[1u].isMember(jss::ripplerpc) &&
                info[1u][jss::ripplerpc] == "2.0");
            BEAST_EXPECT(info[1u].isMember(jss::id) && info[1u][jss::id] == 6);
        }

        // Give alice a SignerList.
        Account const bogie{"bogie"};

        Json::Value const smallSigners = signers(alice, 2, {{bogie, 3}});
        env(smallSigners);
        {
            // account_info without the "signer_lists" argument.
            auto const info = env.rpc("json2", withoutSigners);
            BEAST_EXPECT(
                info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            BEAST_EXPECT(!info[jss::result][jss::account_data].isMember(
                jss::signer_lists));
            BEAST_EXPECT(
                info.isMember(jss::jsonrpc) && info[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                info.isMember(jss::ripplerpc) && info[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(info.isMember(jss::id) && info[jss::id] == 5);
        }
        {
            // account_info with the "signer_lists" argument.
            auto const info = env.rpc("json2", withSigners);
            BEAST_EXPECT(
                info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            auto const& data = info[jss::result][jss::account_data];
            BEAST_EXPECT(data.isMember(jss::signer_lists));
            auto const& signerLists = data[jss::signer_lists];
            BEAST_EXPECT(signerLists.isArray());
            BEAST_EXPECT(signerLists.size() == 1);
            auto const& signers = signerLists[0u];
            BEAST_EXPECT(signers.isObject());
            BEAST_EXPECT(signers[sfSignerQuorum.jsonName] == 2);
            auto const& signerEntries = signers[sfSignerEntries.jsonName];
            BEAST_EXPECT(signerEntries.size() == 1);
            auto const& entry0 = signerEntries[0u][sfSignerEntry.jsonName];
            BEAST_EXPECT(entry0[sfSignerWeight.jsonName] == 3);
            BEAST_EXPECT(
                info.isMember(jss::jsonrpc) && info[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                info.isMember(jss::ripplerpc) && info[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(info.isMember(jss::id) && info[jss::id] == 6);
        }

        // Give alice a big signer list
        Account const demon{"demon"};
        Account const ghost{"ghost"};
        Account const haunt{"haunt"};
        Account const jinni{"jinni"};
        Account const phase{"phase"};
        Account const shade{"shade"};
        Account const spook{"spook"};

        Json::Value const bigSigners = signers(
            alice,
            4,
            {
                {bogie, 1},
                {demon, 1},
                {ghost, 1},
                {haunt, 1},
                {jinni, 1},
                {phase, 1},
                {shade, 1},
                {spook, 1},
            });
        env(bigSigners);
        {
            // account_info with the "signer_lists" argument.
            auto const info = env.rpc("json2", withSigners);
            BEAST_EXPECT(
                info.isMember(jss::result) &&
                info[jss::result].isMember(jss::account_data));
            auto const& data = info[jss::result][jss::account_data];
            BEAST_EXPECT(data.isMember(jss::signer_lists));
            auto const& signerLists = data[jss::signer_lists];
            BEAST_EXPECT(signerLists.isArray());
            BEAST_EXPECT(signerLists.size() == 1);
            auto const& signers = signerLists[0u];
            BEAST_EXPECT(signers.isObject());
            BEAST_EXPECT(signers[sfSignerQuorum.jsonName] == 4);
            auto const& signerEntries = signers[sfSignerEntries.jsonName];
            BEAST_EXPECT(signerEntries.size() == 8);
            for (unsigned i = 0u; i < 8; ++i)
            {
                auto const& entry = signerEntries[i][sfSignerEntry.jsonName];
                BEAST_EXPECT(entry.size() == 2);
                BEAST_EXPECT(entry.isMember(sfAccount.jsonName));
                BEAST_EXPECT(entry[sfSignerWeight.jsonName] == 1);
            }
            BEAST_EXPECT(
                info.isMember(jss::jsonrpc) && info[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(
                info.isMember(jss::ripplerpc) && info[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(info.isMember(jss::id) && info[jss::id] == 6);
        }
    }

    // gRPC stuff
    class GetAccountInfoClient : public GRPCTestClientBase
    {
    public:
        org::xrpl::rpc::v1::GetAccountInfoRequest request;
        org::xrpl::rpc::v1::GetAccountInfoResponse reply;

        explicit GetAccountInfoClient(std::string const& port)
            : GRPCTestClientBase(port)
        {
        }

        void
        GetAccountInfo()
        {
            status = stub_->GetAccountInfo(&context, request, &reply);
        }
    };

    void
    testSimpleGrpc()
    {
        testcase("gRPC simple");

        using namespace jtx;
        std::unique_ptr<Config> config = envconfig(addGrpcConfig);
        std::string grpcPort = *(*config)["port_grpc"].get<std::string>("port");
        Env env(*this, std::move(config));
        Account const alice{"alice"};
        env.fund(drops(1000 * 1000 * 1000), alice);

        {
            // most simple case
            GetAccountInfoClient client(grpcPort);
            client.request.mutable_account()->set_address(alice.human());
            client.GetAccountInfo();
            if (!BEAST_EXPECT(client.status.ok()))
            {
                return;
            }
            BEAST_EXPECT(
                client.reply.account_data().account().value().address() ==
                alice.human());
        }
        {
            GetAccountInfoClient client(grpcPort);
            client.request.mutable_account()->set_address(alice.human());
            client.request.set_queue(true);
            client.request.mutable_ledger()->set_sequence(3);
            client.GetAccountInfo();
            if (!BEAST_EXPECT(client.status.ok()))
                return;
            BEAST_EXPECT(
                client.reply.account_data()
                    .balance()
                    .value()
                    .xrp_amount()
                    .drops() == 1000 * 1000 * 1000);
            BEAST_EXPECT(
                client.reply.account_data().account().value().address() ==
                alice.human());
            BEAST_EXPECT(
                client.reply.account_data().sequence().value() ==
                env.seq(alice));
            BEAST_EXPECT(client.reply.queue_data().txn_count() == 0);
        }
    }

    void
    testErrorsGrpc()
    {
        testcase("gRPC errors");

        using namespace jtx;
        std::unique_ptr<Config> config = envconfig(addGrpcConfig);
        std::string grpcPort = *(*config)["port_grpc"].get<std::string>("port");
        Env env(*this, std::move(config));
        auto getClient = [&grpcPort]() {
            return GetAccountInfoClient(grpcPort);
        };
        Account const alice{"alice"};
        env.fund(drops(1000 * 1000 * 1000), alice);

        {
            // bad address
            auto client = getClient();
            client.request.mutable_account()->set_address("deadbeef");
            client.GetAccountInfo();
            BEAST_EXPECT(!client.status.ok());
        }
        {
            // no account
            Account const bogie{"bogie"};
            auto client = getClient();
            client.request.mutable_account()->set_address(bogie.human());
            client.GetAccountInfo();
            BEAST_EXPECT(!client.status.ok());
        }
        {
            // bad ledger_index
            auto client = getClient();
            client.request.mutable_account()->set_address(alice.human());
            client.request.mutable_ledger()->set_sequence(0);
            client.GetAccountInfo();
            BEAST_EXPECT(!client.status.ok());
        }
    }

    void
    testSignerListsGrpc()
    {
        testcase("gRPC singer lists");

        using namespace jtx;
        std::unique_ptr<Config> config = envconfig(addGrpcConfig);
        std::string grpcPort = *(*config)["port_grpc"].get<std::string>("port");
        Env env(*this, std::move(config));
        auto getClient = [&grpcPort]() {
            return GetAccountInfoClient(grpcPort);
        };

        Account const alice{"alice"};
        env.fund(drops(1000 * 1000 * 1000), alice);

        {
            auto client = getClient();
            client.request.mutable_account()->set_address(alice.human());
            client.request.set_signer_lists(true);
            client.GetAccountInfo();
            if (!BEAST_EXPECT(client.status.ok()))
                return;
            BEAST_EXPECT(client.reply.signer_list().signer_entries_size() == 0);
        }

        // Give alice a SignerList.
        Account const bogie{"bogie"};
        Json::Value const smallSigners = signers(alice, 2, {{bogie, 3}});
        env(smallSigners);
        {
            auto client = getClient();
            client.request.mutable_account()->set_address(alice.human());
            client.request.set_signer_lists(false);
            client.GetAccountInfo();
            if (!BEAST_EXPECT(client.status.ok()))
                return;
            BEAST_EXPECT(client.reply.signer_list().signer_entries_size() == 0);
        }
        {
            auto client = getClient();
            client.request.mutable_account()->set_address(alice.human());
            client.request.set_signer_lists(true);
            client.GetAccountInfo();
            if (!BEAST_EXPECT(client.status.ok()))
            {
                return;
            }
            BEAST_EXPECT(
                client.reply.account_data().owner_count().value() == 1);
            BEAST_EXPECT(client.reply.signer_list().signer_entries_size() == 1);
        }

        // Give alice a big signer list
        Account const demon{"demon"};
        Account const ghost{"ghost"};
        Account const haunt{"haunt"};
        Account const jinni{"jinni"};
        Account const phase{"phase"};
        Account const shade{"shade"};
        Account const spook{"spook"};
        Json::Value const bigSigners = signers(
            alice,
            4,
            {
                {bogie, 1},
                {demon, 1},
                {ghost, 1},
                {haunt, 1},
                {jinni, 1},
                {phase, 1},
                {shade, 1},
                {spook, 1},
            });
        env(bigSigners);

        std::set<std::string> accounts;
        accounts.insert(bogie.human());
        accounts.insert(demon.human());
        accounts.insert(ghost.human());
        accounts.insert(haunt.human());
        accounts.insert(jinni.human());
        accounts.insert(phase.human());
        accounts.insert(shade.human());
        accounts.insert(spook.human());
        {
            auto client = getClient();
            client.request.mutable_account()->set_address(alice.human());
            client.request.set_signer_lists(true);
            client.GetAccountInfo();
            if (!BEAST_EXPECT(client.status.ok()))
            {
                return;
            }
            BEAST_EXPECT(
                client.reply.account_data().owner_count().value() == 1);
            auto& signerList = client.reply.signer_list();
            BEAST_EXPECT(signerList.signer_quorum().value() == 4);
            BEAST_EXPECT(signerList.signer_entries_size() == 8);
            for (int i = 0; i < 8; ++i)
            {
                BEAST_EXPECT(
                    signerList.signer_entries(i).signer_weight().value() == 1);
                BEAST_EXPECT(
                    accounts.erase(signerList.signer_entries(i)
                                       .account()
                                       .value()
                                       .address()) == 1);
            }
            BEAST_EXPECT(accounts.size() == 0);
        }
    }

    void
    run() override
    {
        testErrors();
        testSignerLists();
        testSignerListsV2();
        testSimpleGrpc();
        testErrorsGrpc();
        testSignerListsGrpc();
    }
};

BEAST_DEFINE_TESTSUITE(AccountInfo, app, ripple);

}  // namespace test
}  // namespace ripple
