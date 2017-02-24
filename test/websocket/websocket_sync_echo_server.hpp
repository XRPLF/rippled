//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_SYNC_ECHO_SERVER_HPP
#define BEAST_WEBSOCKET_SYNC_ECHO_SERVER_HPP

#include <beast/core/placeholders.hpp>
#include <beast/core/streambuf.hpp>
#include <beast/websocket.hpp>
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

/** Synchronous WebSocket echo client/server
*/
class sync_echo_server
{
public:
    using error_code = beast::error_code;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;
    using socket_type = boost::asio::ip::tcp::socket;

private:
    struct identity
    {
        template<class Body, class Fields>
        void
        operator()(beast::http::message<
            true, Body, Fields>& req) const
        {
            req.fields.replace("User-Agent", "sync_echo_client");
        }

        template<class Body, class Fields>
        void
        operator()(beast::http::message<
            false, Body, Fields>& resp) const
        {
            resp.fields.replace("Server", "sync_echo_server");
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
    std::thread thread_;
    options_set<socket_type> opts_;

public:
    /** Constructor.

        @param log A pointer to a stream to log to, or `nullptr`
        to disable logging.
    */
    sync_echo_server(std::ostream* log)
        : log_(log)
        , sock_(ios_)
        , acceptor_(ios_)
    {
        opts_.set_option(
            beast::websocket::decorate(identity{}));
    }

    /** Destructor.
    */
    ~sync_echo_server()
    {
        if(thread_.joinable())
        {
            error_code ec;
            ios_.dispatch(
                [&]{ acceptor_.close(ec); });
            thread_.join();
        }
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
            std::bind(&sync_echo_server::on_accept, this,
                beast::asio::placeholders::error));
        thread_ = std::thread{[&]{ ios_.run(); }};
    }

private:
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
    fail(std::string what, error_code ec,
        int id, endpoint_type const& ep)
    {
        if(log_)
            if(ec != beast::websocket::error::closed)
                fail("[#" + std::to_string(id) + " " +
                    boost::lexical_cast<std::string>(ep) +
                        "] " + what, ec);
    }

    void
    on_accept(error_code ec)
    {
        if(ec == boost::asio::error::operation_aborted)
            return;
        if(ec)
            return fail("accept", ec);
        struct lambda
        {
            std::size_t id;
            endpoint_type ep;
            sync_echo_server& self;
            boost::asio::io_service::work work;
            // Must be destroyed before work otherwise the
            // io_service could be destroyed before the socket.
            socket_type sock;

            lambda(sync_echo_server& self_,
                endpoint_type const& ep_,
                    socket_type&& sock_)
                : id([]
                    {
                        static std::atomic<std::size_t> n{0};
                        return ++n;
                    }())
                , ep(ep_)
                , self(self_)
                , work(sock_.get_io_service())
                , sock(std::move(sock_))
            {
            }

            void operator()()
            {
                self.do_peer(id, ep, std::move(sock));
            }
        };
        std::thread{lambda{*this, ep_, std::move(sock_)}}.detach();
        acceptor_.async_accept(sock_, ep_,
            std::bind(&sync_echo_server::on_accept, this,
                beast::asio::placeholders::error));
    }

    template<class DynamicBuffer, std::size_t N>
    static
    bool
    match(DynamicBuffer& db, char const(&s)[N])
    {
        using boost::asio::buffer;
        using boost::asio::buffer_copy;
        if(db.size() < N-1)
            return false;
        beast::static_string<N-1> t;
        t.resize(N-1);
        buffer_copy(buffer(t.data(), t.size()),
            db.data());
        if(t != s)
            return false;
        db.consume(N-1);
        return true;
    }

    void
    do_peer(std::size_t id,
        endpoint_type const& ep, socket_type&& sock)
    {
        using boost::asio::buffer;
        using boost::asio::buffer_copy;
        beast::websocket::stream<
            socket_type> ws{std::move(sock)};
        opts_.set_options(ws);
        error_code ec;
        ws.accept(ec);
        if(ec)
        {
            fail("accept", ec, id, ep);
            return;
        }
        for(;;)
        {
            beast::websocket::opcode op;
            beast::streambuf sb;
            ws.read(op, sb, ec);
            if(ec)
            {
                auto const s = ec.message();
                break;
            }
            ws.set_option(beast::websocket::message_type{op});
            if(match(sb, "RAW"))
            {
                boost::asio::write(
                    ws.next_layer(), sb.data(), ec);
            }
            else if(match(sb, "TEXT"))
            {
                ws.set_option(
                    beast::websocket::message_type{
                        beast::websocket::opcode::text});
                ws.write(sb.data(), ec);
            }
            else if(match(sb, "PING"))
            {
                beast::websocket::ping_data payload;
                sb.consume(buffer_copy(
                    buffer(payload.data(), payload.size()),
                        sb.data()));
                ws.ping(payload, ec);
            }
            else if(match(sb, "CLOSE"))
            {
                ws.close({}, ec);
            }
            else
            {
                ws.write(sb.data(), ec);
            }
            if(ec)
                break;
        }
        if(ec && ec != beast::websocket::error::closed)
        {
            fail("read", ec, id, ep);
        }
    }
};

} // websocket

#endif
