#include <test/jtx.h>

#include <xrpld/rpc/RPCSub.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/server/NetworkOPs.h>

#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace xrpl {
namespace test {

// Minimal TCP server for testing RPCSub webhook delivery.
// Accepts connections and sends configurable HTTP responses.
// Can hold connections open to simulate slow/stalled endpoints.
class MockWebhookServer
{
    boost::asio::io_context ioc_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::thread thread_;
    std::atomic<bool> running_{true};
    unsigned short port_;

    std::atomic<int> activeConnections_{0};
    std::atomic<int> peakConnections_{0};
    std::atomic<int> totalAccepted_{0};
    std::atomic<int> statusCode_{200};
    std::atomic<int> delayMs_{0};
    std::atomic<bool> holdOpen_{false};

    // Sockets held open when holdOpen_ is true, preventing
    // the client from getting a response or EOF.
    std::mutex heldMutex_;
    std::vector<std::shared_ptr<boost::asio::ip::tcp::socket>> heldSockets_;

public:
    MockWebhookServer()
        : work_(boost::asio::make_work_guard(ioc_))
        , acceptor_(
              ioc_,
              boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0))
    {
        port_ = acceptor_.local_endpoint().port();
        accept();
        thread_ = std::thread([this] { ioc_.run(); });
    }

    ~MockWebhookServer()
    {
        running_ = false;
        work_.reset();
        {
            std::lock_guard lk(heldMutex_);
            for (auto& s : heldSockets_)
            {
                boost::system::error_code ec;
                s->close(ec);
            }
            heldSockets_.clear();
        }
        boost::system::error_code ec;
        acceptor_.close(ec);
        ioc_.stop();
        if (thread_.joinable())
            thread_.join();
    }

    unsigned short
    port() const
    {
        return port_;
    }

    int
    totalAcceptedCount() const
    {
        return totalAccepted_;
    }

    int
    activeConnectionCount() const
    {
        return activeConnections_;
    }

    int
    peakConnectionCount() const
    {
        return peakConnections_;
    }

    void
    setStatus(int code)
    {
        statusCode_ = code;
    }

    void
    setDelay(int ms)
    {
        delayMs_ = ms;
    }

    // When true, accept connections and read headers but never
    // respond — keeps io_context.run() blocked on the client side.
    void
    setHoldOpen(bool hold)
    {
        holdOpen_ = hold;
    }

    // Release all held sockets, causing client-side EOF/error.
    void
    releaseHeld()
    {
        std::lock_guard lk(heldMutex_);
        for (auto& s : heldSockets_)
        {
            boost::system::error_code ec;
            s->close(ec);
        }
        heldSockets_.clear();
    }

private:
    void
    accept()
    {
        auto sock = std::make_shared<boost::asio::ip::tcp::socket>(ioc_);
        acceptor_.async_accept(*sock, [this, sock](auto ec) {
            if (!ec && running_)
            {
                ++totalAccepted_;
                int current = ++activeConnections_;
                int prev = peakConnections_.load();
                while (current > prev && !peakConnections_.compare_exchange_weak(prev, current))
                    ;
                handleConnection(sock);
                accept();
            }
        });
    }

    void
    handleConnection(std::shared_ptr<boost::asio::ip::tcp::socket> sock)
    {
        auto buf = std::make_shared<boost::asio::streambuf>();
        boost::asio::async_read_until(*sock, *buf, "\r\n\r\n", [this, sock, buf](auto ec, size_t) {
            if (ec)
            {
                --activeConnections_;
                return;
            }

            if (holdOpen_)
            {
                // Hold socket open — client blocks waiting
                // for a response that never comes.
                std::lock_guard lk(heldMutex_);
                heldSockets_.push_back(sock);
                return;
            }
            auto delay = delayMs_.load();
            if (delay > 0)
            {
                auto timer = std::make_shared<boost::asio::steady_timer>(ioc_);
                timer->expires_after(std::chrono::milliseconds(delay));
                timer->async_wait([this, sock, timer](auto) { sendHTTPResponse(sock); });
            }
            else
            {
                sendHTTPResponse(sock);
            }
        });
    }

