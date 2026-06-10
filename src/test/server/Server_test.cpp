#include <test/jtx/CaptureLogs.h>
#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>
#include <test/unit_test/SuiteJournal.h>

#include <xrpld/core/Config.h>

#include <xrpl/beast/rfc2616.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/Constants.h>
#include <xrpl/server/Handoff.h>
#include <xrpl/server/Port.h>
#include <xrpl/server/Server.h>
#include <xrpl/server/Session.h>
#include <xrpl/server/WSSession.h>
#include <xrpl/server/detail/ServerImpl.h>

#include <boost/asio/buffer.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/system/detail/error_code.hpp>

#include <chrono>
#include <exception>
#include <memory>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace xrpl::test {

using socket_type = boost::beast::tcp_stream;
using stream_type = boost::beast::ssl_stream<socket_type>;

class Server_test : public beast::unit_test::Suite
{
public:
    class TestThread
    {
    private:
        boost::asio::io_context ioContext_;
        std::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>
            work_;
        std::thread thread_;

    public:
        TestThread()
            : work_(std::in_place, boost::asio::make_work_guard(ioContext_))
            , thread_([&]() { this->ioContext_.run(); })
        {
        }

        ~TestThread()
        {
            work_.reset();
            thread_.join();
        }

        boost::asio::io_context&
        getIoContext()
        {
            return ioContext_;
        }
    };

    //--------------------------------------------------------------------------

    class TestSink : public beast::Journal::Sink
    {
        beast::unit_test::Suite& suite_;

    public:
        explicit TestSink(beast::unit_test::Suite& suite)
            : Sink(beast::Severity::Warning, false), suite_(suite)
        {
        }

        void
        write(beast::Severity level, std::string const& text) override
        {
            if (level < threshold())
                return;

            suite_.log << text << std::endl;
        }

        void
        writeAlways(beast::Severity level, std::string const& text) override
        {
            suite_.log << text << std::endl;
        }
    };

    //--------------------------------------------------------------------------

    struct TestHandler
    {
        static bool
        onAccept(Session& session, boost::asio::ip::tcp::endpoint endpoint)
        {
            return true;
        }

        static Handoff
        onHandoff(
            Session& session,
            std::unique_ptr<stream_type> const& bundle,
            http_request_type const& request,
            boost::asio::ip::tcp::endpoint remoteAddress)
        {
            return Handoff{};
        }

        static Handoff
        onHandoff(
            Session& session,
            http_request_type const& request,
            boost::asio::ip::tcp::endpoint remoteAddress)
        {
            return Handoff{};
        }

