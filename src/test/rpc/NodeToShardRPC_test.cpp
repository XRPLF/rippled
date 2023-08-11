//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2021 Ripple Labs Inc.

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

#include <ripple/beast/unit_test.h>
#include <ripple/beast/utility/temp_dir.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/nodestore/DatabaseShard.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <test/jtx/Env.h>

namespace ripple {
namespace test {

class NodeToShardRPC_test : public beast::unit_test::suite
{
    bool
    importCompleted(
        NodeStore::DatabaseShard* shardStore,
        std::uint8_t const numberOfShards,
        Json::Value const& result)
    {
        auto const info = shardStore->getShardInfo();

        // Assume completed if the import isn't running
        auto const completed =
            result[jss::error_message] == "Database import not running";

        if (completed)
        {
            BEAST_EXPECT(
                info->incomplete().size() + info->finalized().size() ==
                numberOfShards);
        }

        return completed;
    }

public:
    void
    testDisabled()
    {
        testcase("Disabled");

        beast::temp_dir tempDir;

        jtx::Env env = [&] {
            auto c = jtx::envconfig();
            auto& sectionNode = c->section(ConfigSection::nodeDatabase());
            sectionNode.set("earliest_seq", "257");
            sectionNode.set("ledgers_per_shard", "256");
            c->setupControl(true, true, true);

            return jtx::Env(*this, std::move(c));
        }();

        std::uint8_t const numberOfShards = 10;

        // Create some ledgers so that we can initiate a
        // shard store database import.
        for (int i = 0; i < 256 * (numberOfShards + 1); ++i)
        {
            env.close();
        }

        {
            auto shardStore = env.app().getShardStore();
            if (!BEAST_EXPECT(!shardStore))
                return;
        }

        {
            // Try the node_to_shard status RPC command. Should fail.

            Json::Value jvParams;
            jvParams[jss::action] = "status";

            auto const result = env.rpc(
                "json", "node_to_shard", to_string(jvParams))[jss::result];

            BEAST_EXPECT(result[jss::error_code] == rpcNOT_ENABLED);
        }

        {
            // Try to start a shard store import via the RPC
            // interface. Should fail.

            Json::Value jvParams;
            jvParams[jss::action] = "start";

            auto const result = env.rpc(
                "json", "node_to_shard", to_string(jvParams))[jss::result];

            BEAST_EXPECT(result[jss::error_code] == rpcNOT_ENABLED);
        }

        {
            // Try the node_to_shard status RPC command. Should fail.

            Json::Value jvParams;
            jvParams[jss::action] = "status";

            auto const result = env.rpc(
                "json", "node_to_shard", to_string(jvParams))[jss::result];

            BEAST_EXPECT(result[jss::error_code] == rpcNOT_ENABLED);
        }
    }

