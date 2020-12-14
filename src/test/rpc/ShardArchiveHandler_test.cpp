//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#include <ripple/beast/utility/temp_dir.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/nodestore/DummyScheduler.h>
#include <ripple/nodestore/Manager.h>
#include <ripple/nodestore/impl/DecodedBlob.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/ShardArchiveHandler.h>
#include <test/jtx/CaptureLogs.h>
#include <test/jtx/Env.h>
#include <test/jtx/TrustedPublisherServer.h>
#include <test/jtx/envconfig.h>
#include <test/nodestore/TestBase.h>

namespace ripple {
namespace test {

class ShardArchiveHandler_test : public beast::unit_test::suite
{
    using Downloads = std::vector<std::pair<std::uint32_t, std::string>>;

    std::shared_ptr<TrustedPublisherServer>
    createServer(jtx::Env& env, bool ssl = true)
    {
        std::vector<TrustedPublisherServer::Validator> list;
        list.push_back(TrustedPublisherServer::randomValidator());
        return make_TrustedPublisherServer(
            env.app().getIOService(),
            list,
            env.timeKeeper().now() + std::chrono::seconds{3600},
            // No future VLs
            {},
            ssl);
    }

public:
    // Test the shard downloading module by queueing
    // a download and verifying the contents of the
    // state database.
    void
    testSingleDownloadAndStateDB()
    {
        testcase("testSingleDownloadAndStateDB");

        beast::temp_dir tempDir;

        auto c = jtx::envconfig();
        auto& section = c->section(ConfigSection::shardDatabase());
        section.set("path", tempDir.path());
        section.set("max_historical_shards", "20");
        c->setupControl(true, true, true);

        jtx::Env env(*this, std::move(c));
        auto handler = env.app().getShardArchiveHandler();
        BEAST_EXPECT(handler);
        BEAST_EXPECT(dynamic_cast<RPC::RecoveryHandler*>(handler) == nullptr);

        std::string const rawUrl = "https://foo:443/1.tar.lz4";
        parsedURL url;

        parseUrl(url, rawUrl);
        handler->add(1, {url, rawUrl});

        {
            std::lock_guard<std::mutex> lock(handler->m_);

            auto& session{handler->sqliteDB_->getSession()};

            soci::rowset<soci::row> rs =
                (session.prepare << "SELECT * FROM State;");

            std::uint64_t rowCount = 0;

            for (auto it = rs.begin(); it != rs.end(); ++it, ++rowCount)
            {
                BEAST_EXPECT(it->get<int>(0) == 1);
                BEAST_EXPECT(it->get<std::string>(1) == rawUrl);
            }

            BEAST_EXPECT(rowCount == 1);
        }

        handler->release();
    }

    // Test the shard downloading module by queueing
    // three downloads and verifying the contents of
    // the state database.
    void
    testDownloadsAndStateDB()
    {
        testcase("testDownloadsAndStateDB");

        beast::temp_dir tempDir;

        auto c = jtx::envconfig();
        auto& section = c->section(ConfigSection::shardDatabase());
        section.set("path", tempDir.path());
        section.set("max_historical_shards", "20");
        c->setupControl(true, true, true);

        jtx::Env env(*this, std::move(c));
        auto handler = env.app().getShardArchiveHandler();
        BEAST_EXPECT(handler);
        BEAST_EXPECT(dynamic_cast<RPC::RecoveryHandler*>(handler) == nullptr);

        Downloads const dl = {
            {1, "https://foo:443/1.tar.lz4"},
            {2, "https://foo:443/2.tar.lz4"},
            {3, "https://foo:443/3.tar.lz4"}};

        for (auto const& entry : dl)
        {
            parsedURL url;
            parseUrl(url, entry.second);
            handler->add(entry.first, {url, entry.second});
        }

        {
            std::lock_guard<std::mutex> lock(handler->m_);

            auto& session{handler->sqliteDB_->getSession()};
            soci::rowset<soci::row> rs =
                (session.prepare << "SELECT * FROM State;");

            std::uint64_t pos = 0;
            for (auto it = rs.begin(); it != rs.end(); ++it, ++pos)
            {
                BEAST_EXPECT(it->get<int>(0) == dl[pos].first);
                BEAST_EXPECT(it->get<std::string>(1) == dl[pos].second);
            }

            BEAST_EXPECT(pos == dl.size());
        }

        handler->release();
    }

