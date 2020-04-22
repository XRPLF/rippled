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
#include <test/jtx/Env.h>
#include <test/jtx/TrustedPublisherServer.h>
#include <test/jtx/envconfig.h>
#include <test/nodestore/TestBase.h>

namespace ripple {
namespace test {

class ShardArchiveHandler_test : public beast::unit_test::suite
{
    using Downloads = std::vector<std::pair<uint32_t, std::string>>;

    TrustedPublisherServer
    createServer(jtx::Env& env, bool ssl = true)
    {
        std::vector<TrustedPublisherServer::Validator> list;
        list.push_back(TrustedPublisherServer::randomValidator());
        return TrustedPublisherServer{
            env.app().getIOService(),
            list,
            env.timeKeeper().now() + std::chrono::seconds{3600},
            ssl};
    }

public:
    void
    testStateDatabase1()
    {
        testcase("testStateDatabase1");

        {
            beast::temp_dir tempDir;

            auto c = jtx::envconfig();
            auto& section = c->section(ConfigSection::shardDatabase());
            section.set("path", tempDir.path());
            section.set("max_size_gb", "100");
            c->setupControl(true, true, true);

            jtx::Env env(*this, std::move(c));
            auto handler = RPC::ShardArchiveHandler::getInstance(
                env.app(), env.app().getJobQueue());
            BEAST_EXPECT(handler);

            BEAST_EXPECT(handler->init());

            std::string const rawUrl = "https://foo:443/1.tar.lz4";
            parsedURL url;

            parseUrl(url, rawUrl);
            handler->add(1, {url, rawUrl});

            {
                std::lock_guard<std::mutex> lock(handler->m_);

                auto& session{handler->sqliteDB_->getSession()};

                soci::rowset<soci::row> rs =
                    (session.prepare << "SELECT * FROM State;");

                uint64_t rowCount = 0;

                for (auto it = rs.begin(); it != rs.end(); ++it, ++rowCount)
                {
                    BEAST_EXPECT(it->get<int>(0) == 1);
                    BEAST_EXPECT(it->get<std::string>(1) == rawUrl);
                }

                BEAST_EXPECT(rowCount == 1);
            }

            handler->release();
        }

        // Destroy the singleton so we start fresh in
        // the next testcase.
        RPC::ShardArchiveHandler::instance_.reset();
    }

    void
    testStateDatabase2()
    {
        testcase("testStateDatabase2");

        {
            beast::temp_dir tempDir;

            auto c = jtx::envconfig();
            auto& section = c->section(ConfigSection::shardDatabase());
            section.set("path", tempDir.path());
            section.set("max_size_gb", "100");
            c->setupControl(true, true, true);

            jtx::Env env(*this, std::move(c));
            auto handler = RPC::ShardArchiveHandler::getInstance(
                env.app(), env.app().getJobQueue());
            BEAST_EXPECT(handler);

            BEAST_EXPECT(handler->init());

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

                uint64_t pos = 0;
                for (auto it = rs.begin(); it != rs.end(); ++it, ++pos)
                {
                    BEAST_EXPECT(it->get<int>(0) == dl[pos].first);
                    BEAST_EXPECT(it->get<std::string>(1) == dl[pos].second);
                }

                BEAST_EXPECT(pos == dl.size());
            }

            handler->release();
        }

        // Destroy the singleton so we start fresh in
        // the next testcase.
        RPC::ShardArchiveHandler::instance_.reset();
    }

    void
    testStateDatabase3()
    {
        testcase("testStateDatabase3");

        {
            beast::temp_dir tempDir;

            auto c = jtx::envconfig();
            auto& section = c->section(ConfigSection::shardDatabase());
            section.set("path", tempDir.path());
            section.set("max_size_gb", "100");
            c->setupControl(true, true, true);

            jtx::Env env(*this, std::move(c));
            auto handler = RPC::ShardArchiveHandler::getInstance(
                env.app(), env.app().getJobQueue());
            BEAST_EXPECT(handler);

            BEAST_EXPECT(handler->init());

            auto server = createServer(env);
            auto host = server.local_endpoint().address().to_string();
            auto port = std::to_string(server.local_endpoint().port());
            server.stop();

            Downloads const dl = [&host, &port] {
                Downloads ret;

                for (int i = 1; i <= 10; ++i)
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

            BEAST_EXPECT(handler->start());

            auto stateDir = RPC::ShardArchiveHandler::getDownloadDirectory(
                env.app().config());

            std::unique_lock<std::mutex> lock(handler->m_);

            BEAST_EXPECT(
                boost::filesystem::exists(stateDir) ||
                handler->archives_.empty());

            while (!handler->archives_.empty())
            {
                lock.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                lock.lock();
            }

            BEAST_EXPECT(!boost::filesystem::exists(stateDir));
        }

        // Destroy the singleton so we start fresh in
        // the next testcase.
        RPC::ShardArchiveHandler::instance_.reset();
    }

    void
    testStateDatabase4()
    {
        testcase("testStateDatabase4");

        beast::temp_dir tempDir;

        {
            auto c = jtx::envconfig();
            auto& section = c->section(ConfigSection::shardDatabase());
            section.set("path", tempDir.path());
            section.set("max_size_gb", "100");
            c->setupControl(true, true, true);

            jtx::Env env(*this, std::move(c));
            auto handler = RPC::ShardArchiveHandler::getInstance(
                env.app(), env.app().getJobQueue());
            BEAST_EXPECT(handler);

            BEAST_EXPECT(handler->init());

            auto server = createServer(env);
            auto host = server.local_endpoint().address().to_string();
            auto port = std::to_string(server.local_endpoint().port());
            server.stop();

            Downloads const dl = [&host, &port] {
                Downloads ret;

                for (int i = 1; i <= 10; ++i)
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

            while (!handler->archives_.empty())
            {
                lock.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                lock.lock();
            }

            BEAST_EXPECT(!boost::filesystem::exists(stateDir));

            boost::filesystem::create_directory(stateDir);

            boost::filesystem::copy_file(
                boost::filesystem::path(tempDir.path()) / stateDBName,
                stateDir / stateDBName);
        }

        // Destroy the singleton so we start fresh in
        // the new scope.
        RPC::ShardArchiveHandler::instance_.reset();

        auto c = jtx::envconfig();
        auto& section = c->section(ConfigSection::shardDatabase());
        section.set("path", tempDir.path());
        section.set("max_size_gb", "100");
        c->setupControl(true, true, true);

        jtx::Env env(*this, std::move(c));

        while (!RPC::ShardArchiveHandler::hasInstance())
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

        BEAST_EXPECT(RPC::ShardArchiveHandler::hasInstance());

        auto handler = RPC::ShardArchiveHandler::getInstance();

        auto stateDir =
            RPC::ShardArchiveHandler::getDownloadDirectory(env.app().config());

        std::unique_lock<std::mutex> lock(handler->m_);

        BEAST_EXPECT(
            boost::filesystem::exists(stateDir) || handler->archives_.empty());

        while (!handler->archives_.empty())
        {
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            lock.lock();
        }

        BEAST_EXPECT(!boost::filesystem::exists(stateDir));
    }

    void
    run() override
    {
        testStateDatabase1();
        testStateDatabase2();
        testStateDatabase3();
        testStateDatabase4();
    }
};

BEAST_DEFINE_TESTSUITE(ShardArchiveHandler, app, ripple);

}  // namespace test
}  // namespace ripple
