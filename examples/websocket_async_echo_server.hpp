//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef WEBSOCKET_ASYNC_ECHO_SERVER_HPP
#define WEBSOCKET_ASYNC_ECHO_SERVER_HPP

#include <beast/core/placeholders.hpp>
#include <beast/core/streambuf.hpp>
#include <beast/websocket/stream.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/optional.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <thread>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>

namespace websocket {

/** Asynchronous WebSocket echo client/server
*/
class async_echo_server
{
public:
    using error_code = beast::error_code;
    using address_type = boost::asio::ip::address;
    using socket_type = boost::asio::ip::tcp::socket;
    using endpoint_type = boost::asio::ip::tcp::endpoint;

private:
    struct identity
    {
        template<class Body, class Fields>
        void
        operator()(beast::http::message<
            true, Body, Fields>& req) const
        {
            req.fields.replace("User-Agent", "async_echo_client");
        }

        template<class Body, class Fields>
        void
        operator()(beast::http::message<
            false, Body, Fields>& resp) const
        {
            resp.fields.replace("Server", "async_echo_server");
        }
    };

    /** A container of type-erased option setters.
    */
    template<class NextLayer>
    class options_set
    {
        // workaround for std::function bug in msvc
        struct callable
        {
            virtual ~callable() = default;
            virtual void operator()(
                beast::websocket::stream<NextLayer>&) = 0;
        };

        template<class T>
        class callable_impl : public callable
        {
            T t_;

        public:
            template<class U>
            callable_impl(U&& u)
                : t_(std::forward<U>(u))
            {
            }

            void
            operator()(beast::websocket::stream<NextLayer>& ws)
            {
                t_(ws);
            }
        };

        template<class Opt>
        class lambda
        {
            Opt opt_;

        public:
            lambda(lambda&&) = default;
            lambda(lambda const&) = default;

            lambda(Opt const& opt)
                : opt_(opt)
            {
            }

            void
            operator()(beast::websocket::stream<NextLayer>& ws) const
            {
                ws.set_option(opt_);
            }
        };

        std::unordered_map<std::type_index,
            std::unique_ptr<callable>> list_;

    public:
        template<class Opt>
        void
        set_option(Opt const& opt)
        {
            std::unique_ptr<callable> p;
            p.reset(new callable_impl<lambda<Opt>>{opt});
            list_[std::type_index{
                typeid(Opt)}] = std::move(p);
        }

        void
        set_options(beast::websocket::stream<NextLayer>& ws)
        {
            for(auto const& op : list_)
                (*op.second)(ws);
        }
    };

    std::ostream* log_;
    boost::asio::io_service ios_;
    socket_type sock_;
    endpoint_type ep_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::vector<std::thread> thread_;
    boost::optional<boost::asio::io_service::work> work_;
    options_set<socket_type> opts_;

public:
    async_echo_server(async_echo_server const&) = delete;
    async_echo_server& operator=(async_echo_server const&) = delete;

    /** Constructor.

        @param log A pointer to a stream to log to, or `nullptr`
        to disable logging.

        @param threads The number of threads in the io_service.
    */
    async_echo_server(std::ostream* log,
            std::size_t threads)
        : log_(log)
        , sock_(ios_)
        , acceptor_(ios_)
        , work_(ios_)
    {
        opts_.set_option(
            beast::websocket::decorate(identity{}));
        thread_.reserve(threads);
        for(std::size_t i = 0; i < threads; ++i)
            thread_.emplace_back(
                [&]{ ios_.run(); });
    }

    /** Destructor.
    */
    ~async_echo_server()
    {
        work_ = boost::none;
        error_code ec;
        ios_.dispatch(
            [&]{ acceptor_.close(ec); });
        for(auto& t : thread_)
            t.join();
    }

    /** Return the listening endpoint.
    */
    endpoint_type
    local_endpoint() const
    {
        return acceptor_.local_endpoint();
    }