    // Test the shard downloading module by initiating
    // and completing ten downloads and verifying the
    // contents of the filesystem and the handler's
    // archives.
    void
    testDownloadsAndFileSystem()
    {
        testcase("testDownloadsAndFileSystem");

        beast::temp_dir tempDir;

        auto c = jtx::envconfig();
        auto& section = c->section(ConfigSection::shardDatabase());
        section.set("path", tempDir.path());
        section.set("max_historical_shards", "20");
        section.set("ledgers_per_shard", "256");
        section.set("earliest_seq", "257");
        auto& sectionNode = c->section(ConfigSection::nodeDatabase());
        sectionNode.set("earliest_seq", "257");
        c->setupControl(true, true, true);

        jtx::Env env(*this, std::move(c));

        std::uint8_t const numberOfDownloads = 10;

        // Create some ledgers so that the ShardArchiveHandler
        // can verify the last ledger hash for the shard
        // downloads.
        for (int i = 0; i < env.app().getShardStore()->ledgersPerShard() *
                 (numberOfDownloads + 1);
             ++i)
        {
            env.close();
        }

        auto handler = env.app().getShardArchiveHandler();
        BEAST_EXPECT(handler);
        BEAST_EXPECT(dynamic_cast<RPC::RecoveryHandler*>(handler) == nullptr);

        auto server = createServer(env);
        auto host = server->local_endpoint().address().to_string();
        auto port = std::to_string(server->local_endpoint().port());
        server->stop();

        Downloads const dl = [count = numberOfDownloads, &host, &port] {
            Downloads ret;

            for (int i = 1; i <= count; ++i)
            {
                ret.push_back(
                    {i,
                     (boost::format("https://%s:%d/%d.tar.lz4") % host % port %
                      i)
                         .str()});
            }

            return ret;
        }();

        for (auto const& entry : dl)
        {
            parsedURL url;
            parseUrl(url, entry.second);
            handler->add(entry.first, {url, entry.second});
        }

        BEAST_EXPECT(handler->start());

        auto stateDir =
            RPC::ShardArchiveHandler::getDownloadDirectory(env.app().config());

        std::unique_lock<std::mutex> lock(handler->m_);

        BEAST_EXPECT(
            boost::filesystem::exists(stateDir) || handler->archives_.empty());

        using namespace std::chrono_literals;
        auto waitMax = 60s;

        while (!handler->archives_.empty())
        {
            lock.unlock();
            std::this_thread::sleep_for(1s);

            if (waitMax -= 1s; waitMax <= 0s)
            {
                BEAST_EXPECT(false);
                break;
            }

            lock.lock();
        }

        BEAST_EXPECT(!boost::filesystem::exists(stateDir));
    }