        static void
        onRequest(Session& session)
        {
            using namespace std::string_view_literals;
            session.write("Hello, world!\n"sv);
            if (beast::rfc2616::isKeepAlive(session.request()))
            {
                session.complete();
            }
            else
            {
                session.close(true);
            }
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
    expectRead(SyncReadStream& s, std::string const& match)
    {
        boost::asio::streambuf b(1000);  // limit on read
        try
        {
            auto const n = boost::asio::read_until(s, b, '\n');
            if (BEAST_EXPECT(n == match.size()))
            {
                std::string got;
                got.resize(n);
                boost::asio::buffer_copy(boost::asio::buffer(&got[0], n), b.data());
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
    testRequest(boost::asio::ip::tcp::endpoint const& ep)
    {
        boost::asio::io_context ios;
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

        if (!expectRead(s, "Hello, world!\n"))
            return;

        boost::system::error_code ec;
        s.shutdown(socket::shutdown_both, ec);  // NOLINT(bugprone-unused-return-value)

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    void
    testKeepalive(boost::asio::ip::tcp::endpoint const& ep)
    {
        boost::asio::io_context ios;
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

        if (!expectRead(s, "Hello, world!\n"))
            return;

        if (!write(
                s,
                "GET / HTTP/1.1\r\n"
                "Connection: close\r\n"
                "\r\n"))
            return;

        if (!expectRead(s, "Hello, world!\n"))
            return;

        boost::system::error_code ec;
        s.shutdown(socket::shutdown_both, ec);  // NOLINT(bugprone-unused-return-value)
    }

    void
    basicTests()
    {
        testcase("Basic client/server");
        TestSink sink{*this};
        TestThread thread;
        sink.threshold(beast::Severity::All);
        beast::Journal const journal{sink};
        TestHandler handler;
        auto s = makeServer(handler, thread.getIoContext(), journal);
        std::vector<Port> serverPort(1);
        serverPort.back().ip = boost::asio::ip::make_address(getEnvLocalhostAddr()),
        serverPort.back().port = 0;
        serverPort.back().protocol.insert("http");
        auto eps = s->ports(serverPort);
        testRequest(eps.begin()->second);
        testKeepalive(eps.begin()->second);
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
            static bool
            onAccept(Session& session, boost::asio::ip::tcp::endpoint endpoint)
            {
                return true;
            }

            static Handoff
            onHandoff(
                Session& session,
                std::unique_ptr<stream_type> const& bundle,
                http_request_type const& request,
                boost::asio::ip::tcp::endpoint remoteAddress)
            {
                return Handoff{};
            }

            static Handoff
            onHandoff(
                Session& session,
                http_request_type const& request,
                boost::asio::ip::tcp::endpoint remoteAddress)
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

        using beast::Severity;
        SuiteJournal journal("Server_test", *this);

        NullHandler h;
        for (int i = 0; i < 1000; ++i)
        {
            TestThread thread;
            auto s = makeServer(h, thread.getIoContext(), journal);
            std::vector<Port> serverPort(1);
            serverPort.back().ip = boost::asio::ip::make_address(getEnvLocalhostAddr()),
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
            Env const env{
                *this,
                envconfig([](std::unique_ptr<Config> cfg) {
                    (*cfg).deprecatedClearSection(Sections::kPortRpc);
                    return cfg;
                }),
                std::make_unique<CaptureLogs>(&messages)};
        });
        BEAST_EXPECT(messages.contains("Missing 'ip' in [port_rpc]"));

        except([&] {
            Env const env{
                *this,
                envconfig([](std::unique_ptr<Config> cfg) {
                    (*cfg).deprecatedClearSection(Sections::kPortRpc);
                    (*cfg)[Sections::kPortRpc].set(Keys::kIp, getEnvLocalhostAddr());
                    return cfg;
                }),
                std::make_unique<CaptureLogs>(&messages)};
        });
        BEAST_EXPECT(messages.contains("Missing 'port' in [port_rpc]"));

        except([&] {
            Env const env{
                *this,
                envconfig([](std::unique_ptr<Config> cfg) {
                    (*cfg).deprecatedClearSection(Sections::kPortRpc);
                    (*cfg)[Sections::kPortRpc].set(Keys::kIp, getEnvLocalhostAddr());
                    (*cfg)[Sections::kPortRpc].set(Keys::kPort, "0");
                    return cfg;
                }),
                std::make_unique<CaptureLogs>(&messages)};
        });
        BEAST_EXPECT(!messages.contains("Invalid value '0' for key 'port' in [port_rpc]"));

        except([&] {
            Env const env{
                *this,
                envconfig([](std::unique_ptr<Config> cfg) {
                    (*cfg)[Sections::kServer].set(Keys::kPort, "0");
                    return cfg;
                }),
                std::make_unique<CaptureLogs>(&messages)};
        });
        BEAST_EXPECT(messages.contains("Invalid value '0' for key 'port' in [server]"));

        except([&] {
            Env const env{
                *this,
                envconfig([](std::unique_ptr<Config> cfg) {
                    (*cfg).deprecatedClearSection(Sections::kPortRpc);
                    (*cfg)[Sections::kPortRpc].set(Keys::kIp, getEnvLocalhostAddr());
                    (*cfg)[Sections::kPortRpc].set(Keys::kPort, "8081");
                    (*cfg)[Sections::kPortRpc].set(Keys::kProtocol, "");
                    return cfg;
                }),
                std::make_unique<CaptureLogs>(&messages)};
        });
        BEAST_EXPECT(messages.contains("Missing 'protocol' in [port_rpc]"));

        except([&]  // this creates a standard test config without the server
                    // section
               {
                   Env const env{
                       *this,
                       envconfig([](std::unique_ptr<Config> cfg) {
                           cfg = std::make_unique<Config>();
                           cfg->overwrite(Sections::kNodeDatabase, Keys::kType, "memory");
                           cfg->overwrite(Sections::kNodeDatabase, Keys::kPath, "main");
                           cfg->deprecatedClearSection(Sections::kImportNodeDatabase);
                           cfg->legacy(Sections::kDatabasePath, "");
                           cfg->setupControl(true, true, true);
                           (*cfg)[Sections::kPortPeer].set(Keys::kIp, getEnvLocalhostAddr());
                           (*cfg)[Sections::kPortPeer].set(Keys::kPort, "8080");
                           (*cfg)[Sections::kPortPeer].set(Keys::kProtocol, "peer");
                           (*cfg)[Sections::kPortRpc].set(Keys::kIp, getEnvLocalhostAddr());
                           (*cfg)[Sections::kPortRpc].set(Keys::kPort, "8081");
                           (*cfg)[Sections::kPortRpc].set(Keys::kProtocol, "http,ws2");
                           (*cfg)[Sections::kPortRpc].set(Keys::kAdmin, getEnvLocalhostAddr());
                           (*cfg)[Sections::kPortWs].set(Keys::kIp, getEnvLocalhostAddr());
                           (*cfg)[Sections::kPortWs].set(Keys::kPort, "8082");
                           (*cfg)[Sections::kPortWs].set(Keys::kProtocol, "ws");
                           (*cfg)[Sections::kPortWs].set(Keys::kAdmin, getEnvLocalhostAddr());
                           return cfg;
                       }),
                       std::make_unique<CaptureLogs>(&messages)};
               });
        BEAST_EXPECT(messages.contains("Required section [server] is missing"));

        except([&]  // this creates a standard test config without some of the
                    // port sections
               {
                   Env const env{
                       *this,
                       envconfig([](std::unique_ptr<Config> cfg) {
                           cfg = std::make_unique<Config>();
                           cfg->overwrite(Sections::kNodeDatabase, Keys::kType, "memory");
                           cfg->overwrite(Sections::kNodeDatabase, Keys::kPath, "main");
                           cfg->deprecatedClearSection(Sections::kImportNodeDatabase);
                           cfg->legacy(Sections::kDatabasePath, "");
                           cfg->setupControl(true, true, true);
                           (*cfg)[Sections::kServer].append(Sections::kPortPeer);
                           (*cfg)[Sections::kServer].append(Sections::kPortRpc);
                           (*cfg)[Sections::kServer].append(Sections::kPortWs);
                           return cfg;
                       }),
                       std::make_unique<CaptureLogs>(&messages)};
               });
        BEAST_EXPECT(messages.contains("Missing section: [port_peer]"));
    }

    void
    run() override
    {
        basicTests();
        stressTest();
        testBadConfig();
    }
};

BEAST_DEFINE_TESTSUITE(Server, server, xrpl);

}  // namespace xrpl::test