    /** Set a websocket option.

        The option will be applied to all new connections.

        @param opt The option to apply.
    */
    template<class Opt>
    void
    set_option(Opt const& opt)
    {
        opts_.set_option(opt);
    }

    /** Open a listening port.

        @param ep The address and port to bind to.

        @param ec Set to the error, if any occurred.
    */
    void
    open(endpoint_type const& ep, error_code& ec)
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
        acceptor_.async_accept(sock_, ep_,
            std::bind(&async_echo_server::on_accept, this,
                beast::asio::placeholders::error));
    }

private:
    class peer
    {
        struct data
        {
            async_echo_server& server;
            endpoint_type ep;
            int state = 0;
            beast::websocket::stream<socket_type> ws;
            boost::asio::io_service::strand strand;
            beast::websocket::opcode op;
            beast::streambuf db;
            std::size_t id;

            data(async_echo_server& server_,
                endpoint_type const& ep_,
                    socket_type&& sock_)
                : server(server_)
                , ep(ep_)
                , ws(std::move(sock_))
                , strand(ws.get_io_service())
                , id([]
                    {
                        static std::atomic<std::size_t> n{0};
                        return ++n;
                    }())
            {
            }
        };

        // VFALCO This could be unique_ptr in [Net.TS]
        std::shared_ptr<data> d_;

    public:
        peer(peer&&) = default;
        peer(peer const&) = default;
        peer& operator=(peer&&) = delete;
        peer& operator=(peer const&) = delete;

        template<class... Args>
        explicit
        peer(async_echo_server& server,
            endpoint_type const& ep, socket_type&& sock,
                Args&&... args)
            : d_(std::make_shared<data>(server, ep,
                std::forward<socket_type>(sock),
                    std::forward<Args>(args)...))
        {
            auto& d = *d_;
            d.server.opts_.set_options(d.ws);
            run();
        }

        void run()
        {
            auto& d = *d_;
            d.ws.async_accept(std::move(*this));
        }

        void operator()(error_code ec, std::size_t)
        {
            (*this)(ec);
        }

        void operator()(error_code ec)
        {
            using boost::asio::buffer;
            using boost::asio::buffer_copy;
            auto& d = *d_;
            switch(d.state)
            {
            // did accept
            case 0:
                if(ec)
                    return fail("async_accept", ec);

            // start
            case 1:
                if(ec)
                    return fail("async_handshake", ec);
                d.db.consume(d.db.size());
                // read message
                d.state = 2;
                d.ws.async_read(d.op, d.db,
                    d.strand.wrap(std::move(*this)));
                return;

            // got message
            case 2:
                if(ec == beast::websocket::error::closed)
                    return;
                if(ec)
                    return fail("async_read", ec);
                // write message
                d.state = 1;
                d.ws.set_option(
                    beast::websocket::message_type(d.op));
                d.ws.async_write(d.db.data(),
                    d.strand.wrap(std::move(*this)));
                return;
            }
        }

    private:
        void
        fail(std::string what, error_code ec)
        {
            auto& d = *d_;
            if(d.server.log_)
                if(ec != beast::websocket::error::closed)
                    d.server.fail("[#" + std::to_string(d.id) +
                        " " + boost::lexical_cast<std::string>(d.ep) +
                            "] " + what, ec);
        }
    };

    void
    fail(std::string what, error_code ec)
    {
        if(log_)
        {
            static std::mutex m;
            std::lock_guard<std::mutex> lock{m};
            (*log_) << what << ": " <<
                ec.message() << std::endl;
        }
    }

    void
    on_accept(error_code ec)
    {
        if(! acceptor_.is_open())
            return;
        if(ec == boost::asio::error::operation_aborted)
            return;
        if(ec)
            fail("accept", ec);
        peer{*this, ep_, std::move(sock_)};
        acceptor_.async_accept(sock_, ep_,
            std::bind(&async_echo_server::on_accept, this,
                beast::asio::placeholders::error));
    }
};

} // websocket

#endif
