//
// Copyright (c) 2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "../common/helpers.hpp"

#include <beast/core.hpp>
#include <beast/websocket.hpp>
#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/optional.hpp>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace http = beast::http;           // from <beast/http.hpp>
namespace websocket = beast::websocket; // from <beast/websocket.hpp>
namespace ip = boost::asio::ip;         // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio.hpp>

//------------------------------------------------------------------------------
//
// Example: WebSocket echo server, asynchronous
//
//------------------------------------------------------------------------------

/** WebSocket asynchronous echo server

    The server holds the listening socket, the io_service, and
    the threads calling io_service::run
*/
class server
{
    using error_code = beast::error_code;       // Saves typing
    using clock_type =
        std::chrono::steady_clock;              // For the timer
    using stream_type =
        websocket::stream<tcp::socket>;         // The type of our websocket stream
    std::ostream* log_;                         // Used for diagnostic output, may be null
    boost::asio::io_service ios_;               // The io_service, required
    tcp::socket sock_;                          // Holds accepted connections
    tcp::endpoint ep_;                          // The remote endpoint during accept
    std::vector<std::thread> thread_;           // Threads for the io_service
    boost::asio::ip::tcp::acceptor acceptor_;   // The listening socket
    std::function<void(stream_type&)> mod_;     // Called on new stream
    boost::optional<
        boost::asio::io_service::work> work_;   // Keeps io_service::run from returning

    //--------------------------------------------------------------------------

    class connection : public std::enable_shared_from_this<connection>
    {
        std::ostream* log_;                     // Where to log, may be null
        tcp::endpoint ep_;                      // The remote endpoing
        stream_type ws_;                        // The websocket stream
        boost::asio::basic_waitable_timer<
            clock_type> timer_;                 // Needed for timeouts
        boost::asio::io_service::strand strand_;// Needed when threads > 1
        beast::multi_buffer buffer_;            // Stores the current message
        beast::drain_buffer drain_;             // Helps discard data on close
        std::size_t id_;                        // A small unique id

    public:
        /// Constructor
        connection(
            server& parent,
            tcp::endpoint const& ep,
            tcp::socket&& sock)
            : log_(parent.log_)
            , ep_(ep)
            , ws_(std::move(sock))
            , timer_(ws_.get_io_service(), (clock_type::time_point::max)())
            , strand_(ws_.get_io_service())
            , id_([]
                {
                    static std::atomic<std::size_t> n{0};
                    return ++n;
                }())
        {
            // Invoke the callback for new connections if set.
            // This allows the settings on the websocket stream
            // to be adjusted. For example to turn compression
            // on or off or adjust the read and write buffer sizes.
            //
            if(parent.mod_)
                parent.mod_(ws_);
        }

        // Called immediately after the connection is created.
        // We keep this separate from the constructor because
        // shared_from_this may not be called from constructors.
        void run()
        {
            // Run the timer
            on_timer({});

            // Put the handshake on the timer
            timer_.expires_from_now(std::chrono::seconds(15));

            // Read the websocket handshake and send the response
            ws_.async_accept_ex(
                [](websocket::response_type& res)
                {
                    res.insert(http::field::server, "websocket-server-async");
                },
                strand_.wrap(std::bind(
                    &connection::on_accept,
                    shared_from_this(),
                    std::placeholders::_1)));
        }

    private:
        // Called when the timer expires.
        // We operate the timer continuously this simplifies the code.
        //
        void on_timer(error_code ec)
        {
            if(ec && ec != boost::asio::error::operation_aborted)
                return fail("timer", ec);

            // Verify that the timer really expired
            // since the deadline may have moved.
            //
            if(timer_.expires_at() <= clock_type::now())
            {
                // Closing the socket cancels all outstanding
                // operations. They will complete with
                // boost::asio::error::operation_aborted
                //
                ws_.next_layer().close(ec);
                return;
            }

            // Wait on the timer
            timer_.async_wait(
                strand_.wrap(std::bind(
                    &connection::on_timer,
                    shared_from_this(),
                    std::placeholders::_1)));
        }

        // Called after the handshake is performed
        void on_accept(error_code ec)
        {
            if(ec)
                return fail("accept", ec);
            do_read();
        }

        // Read a message from the websocket stream
        void do_read()
        {
            // Put the read on the timer
            timer_.expires_from_now(std::chrono::seconds(15));

            // Read a message
            ws_.async_read(buffer_,
                strand_.wrap(std::bind(
                    &connection::on_read,
                    shared_from_this(),
                    std::placeholders::_1)));
        }

        // Called after the message read completes
        void on_read(error_code ec)
        {
            // This error means the other side
            // closed the websocket stream.
            if(ec == websocket::error::closed)
                return;

            if(ec)
                return fail("read", ec);

            // Put the write on the timer
            timer_.expires_from_now(std::chrono::seconds(15));

            // Write the received message back
            ws_.binary(ws_.got_binary());
            ws_.async_write(buffer_.data(),
                strand_.wrap(std::bind(
                    &connection::on_write,
                    shared_from_this(),
                    std::placeholders::_1)));
        }

        // Called after the message write completes
        void on_write(error_code ec)
        {
            if(ec)
                return fail("write", ec);

            // Empty out the buffer. This is
            // needed if we want to do another read.
            //
            buffer_.consume(buffer_.size());

            // This shows how the server can close the
            // connection. Alternatively we could call
            // do_read again and the connection would
            // stay open until the other side closes it.
            //
            do_close();
        }

        // Sends a websocket close frame
        void do_close()
        {
            // Put the close frame on the timer
            timer_.expires_from_now(std::chrono::seconds(15));

            // Send the close frame
            ws_.async_close({},
                strand_.wrap(std::bind(
                    &connection::on_close,
                    shared_from_this(),
                    std::placeholders::_1)));
        }

