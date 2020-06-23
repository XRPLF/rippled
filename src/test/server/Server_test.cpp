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

#include <ripple/basics/make_SSLContext.h>
#include <ripple/beast/rfc2616.h>
#include <ripple/beast/unit_test.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/server/Server.h>
#include <ripple/server/Session.h>
#include <boost/asio.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/optional.hpp>
#include <boost/utility/in_place_factory.hpp>
#include <chrono>
#include <stdexcept>
#include <test/jtx.h>
#include <test/jtx/CaptureLogs.h>
#include <test/jtx/envconfig.h>
#include <test/unit_test/SuiteJournal.h>
#include <thread>

namespace ripple {
namespace test {

using socket_type = boost::beast::tcp_stream;
using stream_type = boost::beast::ssl_stream<socket_type>;

class Server_test : public beast::unit_test::suite
{
public:
    class TestThread
    {
    private:
        boost::asio::io_service io_service_;
        boost::optional<boost::asio::io_service::work> work_;
        std::thread thread_;

    public:
        TestThread()
            : work_(boost::in_place(std::ref(io_service_)))
            , thread_([&]() { this->io_service_.run(); })
        {
        }

        ~TestThread()
        {
            work_ = boost::none;
            thread_.join();
        }

        boost::asio::io_service&
        get_io_service()
        {
            return io_service_;
        }
    };

    //--------------------------------------------------------------------------

    class TestSink : public beast::Journal::Sink
    {
        beast::unit_test::suite& suite_;

    public:
        explicit TestSink(beast::unit_test::suite& suite)
            : Sink(beast::severities::kWarning, false), suite_(suite)
        {
        }

        void
        write(beast::severities::Severity level, std::string const& text)
            override
        {
            if (level < threshold())
                return;

            suite_.log << text << std::endl;
        }
    };

    //--------------------------------------------------------------------------

    struct TestHandler
    {
        bool
        onAccept(Session& session, boost::asio::ip::tcp::endpoint endpoint)
        {
            return true;
        }

        Handoff
        onHandoff(
            Session& session,
            std::unique_ptr<stream_type>&& bundle,
            http_request_type&& request,
            boost::asio::ip::tcp::endpoint remote_address)
        {
            return Handoff{};
        }

        Handoff
        onHandoff(
            Session& session,
            http_request_type&& request,
            boost::asio::ip::tcp::endpoint remote_address)
        {
            return Handoff{};
        }

        void
        onRequest(Session& session)
        {
            session.write(std::string("Hello, world!\n"));
            if (beast::rfc2616::is_keep_alive(session.request()))
                session.complete();
            else
                session.close(true);
        }

        void
        onWSMessage(
            std::shared_ptr<WSSession> session,
            std::vector<boost::asio::const_buffer> const&)
        {
        }

        void
        onClose(Session& session, boost::system::error_code const&)
        {
        }

        void
        onStopped(Server& server)
        {
        }
    };

    //--------------------------------------------------------------------------

    // Connect to an address
    template <class Socket>
    bool
    connect(Socket& s, typename Socket::endpoint_type const& ep)
    {
        try
        {
            s.connect(ep);
            pass();
            return true;
        }
        catch (std::exception const& e)
        {
            fail(e.what());
        }

        return false;
    }

    // Write a string to the stream
    template <class SyncWriteStream>
    bool
    write(SyncWriteStream& s, std::string const& text)
    {
        try
        {
            boost::asio::write(s, boost::asio::buffer(text));
            pass();
            return true;
        }
        catch (std::exception const& e)
        {
            fail(e.what());
        }
        return false;
    }

    // Expect that reading the stream produces a matching string
    template <class SyncReadStream>
    bool
    expect_read(SyncReadStream& s, std::string const& match)
    {
        boost::asio::streambuf b(1000);  // limit on read
        try
        {
            auto const n = boost::asio::read_until(s, b, '\n');
            if (BEAST_EXPECT(n == match.size()))
            {
                std::string got;
                got.resize(n);
                boost::asio::buffer_copy(
                    boost::asio::buffer(&got[0], n), b.data());
                return BEAST_EXPECT(got == match);
            }
        }
        catch (std::length_error const& e)
        {
            fail(e.what());
        }
        catch (std::exception const& e)
        {
            fail(e.what());
        }
        return false;
    }