    // Test the shard downloading module by initiating
    // and completing ten downloads and verifying the
    // contents of the filesystem and the handler's
    // archives. Then restart the application and ensure
    // that the handler is created and started automatically.
    void
    testDownloadsAndRestart()
    {
        testcase("testDownloadsAndRestart");

        beast::temp_dir tempDir;

        {
            auto c = jtx::envconfig();
            auto& section = c->section(ConfigSection::shardDatabase());
            section.set("path", tempDir.path());
            section.set("max_historical_shards", "20");
            section.set("ledgers_per_shard", "256");
            section.set("earliest_seq", "257");
            auto& sectionNode = c->section(ConfigSection::nodeDatabase());
            sectionNode.set("earliest_seq", "257");
            c->setupControl(true, true, true);

            jtx::Env env(*this, std::move(c));

            std::uint8_t const numberOfDownloads = 10;

            // Create some ledgers so that the ShardArchiveHandler
            // can verify the last ledger hash for the shard
            // downloads.
            for (int i = 0; i < env.app().getShardStore()->ledgersPerShard() *
                     (numberOfDownloads + 1);
                 ++i)
            {
                env.close();
            }

            auto handler = env.app().getShardArchiveHandler();
            BEAST_EXPECT(handler);
            BEAST_EXPECT(
                dynamic_cast<RPC::RecoveryHandler*>(handler) == nullptr);

            auto server = createServer(env);
            auto host = server->local_endpoint().address().to_string();
            auto port = std::to_string(server->local_endpoint().port());
            server->stop();

            Downloads const dl = [count = numberOfDownloads, &host, &port] {
                Downloads ret;

                for (int i = 1; i <= count; ++i)
                {
                    ret.push_back(
                        {i,
                         (boost::format("https://%s:%d/%d.tar.lz4") % host %
                          port % i)
                             .str()});
                }

                return ret;
            }();

            for (auto const& entry : dl)
            {
                parsedURL url;
                parseUrl(url, entry.second);
                handler->add(entry.first, {url, entry.second});
            }

            auto stateDir = RPC::ShardArchiveHandler::getDownloadDirectory(
                env.app().config());

            boost::filesystem::copy_file(
                stateDir / stateDBName,
                boost::filesystem::path(tempDir.path()) / stateDBName);

            BEAST_EXPECT(handler->start());

            std::unique_lock<std::mutex> lock(handler->m_);

            BEAST_EXPECT(
                boost::filesystem::exists(stateDir) ||
                handler->archives_.empty());

            using namespace std::chrono_literals;
            auto waitMax = 60s;

            while (!handler->archives_.empty())
            {
                lock.unlock();
                std::this_thread::sleep_for(1s);

                if (waitMax -= 1s; waitMax <= 0s)
                {
                    BEAST_EXPECT(false);
                    break;
                }

                lock.lock();
            }

            BEAST_EXPECT(!boost::filesystem::exists(stateDir));

            boost::filesystem::create_directory(stateDir);

            boost::filesystem::copy_file(
                boost::filesystem::path(tempDir.path()) / stateDBName,
                stateDir / stateDBName);
        }

        auto c = jtx::envconfig();
        auto& section = c->section(ConfigSection::shardDatabase());
        section.set("path", tempDir.path());
        section.set("max_historical_shards", "20");
        section.set("ledgers_per_shard", "256");
        section.set("shard_verification_retry_interval", "1");
        section.set("shard_verification_max_attempts", "10000");
        section.set("earliest_seq", "257");
        auto& sectionNode = c->section(ConfigSection::nodeDatabase());
        sectionNode.set("earliest_seq", "257");
        c->setupControl(true, true, true);

        jtx::Env env(*this, std::move(c));

        std::uint8_t const numberOfDownloads = 10;

        // Create some ledgers so that the ShardArchiveHandler
        // can verify the last ledger hash for the shard
        // downloads.
        for (int i = 0; i < env.app().getShardStore()->ledgersPerShard() *
                 (numberOfDownloads + 1);
             ++i)
        {
            env.close();
        }

        auto handler = env.app().getShardArchiveHandler();
        BEAST_EXPECT(dynamic_cast<RPC::RecoveryHandler*>(handler) != nullptr);

        auto stateDir =
            RPC::ShardArchiveHandler::getDownloadDirectory(env.app().config());

        std::unique_lock<std::mutex> lock(handler->m_);

        BEAST_EXPECT(
            boost::filesystem::exists(stateDir) || handler->archives_.empty());

        using namespace std::chrono_literals;
        auto waitMax = 60s;

        while (!handler->archives_.empty())
        {
            lock.unlock();
            std::this_thread::sleep_for(1s);

            if (waitMax -= 1s; waitMax <= 0s)
            {
                BEAST_EXPECT(false);
                break;
            }

            lock.lock();
        }

        BEAST_EXPECT(!boost::filesystem::exists(stateDir));
    }

