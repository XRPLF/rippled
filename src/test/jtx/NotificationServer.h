#pragma once

#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_value.h>

#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace xrpl::test {

/**
 * A minimal HTTP listener a test can subscribe a webhook `url` to, so it can
 * observe what `RPCSub` actually posts.
 *
 * Accepts asynchronously on an `io_context` of its own, so it needs no
 * cooperation from whichever thread services the server under test. Each
 * connection is read with a deadline: a peer that connects and sends nothing
 * must not hold the listener open, since the destructor waits for the thread
 * to finish.
 */
class NotificationServer
{
public:
    NotificationServer()
        : acceptor_(
              ioc_,
              boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0))
        , port_(acceptor_.local_endpoint().port())
    {
        accept();
        thread_ = std::thread([this] { ioc_.run(); });
    }

    /**
     * Closes the acceptor on the thread that runs the io_context, since one
     * acceptor touched from two threads while an accept is outstanding is not a
     * use asio documents, then joins that thread. run() returns once the
     * aborted accept and any connection still inside its deadline are done.
     */
    ~NotificationServer()
    {
        boost::asio::post(ioc_, [this] {
            boost::system::error_code ec;
            acceptor_.close(ec);
        });
        if (thread_.joinable())
            thread_.join();
    }

    NotificationServer(NotificationServer const&) = delete;
    NotificationServer&
    operator=(NotificationServer const&) = delete;

    /**
     * @return The port this listener bound to, for building a webhook url.
     *
     * Read in the constructor rather than here: `local_endpoint()` on an
     * acceptor with an accept outstanding is a use asio does not document, and
     * the throwing overload would throw once the acceptor closes. The port
     * cannot change, so reading it once is enough.
     */
    [[nodiscard]] std::uint16_t
    port() const
    {
        return port_;
    }

    /**
     * Waits for the next POST body this listener has received, parsed as
     * JSON.
     *
     * A body that does not parse, or that is not a JSON object, is not queued,
     * so a peer that posts one is reported here as no body at all rather than
     * as something the caller has to check the shape of.
     *
     * @param timeout How long to wait before giving up.
     * @return The parsed body, or `std::nullopt` if none arrived in time.
     */
    std::optional<json::Value>
    waitForNotification(std::chrono::milliseconds timeout = std::chrono::seconds(10))
    {
        std::unique_lock lock(mutex_);
        if (!cv_.wait_for(lock, timeout, [this] { return !queue_.empty(); }))
            return std::nullopt;
        auto value = std::move(queue_.front());
        queue_.pop_front();
        return value;
    }

private:
    using Tcp = boost::asio::ip::tcp;
    using ErrorCode = boost::system::error_code;

    /**
     * One accepted connection: its stream, buffer, request and response. One
     * request is read and answered per connection, which is what RPCSub sends.
     */
    struct Connection
    {
        explicit Connection(Tcp::socket socket) : stream(std::move(socket))
        {
        }

        boost::beast::tcp_stream stream;
        boost::beast::flat_buffer buffer;
        boost::beast::http::request<boost::beast::http::string_body> request;
        boost::beast::http::response<boost::beast::http::string_body> response;
    };

    /**
     * Arms one asynchronous accept, whose completion reads the connection and
     * arms the next.
     */
    void
    accept()
    {
        acceptor_.async_accept([this](ErrorCode ec, Tcp::socket socket) {
            // The close the destructor posts ends the chain, either by aborting the outstanding
            // accept or by leaving the acceptor closed when a completion was already queued. Any
            // other error (ECONNABORTED for a peer gone between SYN and accept, EMFILE for a
            // process out of descriptors) is one connection's, and the listener keeps accepting.
            if (ec == boost::asio::error::operation_aborted || !acceptor_.is_open())
                return;

            if (!ec)
                read(std::make_shared<Connection>(std::move(socket)));

            accept();
        });
    }

    /**
     * Reads one request from @p connection under the deadline, queues its body
     * and answers it.
     *
     * @param connection The accepted connection, kept alive by the handler.
     */
    void
    read(std::shared_ptr<Connection> connection)
    {
        auto& c = *connection;
        c.stream.expires_after(kTimeout);
        boost::beast::http::async_read(
            c.stream,
            c.buffer,
            c.request,
            [this, connection = std::move(connection)](ErrorCode ec, std::size_t) {
                if (ec)
                    return;

                queue(connection->request.body());
                write(connection);
            });
    }

    /**
     * Parses @p body and queues it for waitForNotification.
     *
     * A body that does not parse, or that is not a JSON object, is not queued.
     * `null` is what the guard is for: the reader accepts it at the root, and
     * it would read as a notification that arrived carrying nothing. A root
     * scalar cannot reach here, the reader accepting only null, an array or an
     * object there.
     *
     * @param body The POST body as received.
     */
    void
    queue(std::string const& body)
    {
        json::Value parsed;
        if (!json::Reader{}.parse(body, parsed) || !parsed.isObject())
            return;

        {
            std::scoped_lock const lock(mutex_);
            queue_.push_back(std::move(parsed));
        }
        cv_.notify_all();
    }

    /**
     * Answers @p connection with an empty 200 under the deadline; the
     * connection closes with the stream the handler holds the last reference
     * to.
     *
     * @param connection The connection whose request was read.
     */
    static void
    write(std::shared_ptr<Connection> connection)
    {
        auto& c = *connection;
        c.response.result(boost::beast::http::status::ok);
        c.response.version(c.request.version());
        c.response.prepare_payload();
        c.stream.expires_after(kTimeout);
        boost::beast::http::async_write(
            c.stream, c.response, [connection = std::move(connection)](ErrorCode, std::size_t) {
                // The connection closes with the stream this holds the last reference to.
            });
    }

    static constexpr std::chrono::seconds kTimeout{10};

    boost::asio::io_context ioc_;
    Tcp::acceptor acceptor_;
    std::uint16_t const port_;
    std::thread thread_;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<json::Value> queue_;
};

}  // namespace xrpl::test