    void
    test_request(boost::asio::ip::tcp::endpoint const& ep)
    {
        boost::asio::io_service ios;
        using socket = boost::asio::ip::tcp::socket;
        socket s(ios);

        if (!connect(s, ep))
            return;

        if (!write(
                s,
                "GET / HTTP/1.1\r\n"
                "Connection: close\r\n"
                "\r\n"))
            return;

        if (!expect_read(s, "Hello, world!\n"))
            return;

        boost::system::error_code ec;
        s.shutdown(socket::shutdown_both, ec);

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    void
    test_keepalive(boost::asio::ip::tcp::endpoint const& ep)
    {
        boost::asio::io_service ios;
        using socket = boost::asio::ip::tcp::socket;
        socket s(ios);

        if (!connect(s, ep))
            return;

        if (!write(
                s,
                "GET / HTTP/1.1\r\n"
                "Connection: Keep-Alive\r\n"
                "\r\n"))
            return;

        if (!expect_read(s, "Hello, world!\n"))
            return;

        if (!write(
                s,
                "GET / HTTP/1.1\r\n"
                "Connection: close\r\n"
                "\r\n"))
            return;

        if (!expect_read(s, "Hello, world!\n"))
            return;

        boost::system::error_code ec;
        s.shutdown(socket::shutdown_both, ec);
    }

    void
    basicTests()
    {
        testcase("Basic client/server");
        TestSink sink{*this};
        TestThread thread;
        sink.threshold(beast::severities::Severity::kAll);
        beast::Journal journal{sink};
        TestHandler handler;
        auto s = make_Server(handler, thread.get_io_service(), journal);
        std::vector<Port> serverPort(1);
        serverPort.back().ip =
            beast::IP::Address::from_string(getEnvLocalhostAddr()),
        serverPort.back().port = 0;
        serverPort.back().protocol.insert("http");
        auto eps = s->ports(serverPort);
        log << "server listening on port " << eps[0].port() << std::endl;
        test_request(eps[0]);
        test_keepalive(eps[0]);
        // s->close();
        s = nullptr;
        pass();
    }

    void
    stressTest()
    {
        testcase("stress test");
        struct NullHandler
        {
            bool
            onAccept(Session& session, boost::asio::ip::tcp::endpoint endpoint)
            {
                return true;
            }

            Handoff
            onHandoff(
                Session& session,
                std::unique_ptr<stream_type>&& bundle,
                http_request_type&& request,
                boost::asio::ip::tcp::endpoint remote_address)
            {
                return Handoff{};
            }

            Handoff
            onHandoff(
                Session& session,
                http_request_type&& request,
                boost::asio::ip::tcp::endpoint remote_address)
            {
                return Handoff{};
            }

            void
            onRequest(Session& session)
            {
            }

            void
            onWSMessage(
                std::shared_ptr<WSSession> session,
                std::vector<boost::asio::const_buffer> const& buffers)
            {
            }

            void
            onClose(Session& session, boost::system::error_code const&)
            {
            }

            void
            onStopped(Server& server)
            {
            }
        };

        using namespace beast::severities;
        SuiteJournal journal("Server_test", *this);

        NullHandler h;
        for (int i = 0; i < 1000; ++i)
        {
            TestThread thread;
            auto s = make_Server(h, thread.get_io_service(), journal);
            std::vector<Port> serverPort(1);
            serverPort.back().ip =
                beast::IP::Address::from_string(getEnvLocalhostAddr()),
            serverPort.back().port = 0;
            serverPort.back().protocol.insert("http");
            s->ports(serverPort);
        }
        pass();
    }

    void
    testBadConfig()
    {
        testcase("Server config - invalid options");
        using namespace test::jtx;

        std::string messages;

        except([&] {
            Env env{
                *this,
                envconfig([](std::unique_ptr<Config> cfg) {
                    (*cfg).deprecatedClearSection("port_rpc");
                    return cfg;
                }),
                std::make_unique<CaptureLogs>(&messages)};
        });
        BEAST_EXPECT(
            messages.find("Missing 'ip' in [port_rpc]") != std::string::npos);

        except([&] {
            Env env{
                *this,
                envconfig([](std::unique_ptr<Config> cfg) {
                    (*cfg).deprecatedClearSection("port_rpc");
                    (*cfg)["port_rpc"].set("ip", getEnvLocalhostAddr());
                    return cfg;
                }),
                std::make_unique<CaptureLogs>(&messages)};
        });
        BEAST_EXPECT(
            messages.find("Missing 'port' in [port_rpc]") != std::string::npos);

        except([&] {
            Env env{
                *this,
                envconfig([](std::unique_ptr<Config> cfg) {
                    (*cfg).deprecatedClearSection("port_rpc");
                    (*cfg)["port_rpc"].set("ip", getEnvLocalhostAddr());
                    (*cfg)["port_rpc"].set("port", "0");
                    return cfg;
                }),
                std::make_unique<CaptureLogs>(&messages)};
        });
        BEAST_EXPECT(
            messages.find("Invalid value '0' for key 'port' in [port_rpc]") !=
            std::string::npos);

        except([&] {
            Env env{
                *this,
                envconfig([](std::unique_ptr<Config> cfg) {
                    (*cfg).deprecatedClearSection("port_rpc");
                    (*cfg)["port_rpc"].set("ip", getEnvLocalhostAddr());
                    (*cfg)["port_rpc"].set("port", "8081");
                    (*cfg)["port_rpc"].set("protocol", "");
                    return cfg;
                }),
                std::make_unique<CaptureLogs>(&messages)};
        });
        BEAST_EXPECT(
            messages.find("Missing 'protocol' in [port_rpc]") !=
            std::string::npos);

        except(
            [&]  // this creates a standard test config without the server
                 // section
            {
                Env env{
                    *this,
                    envconfig([](std::unique_ptr<Config> cfg) {
                        cfg = std::make_unique<Config>();
                        cfg->overwrite(
                            ConfigSection::nodeDatabase(), "type", "memory");
                        cfg->overwrite(
                            ConfigSection::nodeDatabase(), "path", "main");
                        cfg->deprecatedClearSection(
                            ConfigSection::importNodeDatabase());
                        cfg->legacy("database_path", "");
                        cfg->setupControl(true, true, true);
                        (*cfg)["port_peer"].set("ip", getEnvLocalhostAddr());
                        (*cfg)["port_peer"].set("port", "8080");
                        (*cfg)["port_peer"].set("protocol", "peer");
                        (*cfg)["port_rpc"].set("ip", getEnvLocalhostAddr());
                        (*cfg)["port_rpc"].set("port", "8081");
                        (*cfg)["port_rpc"].set("protocol", "http,ws2");
                        (*cfg)["port_rpc"].set("admin", getEnvLocalhostAddr());
                        (*cfg)["port_ws"].set("ip", getEnvLocalhostAddr());
                        (*cfg)["port_ws"].set("port", "8082");
                        (*cfg)["port_ws"].set("protocol", "ws");
                        (*cfg)["port_ws"].set("admin", getEnvLocalhostAddr());
                        return cfg;
                    }),
                    std::make_unique<CaptureLogs>(&messages)};
            });
        BEAST_EXPECT(
            messages.find("Required section [server] is missing") !=
            std::string::npos);

        except([&]  // this creates a standard test config without some of the
                    // port sections
               {
                   Env env{
                       *this,
                       envconfig([](std::unique_ptr<Config> cfg) {
                           cfg = std::make_unique<Config>();
                           cfg->overwrite(
                               ConfigSection::nodeDatabase(), "type", "memory");
                           cfg->overwrite(
                               ConfigSection::nodeDatabase(), "path", "main");
                           cfg->deprecatedClearSection(
                               ConfigSection::importNodeDatabase());
                           cfg->legacy("database_path", "");
                           cfg->setupControl(true, true, true);
                           (*cfg)["server"].append("port_peer");
                           (*cfg)["server"].append("port_rpc");
                           (*cfg)["server"].append("port_ws");
                           return cfg;
                       }),
                       std::make_unique<CaptureLogs>(&messages)};
               });
        BEAST_EXPECT(
            messages.find("Missing section: [port_peer]") != std::string::npos);
    }

    void
    run() override
    {
        basicTests();
        stressTest();
        testBadConfig();
    }
};

BEAST_DEFINE_TESTSUITE(Server, http, ripple);

}  // namespace test
}  // namespace ripple