    // Ensure that downloads fail when the shard
    // database cannot store any more shards
    void
    testShardCountFailure()
    {
        testcase("testShardCountFailure");
        std::string capturedLogs;

        {
            beast::temp_dir tempDir;

            auto c = jtx::envconfig();
            auto& section = c->section(ConfigSection::shardDatabase());
            section.set("path", tempDir.path());
            section.set("max_historical_shards", "1");
            section.set("ledgers_per_shard", "256");
            section.set("earliest_seq", "257");
            auto& sectionNode = c->section(ConfigSection::nodeDatabase());
            sectionNode.set("earliest_seq", "257");
            c->setupControl(true, true, true);

            std::unique_ptr<Logs> logs(new CaptureLogs(&capturedLogs));
            jtx::Env env(*this, std::move(c), std::move(logs));

            std::uint8_t const numberOfDownloads = 10;

            // Create some ledgers so that the ShardArchiveHandler
            // can verify the last ledger hash for the shard
            // downloads.
            for (int i = 0; i < env.app().getShardStore()->ledgersPerShard() *
                     (numberOfDownloads + 1);
                 ++i)
            {
                env.close();
            }

            auto handler = env.app().getShardArchiveHandler();
            BEAST_EXPECT(handler);
            BEAST_EXPECT(
                dynamic_cast<RPC::RecoveryHandler*>(handler) == nullptr);

            auto server = createServer(env);
            auto host = server->local_endpoint().address().to_string();
            auto port = std::to_string(server->local_endpoint().port());
            server->stop();

            Downloads const dl = [count = numberOfDownloads, &host, &port] {
                Downloads ret;

                for (int i = 1; i <= count; ++i)
                {
                    ret.push_back(
                        {i,
                         (boost::format("https://%s:%d/%d.tar.lz4") % host %
                          port % i)
                             .str()});
                }

                return ret;
            }();

            for (auto const& entry : dl)
            {
                parsedURL url;
                parseUrl(url, entry.second);
                handler->add(entry.first, {url, entry.second});
            }

            BEAST_EXPECT(!handler->start());
            auto stateDir = RPC::ShardArchiveHandler::getDownloadDirectory(
                env.app().config());

            handler->release();
            BEAST_EXPECT(!boost::filesystem::exists(stateDir));
        }

        auto const expectedErrorMessage =
            "shards 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 maximum number of historical "
            "shards reached";
        BEAST_EXPECT(
            capturedLogs.find(expectedErrorMessage) != std::string::npos);

        {
            beast::temp_dir tempDir;

            auto c = jtx::envconfig();
            auto& section = c->section(ConfigSection::shardDatabase());
            section.set("path", tempDir.path());
            section.set("max_historical_shards", "0");
            section.set("ledgers_per_shard", "256");
            section.set("earliest_seq", "257");
            auto& sectionNode = c->section(ConfigSection::nodeDatabase());
            sectionNode.set("earliest_seq", "257");
            c->setupControl(true, true, true);

            std::unique_ptr<Logs> logs(new CaptureLogs(&capturedLogs));
            jtx::Env env(*this, std::move(c), std::move(logs));

            std::uint8_t const numberOfDownloads = 1;

            // Create some ledgers so that the ShardArchiveHandler
            // can verify the last ledger hash for the shard
            // downloads.
            for (int i = 0; i < env.app().getShardStore()->ledgersPerShard() *
                     ((numberOfDownloads * 3) + 1);
                 ++i)
            {
                env.close();
            }

            auto handler = env.app().getShardArchiveHandler();
            BEAST_EXPECT(handler);
            BEAST_EXPECT(
                dynamic_cast<RPC::RecoveryHandler*>(handler) == nullptr);

            auto server = createServer(env);
            auto host = server->local_endpoint().address().to_string();
            auto port = std::to_string(server->local_endpoint().port());
            server->stop();

            Downloads const dl = [count = numberOfDownloads, &host, &port] {
                Downloads ret;

                for (int i = 1; i <= count; ++i)
                {
                    ret.push_back(
                        {i,
                         (boost::format("https://%s:%d/%d.tar.lz4") % host %
                          port % i)
                             .str()});
                }

                return ret;
            }();

            for (auto const& entry : dl)
            {
                parsedURL url;
                parseUrl(url, entry.second);
                handler->add(entry.first, {url, entry.second});
            }

            BEAST_EXPECT(!handler->start());
            auto stateDir = RPC::ShardArchiveHandler::getDownloadDirectory(
                env.app().config());

            handler->release();
            BEAST_EXPECT(!boost::filesystem::exists(stateDir));
        }

        auto const expectedErrorMessage2 =
            "shard 1 maximum number of historical shards reached";
        BEAST_EXPECT(
            capturedLogs.find(expectedErrorMessage2) != std::string::npos);
    }