    void
    sendHTTPResponse(std::shared_ptr<boost::asio::ip::tcp::socket> sock)
    {
        auto body = std::string("{}");
        auto response = std::make_shared<std::string>(
            "HTTP/1.0 " + std::to_string(statusCode_.load()) +
            " OK\r\n"
            "Content-Length: " +
            std::to_string(body.size()) + "\r\n\r\n" + body);

        boost::asio::async_write(
            *sock, boost::asio::buffer(*response), [this, sock, response](auto, size_t) {
                boost::system::error_code ec;
                sock->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
                sock->close(ec);
                --activeConnections_;
            });
    }
};

//------------------------------------------------------------------------------

class RPCSub_test : public beast::unit_test::suite
{
    void
    testWebhookDelivery()
    {
        testcase("Webhook delivery via ledger stream");

        using namespace jtx;
        Env env{*this};

        MockWebhookServer server;

        Json::Value jv;
        jv[jss::url] = "http://127.0.0.1:" + std::to_string(server.port()) + "/";
        jv[jss::streams] = Json::arrayValue;
        jv[jss::streams][0u] = "ledger";
        auto jr = env.rpc("json", "subscribe", to_string(jv))[jss::result];
        BEAST_EXPECT(jr[jss::status] == "success");

        // Close a ledger to trigger the event
        env.close();

        // Wait for delivery
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
        while (server.totalAcceptedCount() == 0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

        BEAST_EXPECT(server.totalAcceptedCount() >= 1);

        // Wait for connections to close
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        BEAST_EXPECT(server.activeConnectionCount() == 0);

        jr = env.rpc("json", "unsubscribe", to_string(jv))[jss::result];
        BEAST_EXPECT(jr[jss::status] == "success");
    }

    void
    testWebhookMultipleEvents()
    {
        testcase("Multiple events delivered via webhook");

        using namespace jtx;
        Env env{*this};

        MockWebhookServer server;

        Json::Value jv;
        jv[jss::url] = "http://127.0.0.1:" + std::to_string(server.port()) + "/";
        jv[jss::streams] = Json::arrayValue;
        jv[jss::streams][0u] = "ledger";
        auto jr = env.rpc("json", "subscribe", to_string(jv))[jss::result];
        BEAST_EXPECT(jr[jss::status] == "success");

        // Close multiple ledgers to trigger multiple events
        for (int i = 0; i < 5; ++i)
            env.close();

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
        while (server.totalAcceptedCount() < 5 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

        BEAST_EXPECT(server.totalAcceptedCount() >= 5);

        jr = env.rpc("json", "unsubscribe", to_string(jv))[jss::result];
        BEAST_EXPECT(jr[jss::status] == "success");
    }

    void
    testWebhookHTTP500()
    {
        testcase("Webhook delivery completes on HTTP 500");

        using namespace jtx;
        Env env{*this};

        MockWebhookServer server;
        server.setStatus(500);

        Json::Value jv;
        jv[jss::url] = "http://127.0.0.1:" + std::to_string(server.port()) + "/";
        jv[jss::streams] = Json::arrayValue;
        jv[jss::streams][0u] = "ledger";
        auto jr = env.rpc("json", "subscribe", to_string(jv))[jss::result];
        BEAST_EXPECT(jr[jss::status] == "success");

        env.close();

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
        while (server.totalAcceptedCount() == 0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

        BEAST_EXPECT(server.totalAcceptedCount() >= 1);

        jr = env.rpc("json", "unsubscribe", to_string(jv))[jss::result];
        BEAST_EXPECT(jr[jss::status] == "success");
    }

    void
    testQueueOverflow()
    {
        testcase("Queue overflow drops events");

        using namespace jtx;
        Env env{*this};

        // Server holds connections so sendThread blocks,
        // letting the queue fill up.
        MockWebhookServer server;
        server.setHoldOpen(true);

        std::string url = "http://127.0.0.1:" + std::to_string(server.port()) + "/";

        // Subscribe via RPC — NetworkOPs manages lifetime.
        Json::Value jv;
        jv[jss::url] = url;
        jv[jss::streams] = Json::arrayValue;
        jv[jss::streams][0u] = "ledger";
        auto jr = env.rpc("json", "subscribe", to_string(jv))[jss::result];
        BEAST_EXPECT(jr[jss::status] == "success");

        // Grab our own shared_ptr so RPCSub stays alive
        // even after unsubscribe.
        auto sub = env.app().getOPs().findRpcSub(url);
        BEAST_EXPECT(sub != nullptr);

        // First send starts sendThread via JobQueue.
        Json::Value event;
        event["type"] = "test";
        sub->send(event, false);

        // Wait for sendThread to block in io_context.run().
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Flood — sendThread is blocked, queue fills.
        // maxQueueSize is 16384.
        for (int i = 0; i < 16500; ++i)
            sub->send(event, false);

        // Overflow guard (lines 60-65) has now executed.
        // Release held sockets and stop holding new ones
        // so sendThread can drain via normal fast responses.
        auto preRelease = server.totalAcceptedCount();
        server.setHoldOpen(false);
        server.releaseHeld();

        // Wait for sendThread to make post-release progress.
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
        while (server.totalAcceptedCount() <= preRelease &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        BEAST_EXPECT(server.totalAcceptedCount() > preRelease);

        // Unsubscribe — NetworkOPs releases its ref.
        jr = env.rpc("json", "unsubscribe", to_string(jv))[jss::result];
        BEAST_EXPECT(jr[jss::status] == "success");

        // Our shared_ptr `sub` keeps RPCSub alive until
        // this scope exits, after sendThread has drained.
    }

    void
    testInFlightBound()
    {
        testcase("In-flight deliveries remain bounded under concurrent senders");

        using namespace jtx;
        Env env{*this};

        MockWebhookServer server;
        server.setDelay(20);

        std::string url = "http://127.0.0.1:" + std::to_string(server.port()) + "/";
        Json::Value jv;
        jv[jss::url] = url;
        jv[jss::streams] = Json::arrayValue;
        jv[jss::streams][0u] = "ledger";
        auto jr = env.rpc("json", "subscribe", to_string(jv))[jss::result];
        BEAST_EXPECT(jr[jss::status] == "success");

        auto sub = env.app().getOPs().findRpcSub(url);
        BEAST_EXPECT(sub != nullptr);
        if (!sub)
            return;

        static constexpr int producers = 4;
        static constexpr int sendsPerProducer = 400;
        std::vector<std::thread> threads;
        threads.reserve(producers);
        for (int p = 0; p < producers; ++p)
        {
            threads.emplace_back([sub] {
                for (int i = 0; i < sendsPerProducer; ++i)
                {
                    Json::Value event;
                    event["type"] = "burst";
                    sub->send(event, false);
                }
            });
        }
        for (auto& t : threads)
            t.join();

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
        while (server.activeConnectionCount() != 0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));

        BEAST_EXPECT(server.peakConnectionCount() <= 32);

        jr = env.rpc("json", "unsubscribe", to_string(jv))[jss::result];
        BEAST_EXPECT(jr[jss::status] == "success");
    }

    void
    testSendThreadRestartsAfterIdle()
    {
        testcase("Send thread restarts after queue drains to idle");

        using namespace jtx;
        Env env{*this};

        MockWebhookServer server;

        std::string url = "http://127.0.0.1:" + std::to_string(server.port()) + "/";
        Json::Value jv;
        jv[jss::url] = url;
        jv[jss::streams] = Json::arrayValue;
        jv[jss::streams][0u] = "ledger";
        auto jr = env.rpc("json", "subscribe", to_string(jv))[jss::result];
        BEAST_EXPECT(jr[jss::status] == "success");

        auto sub = env.app().getOPs().findRpcSub(url);
        BEAST_EXPECT(sub != nullptr);
        if (!sub)
            return;

        Json::Value event;
        event["type"] = "restart";

        // First send creates and runs sendThread.
        sub->send(event, false);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
        while (server.totalAcceptedCount() < 1 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        BEAST_EXPECT(server.totalAcceptedCount() >= 1);

        // Allow sendThread to observe empty queue and exit.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        auto acceptedBeforeRestart = server.totalAcceptedCount();

        // Second send should start a fresh sender and deliver again.
        sub->send(event, false);
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
        while (server.totalAcceptedCount() <= acceptedBeforeRestart &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        BEAST_EXPECT(server.totalAcceptedCount() > acceptedBeforeRestart);

        jr = env.rpc("json", "unsubscribe", to_string(jv))[jss::result];
        BEAST_EXPECT(jr[jss::status] == "success");
    }

public:
    void
    run() override
    {
        testWebhookDelivery();
        testWebhookMultipleEvents();
        testWebhookHTTP500();
        testQueueOverflow();
        testInFlightBound();
        testSendThreadRestartsAfterIdle();
    }
};

BEAST_DEFINE_TESTSUITE(RPCSub, rpc, xrpl);

}  // namespace test
}  // namespace xrpl