        // Called when writing the close frame completes
        void on_close(error_code ec)
        {
            if(ec)
                return fail("close", ec);

            on_drain({});
        }

        // Read and discard any leftover message data
        void on_drain(error_code ec)
        {
            if(ec == websocket::error::closed)
            {
                // the connection has been closed gracefully
                return;
            }

            if(ec)
                return fail("drain", ec);

            // WebSocket says that to close a connection you have
            // to keep reading messages until you receive a close frame.
            // Beast delivers the close frame as an error from read.
            //
            ws_.async_read(drain_,
                strand_.wrap(std::bind(
                    &connection::on_drain,
                    shared_from_this(),
                    std::placeholders::_1)));
        }

        // Pretty-print an error to the log
        void fail(std::string what, error_code ec)
        {
            if(log_)
                if(ec != boost::asio::error::operation_aborted)
                    print(*log_, "[#", id_, " ", ep_, "] ", what, ": ", ec.message());
        }
    };

    //--------------------------------------------------------------------------

    // Pretty-print an error to the log
    void fail(std::string what, error_code ec)
    {
        if(log_)
            print(*log_, what, ": ", ec.message());
    }

    // Initiates an accept
    void do_accept()
    {
        acceptor_.async_accept(sock_, ep_,
            std::bind(&server::on_accept, this,
                std::placeholders::_1));
    }

    // Called when receiving an incoming connection
    void on_accept(error_code ec)
    {
        // This can happen during exit
        if(! acceptor_.is_open())
            return;

        // This can happen during exit
        if(ec == boost::asio::error::operation_aborted)
            return;

        if(ec)
            fail("accept", ec);

        // Create the connection and run it
        std::make_shared<connection>(*this, ep_, std::move(sock_))->run();

        // Initiate another accept
        do_accept();
    }

public:
    /** Constructor.

        @param log A pointer to a stream to log to, or `nullptr`
        to disable logging.
        
        @param threads The number of threads in the io_service.
    */
    server(std::ostream* log, std::size_t threads)
        : log_(log)
        , sock_(ios_)
        , acceptor_(ios_)
        , work_(ios_)
    {
        thread_.reserve(threads);
        for(std::size_t i = 0; i < threads; ++i)
            thread_.emplace_back(
                [&]{ ios_.run(); });
    }

    /// Destructor.
    ~server()
    {
        work_ = boost::none;
        ios_.dispatch([&]
            {
                error_code ec;
                acceptor_.close(ec);
            });
        for(auto& t : thread_)
            t.join();
    }

    /// Return the listening endpoint.
    tcp::endpoint
    local_endpoint() const
    {
        return acceptor_.local_endpoint();
    }

    /** Set a handler called for new streams.

        This function is called for each new stream.
        It is used to set options for every connection.
    */
    template<class F>
    void
    on_new_stream(F const& f)
    {
        mod_ = f;
    }

    /** Open a listening port.

        @param ep The address and port to bind to.

        @param ec Set to the error, if any occurred.
    */
    void
    open(tcp::endpoint const& ep, error_code& ec)
    {
        acceptor_.open(ep.protocol(), ec);
        if(ec)
            return fail("open", ec);
        acceptor_.set_option(
            boost::asio::socket_base::reuse_address{true});
        acceptor_.bind(ep, ec);
        if(ec)
            return fail("bind", ec);
        acceptor_.listen(
            boost::asio::socket_base::max_connections, ec);
        if(ec)
            return fail("listen", ec);
        do_accept();
    }
};

//------------------------------------------------------------------------------

// This helper will apply some settings to a WebSocket
// stream. The server applies it to all new connections.
//
class set_stream_options
{
    websocket::permessage_deflate pmd_;

public:
    set_stream_options(set_stream_options const&) = default;

    explicit
    set_stream_options(
            websocket::permessage_deflate const& pmd)
        : pmd_(pmd)
    {
    }

    template<class NextLayer>
    void
    operator()(websocket::stream<NextLayer>& ws) const
    {
        ws.set_option(pmd_);

        // Turn off the auto-fragment option.
        // This improves Autobahn performance.
        //
        ws.auto_fragment(false);

        // 64MB message size limit.
        // The high limit is needed for Autobahn.
        ws.read_message_max(64 * 1024 * 1024);
    }
};

int main(int argc, char* argv[])
{
    // Check command line arguments.
    if(argc != 4)
    {
        std::cerr <<
            "Usage: " << argv[0] << " <address> <port> <threads>\n"
            "  For IPv4, try: " << argv[0] << " 0.0.0.0 8080 1\n"
            "  For IPv6, try: " << argv[0] << " 0::0 8080 1\n"
            ;
        return EXIT_FAILURE;
    }

    // Decode command line options
    auto address = ip::address::from_string(argv[1]);
    unsigned short port = static_cast<unsigned short>(std::atoi(argv[2]));
    unsigned short threads = static_cast<unsigned short>(std::atoi(argv[3]));

    // Allow permessage-deflate
    // compression on all connections
    websocket::permessage_deflate pmd;
    pmd.client_enable = true;
    pmd.server_enable = true;
    pmd.compLevel = 3;

    // Create our server
    server s{&std::cout, threads};
    s.on_new_stream(set_stream_options{pmd});

    // Open the listening port
    beast::error_code ec;
    s.open(tcp::endpoint{address, port}, ec);
    if(ec)
    {
        std::cerr << "Error: " << ec.message();
        return EXIT_FAILURE;
    }

    // Wait for CTRL+C. After receiving CTRL+C,
    // the server should shut down cleanly.
    //
    sig_wait();

    return EXIT_SUCCESS;
}