    void
    testStart()
    {
        testcase("Start");

        beast::temp_dir tempDir;

        jtx::Env env = [&] {
            auto c = jtx::envconfig();
            auto& section = c->section(ConfigSection::shardDatabase());
            section.set("path", tempDir.path());
            section.set("max_historical_shards", "20");
            section.set("ledgers_per_shard", "256");
            section.set("earliest_seq", "257");
            auto& sectionNode = c->section(ConfigSection::nodeDatabase());
            sectionNode.set("earliest_seq", "257");
            sectionNode.set("ledgers_per_shard", "256");
            c->setupControl(true, true, true);

            return jtx::Env(*this, std::move(c));
        }();

        std::uint8_t const numberOfShards = 10;

        // Create some ledgers so that we can initiate a
        // shard store database import.
        for (int i = 0; i < env.app().getShardStore()->ledgersPerShard() *
                 (numberOfShards + 1);
             ++i)
        {
            env.close();
        }

        auto shardStore = env.app().getShardStore();
        if (!BEAST_EXPECT(shardStore))
            return;

        {
            // Initiate a shard store import via the RPC
            // interface.

            Json::Value jvParams;
            jvParams[jss::action] = "start";

            auto const result = env.rpc(
                "json", "node_to_shard", to_string(jvParams))[jss::result];

            BEAST_EXPECT(
                result[jss::message] == "Database import initiated...");
        }

        while (!shardStore->getDatabaseImportSequence())
        {
            // Wait until the import starts
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        {
            // Verify that the import is in progress with
            // the node_to_shard status RPC command

            Json::Value jvParams;
            jvParams[jss::action] = "status";

            auto const result = env.rpc(
                "json", "node_to_shard", to_string(jvParams))[jss::result];

            BEAST_EXPECT(
                result[jss::status] == "success" ||
                importCompleted(shardStore, numberOfShards, result));

            std::chrono::seconds const maxWait{180};

            {
                auto const start = std::chrono::system_clock::now();
                while (true)
                {
                    // Verify that the status object accurately
                    // reflects import progress.

                    auto const completeShards =
                        shardStore->getShardInfo()->finalized();

                    if (!completeShards.empty())
                    {
                        auto const result = env.rpc(
                            "json",
                            "node_to_shard",
                            to_string(jvParams))[jss::result];

                        if (!importCompleted(
                                shardStore, numberOfShards, result))
                        {
                            BEAST_EXPECT(result[jss::firstShardIndex] == 1);
                            BEAST_EXPECT(result[jss::lastShardIndex] == 10);
                        }
                    }

                    if (boost::icl::contains(completeShards, 1))
                    {
                        auto const result = env.rpc(
                            "json",
                            "node_to_shard",
                            to_string(jvParams))[jss::result];

                        BEAST_EXPECT(
                            result[jss::currentShardIndex] >= 1 ||
                            importCompleted(
                                shardStore, numberOfShards, result));

                        break;
                    }

                    if (std::this_thread::sleep_for(
                            std::chrono::milliseconds{100});
                        std::chrono::system_clock::now() - start > maxWait)
                    {
                        BEAST_EXPECTS(
                            false,
                            "Import timeout: could just be a slow machine.");
                        break;
                    }
                }
            }

            {
                // Wait for the import to complete
                auto const start = std::chrono::system_clock::now();
                while (!boost::icl::contains(
                    shardStore->getShardInfo()->finalized(), 10))
                {
                    if (std::this_thread::sleep_for(
                            std::chrono::milliseconds{100});
                        std::chrono::system_clock::now() - start > maxWait)
                    {
                        BEAST_EXPECT(importCompleted(
                            shardStore, numberOfShards, result));
                        break;
                    }
                }
            }
        }
    }

    void
    testStop()
    {
        testcase("Stop");

        beast::temp_dir tempDir;

        jtx::Env env = [&] {
            auto c = jtx::envconfig();
            auto& section = c->section(ConfigSection::shardDatabase());
            section.set("path", tempDir.path());
            section.set("max_historical_shards", "20");
            section.set("ledgers_per_shard", "256");
            section.set("earliest_seq", "257");
            auto& sectionNode = c->section(ConfigSection::nodeDatabase());
            sectionNode.set("earliest_seq", "257");
            sectionNode.set("ledgers_per_shard", "256");
            c->setupControl(true, true, true);

            return jtx::Env(
                *this, std::move(c), nullptr, beast::severities::kDisabled);
        }();

        std::uint8_t const numberOfShards = 10;

        // Create some ledgers so that we can initiate a
        // shard store database import.
        for (int i = 0; i < env.app().getShardStore()->ledgersPerShard() *
                 (numberOfShards + 1);
             ++i)
        {
            env.close();
        }

        auto shardStore = env.app().getShardStore();
        if (!BEAST_EXPECT(shardStore))
            return;

        {
            // Initiate a shard store import via the RPC
            // interface.

            Json::Value jvParams;
            jvParams[jss::action] = "start";

            auto const result = env.rpc(
                "json", "node_to_shard", to_string(jvParams))[jss::result];

            BEAST_EXPECT(
                result[jss::message] == "Database import initiated...");
        }

        {
            // Verify that the import is in progress with
            // the node_to_shard status RPC command

            Json::Value jvParams;
            jvParams[jss::action] = "status";

            auto const result = env.rpc(
                "json", "node_to_shard", to_string(jvParams))[jss::result];

            BEAST_EXPECT(
                result[jss::status] == "success" ||
                importCompleted(shardStore, numberOfShards, result));

            std::chrono::seconds const maxWait{30};
            auto const start = std::chrono::system_clock::now();

            while (shardStore->getShardInfo()->finalized().empty())
            {
                // Wait for at least one shard to complete

                if (std::this_thread::sleep_for(std::chrono::milliseconds{100});
                    std::chrono::system_clock::now() - start > maxWait)
                {
                    BEAST_EXPECTS(
                        false, "Import timeout: could just be a slow machine.");
                    break;
                }
            }
        }

        {
            Json::Value jvParams;
            jvParams[jss::action] = "stop";

            auto const result = env.rpc(
                "json", "node_to_shard", to_string(jvParams))[jss::result];

            BEAST_EXPECT(
                result[jss::message] == "Database import halt initiated..." ||
                importCompleted(shardStore, numberOfShards, result));
        }

        std::chrono::seconds const maxWait{30};
        auto const start = std::chrono::system_clock::now();

        while (true)
        {
            // Wait until we can verify that the import has
            // stopped

            Json::Value jvParams;
            jvParams[jss::action] = "status";

            auto const result = env.rpc(
                "json", "node_to_shard", to_string(jvParams))[jss::result];

            // When the import has stopped, polling the
            // status returns an error
            if (result.isMember(jss::error))
            {
                if (BEAST_EXPECT(result.isMember(jss::error_message)))
                {
                    BEAST_EXPECT(
                        result[jss::error_message] ==
                        "Database import not running");
                }

                break;
            }

            if (std::this_thread::sleep_for(std::chrono::milliseconds{100});
                std::chrono::system_clock::now() - start > maxWait)
            {
                BEAST_EXPECTS(
                    false, "Import timeout: could just be a slow machine.");
                break;
            }
        }
    }

    void
    run() override
    {
        testDisabled();
        testStart();
        testStop();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(NodeToShardRPC, rpc, ripple);
}  // namespace test
}  // namespace ripple