    // Ensure that downloads fail when the shard
    // database has already stored one of the
    // queued shards
    void
    testRedundantShardFailure()
    {
        testcase("testRedundantShardFailure");
        std::string capturedLogs;

        {
            beast::temp_dir tempDir;

            auto c = jtx::envconfig();
            auto& section = c->section(ConfigSection::shardDatabase());
            section.set("path", tempDir.path());
            section.set("max_historical_shards", "1");
            section.set("ledgers_per_shard", "256");
            section.set("earliest_seq", "257");
            auto& sectionNode = c->section(ConfigSection::nodeDatabase());
            sectionNode.set("earliest_seq", "257");
            c->setupControl(true, true, true);

            std::unique_ptr<Logs> logs(new CaptureLogs(&capturedLogs));
            jtx::Env env(
                *this,
                std::move(c),
                std::move(logs),
                beast::severities::kDebug);

            std::uint8_t const numberOfDownloads = 10;

            // Create some ledgers so that the ShardArchiveHandler
            // can verify the last ledger hash for the shard
            // downloads.
            for (int i = 0; i < env.app().getShardStore()->ledgersPerShard() *
                     (numberOfDownloads + 1);
                 ++i)
            {
                env.close();
            }

            env.app().getShardStore()->prepareShards({1});

            auto handler = env.app().getShardArchiveHandler();
            BEAST_EXPECT(handler);
            BEAST_EXPECT(
                dynamic_cast<RPC::RecoveryHandler*>(handler) == nullptr);

            auto server = createServer(env);
            auto host = server->local_endpoint().address().to_string();
            auto port = std::to_string(server->local_endpoint().port());
            server->stop();

            Downloads const dl = [count = numberOfDownloads, &host, &port] {
                Downloads ret;

                for (int i = 1; i <= count; ++i)
                {
                    ret.push_back(
                        {i,
                         (boost::format("https://%s:%d/%d.tar.lz4") % host %
                          port % i)
                             .str()});
                }

                return ret;
            }();

            for (auto const& entry : dl)
            {
                parsedURL url;
                parseUrl(url, entry.second);
                handler->add(entry.first, {url, entry.second});
            }

            BEAST_EXPECT(!handler->start());
            auto stateDir = RPC::ShardArchiveHandler::getDownloadDirectory(
                env.app().config());

            handler->release();
            BEAST_EXPECT(!boost::filesystem::exists(stateDir));
        }

        auto const expectedErrorMessage =
            "shard 1 is already queued for import";
        BEAST_EXPECT(
            capturedLogs.find(expectedErrorMessage) != std::string::npos);
    }

    void
    run() override
    {
        testSingleDownloadAndStateDB();
        testDownloadsAndStateDB();
        testDownloadsAndFileSystem();
        testDownloadsAndRestart();
        testShardCountFailure();
        testRedundantShardFailure();
    }
};

BEAST_DEFINE_TESTSUITE(ShardArchiveHandler, app, ripple);

}  // namespace test
}  // namespace ripple
