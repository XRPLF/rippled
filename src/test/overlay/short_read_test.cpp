//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright 2014 Ripple Labs Inc.

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

#include <BeastConfig.h>
#include <ripple/basics/make_SSLContext.h>
#include <ripple/beast/core/CurrentThreadName.h>
#include <ripple/beast/unit_test.h>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/optional.hpp>
#include <boost/utility/in_place_factory.hpp>
#include <cassert>
#include <condition_variable>
#include <functional>
#include <memory>
#include <thread>
#include <utility>

namespace ripple {
/*

Findings from the test:

If the remote host calls async_shutdown then the local host's
async_read will complete with eof.

If both hosts call async_shutdown then the calls to async_shutdown
will complete with eof.

*/

class short_read_test : public beast::unit_test::suite
{
private:
    using io_service_type = boost::asio::io_service;
    using strand_type = io_service_type::strand;
    using timer_type = boost::asio::basic_waitable_timer<
        std::chrono::steady_clock>;
    using acceptor_type = boost::asio::ip::tcp::acceptor;
    using socket_type = boost::asio::ip::tcp::socket;
    using stream_type = boost::asio::ssl::stream<socket_type&>;
    using error_code = boost::system::error_code;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;

    io_service_type io_service_;
    boost::optional<io_service_type::work> work_;
    std::thread thread_;
    std::shared_ptr<boost::asio::ssl::context> context_;

    template <class Streambuf>
    static
    void
    write(Streambuf& sb, std::string const& s)
    {
        using boost::asio::buffer;
        using boost::asio::buffer_copy;
        using boost::asio::buffer_size;
        boost::asio::const_buffers_1 buf(s.data(), s.size());
        sb.commit(buffer_copy(sb.prepare(buffer_size(buf)), buf));
    }

    //--------------------------------------------------------------------------

    class Base
    {
    protected:
        class Child
        {
        private:
            Base& base_;

        public:
            Child(Base& base)
                : base_(base)
            {
            }

            virtual ~Child()
            {
                base_.remove(this);
            }

            virtual void close() = 0;
        };

    private:
        std::mutex mutex_;
        std::condition_variable cond_;
        std::map<Child*, std::weak_ptr<Child>> list_;
        bool closed_ = false;

    public:
        ~Base()
        {
            // Derived class must call wait() in the destructor
            assert(list_.empty());
        }

        void
        add (std::shared_ptr<Child> const& child)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            list_.emplace(child.get(), child);
        }

        void
        remove (Child* child)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            list_.erase(child);
            if (list_.empty())
                cond_.notify_one();
        }

        void
        close()
        {
            std::vector<std::shared_ptr<Child>> v;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                v.reserve(list_.size());
                if (closed_)
                    return;
                closed_ = true;
                for(auto const& c : list_)
                {
                    if(auto p = c.second.lock())
                    {
                        p->close();
                        // Must destroy shared_ptr outside the
                        // lock otherwise deadlock from the
                        // managed object's destructor.
                        v.emplace_back(std::move(p));
                    }
                }
            }
        }

        void
        wait()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            while(! list_.empty())
                cond_.wait(lock);
        }
    };

    //--------------------------------------------------------------------------

    class Server : public Base
    {
    private:
        short_read_test& test_;
        endpoint_type endpoint_;

        struct Acceptor
            : Child, std::enable_shared_from_this<Acceptor>
        {
            Server& server_;
            short_read_test& test_;
            acceptor_type acceptor_;
            socket_type socket_;
            strand_type strand_;

            Acceptor(Server& server)
                : Child(server)
                , server_(server)
                , test_(server_.test_)
                , acceptor_(test_.io_service_,
                    endpoint_type(address_type::from_string("127.0.0.1"), 0))
                , socket_(test_.io_service_)
                , strand_(socket_.get_io_service())
            {
                acceptor_.listen();
                server_.endpoint_ = acceptor_.local_endpoint();
                test_.log << "[server] up on port: " <<
                    server_.endpoint_.port() << std::endl;
            }

            void
            close() override
            {
                if(! strand_.running_in_this_thread())
                    return strand_.post(std::bind(&Acceptor::close,
                        shared_from_this()));
                acceptor_.close();
            }

            void
            run()
            {
                acceptor_.async_accept(socket_, strand_.wrap(std::bind(
                    &Acceptor::on_accept, shared_from_this(),
                        std::placeholders::_1)));
            }

            void
            fail (std::string const& what, error_code ec)
            {
                if (acceptor_.is_open())
                {
                    if (ec != boost::asio::error::operation_aborted)
                        test_.log << what <<
                            ": " << ec.message() << std::endl;
                    acceptor_.close();
                }
            }

            void
            on_accept(error_code ec)
            {
                if (ec)
                    return fail ("accept", ec);
                auto const p = std::make_shared<Connection>(
                    server_, std::move(socket_));
                server_.add(p);
                p->run();
                acceptor_.async_accept(socket_, strand_.wrap(std::bind(
                    &Acceptor::on_accept, shared_from_this(),
                        std::placeholders::_1)));
            }
        };

        struct Connection
            : Child, std::enable_shared_from_this<Connection>
        {
            Server& server_;
            short_read_test& test_;
            socket_type socket_;
            stream_type stream_;
            strand_type strand_;
            timer_type timer_;
            boost::asio::streambuf buf_;

            Connection (Server& server, socket_type&& socket)
                : Child(server)
                , server_(server)
                , test_(server_.test_)
                , socket_(std::move(socket))
                , stream_(socket_, *test_.context_)
                , strand_(socket_.get_io_service())
                , timer_(socket_.get_io_service())
            {
            }

            void
            close() override
            {
                if(! strand_.running_in_this_thread())
                    return strand_.post(std::bind(&Connection::close,
                        shared_from_this()));
                if (socket_.is_open())
                {
                    socket_.close();
                    timer_.cancel();
                }
            }

            void
            run()
            {
                timer_.expires_from_now(std::chrono::seconds(3));
                timer_.async_wait(strand_.wrap(std::bind(&Connection::on_timer,
                    shared_from_this(), std::placeholders::_1)));
                stream_.async_handshake(stream_type::server, strand_.wrap(
                    std::bind(&Connection::on_handshake, shared_from_this(),
                        std::placeholders::_1)));
            }

            void
            fail (std::string const& what, error_code ec)
            {
                if (socket_.is_open())
                {
                    if (ec != boost::asio::error::operation_aborted)
                        test_.log << "[server] " << what <<
                            ": " << ec.message() << std::endl;
                    socket_.close();
                    timer_.cancel();
                }
            }

            void
            on_timer(error_code ec)
            {
                if (ec == boost::asio::error::operation_aborted)
                    return;
                if (ec)
                    return fail("timer", ec);
                test_.log << "[server] timeout" << std::endl;
                socket_.close();
            }

            void
            on_handshake(error_code ec)
            {
                if (ec)
                    return fail("handshake", ec);
#if 1
                boost::asio::async_read_until(stream_, buf_, "\n", strand_.wrap(
                    std::bind(&Connection::on_read, shared_from_this(),
                        std::placeholders::_1,
                            std::placeholders::_2)));
#else
                close();
#endif
            }

            void
            on_read(error_code ec, std::size_t bytes_transferred)
            {
                if (ec == boost::asio::error::eof)
                {
                    server_.test_.log << "[server] read: EOF" << std::endl;
                    return stream_.async_shutdown(strand_.wrap(std::bind(
                        &Connection::on_shutdown, shared_from_this(),
                            std::placeholders::_1)));
                }
                if (ec)
                    return fail("read", ec);

                buf_.commit(bytes_transferred);
                buf_.consume(bytes_transferred);
                write(buf_, "BYE\n");
                boost::asio::async_write(stream_, buf_.data(), strand_.wrap(
                    std::bind(&Connection::on_write, shared_from_this(),
                        std::placeholders::_1,
                            std::placeholders::_2)));
            }

            void
            on_write(error_code ec, std::size_t bytes_transferred)
            {
                buf_.consume(bytes_transferred);
                if (ec)
                    return fail("write", ec);
                stream_.async_shutdown(strand_.wrap(std::bind(
                    &Connection::on_shutdown, shared_from_this(),
                        std::placeholders::_1)));
            }

            void
            on_shutdown(error_code ec)
            {
                if (ec)
                    return fail("shutdown", ec);
                socket_.close();
                timer_.cancel();
            }
        };

    public:
        Server(short_read_test& test)
            : test_(test)
        {
            auto const p = std::make_shared<Acceptor>(*this);
            add(p);
            p->run();
        }

        ~Server()
        {
            close();
            wait();
        }

        endpoint_type const&
        endpoint () const
        {
            return endpoint_;
        }
    };

    //--------------------------------------------------------------------------
    class Client : public Base
    {
    private:
        short_read_test& test_;

        struct Connection
            : Child, std::enable_shared_from_this<Connection>
        {
            Client& client_;
            short_read_test& test_;
            socket_type socket_;
            stream_type stream_;
            strand_type strand_;
            timer_type timer_;
            boost::asio::streambuf buf_;

            Connection (Client& client)
                : Child(client)
                , client_(client)
                , test_(client_.test_)
                , socket_(test_.io_service_)
                , stream_(socket_, *test_.context_)
                , strand_(socket_.get_io_service())
                , timer_(socket_.get_io_service())
            {
            }

            void
            close() override
            {
                if(! strand_.running_in_this_thread())
                    return strand_.post(std::bind(&Connection::close,
                        shared_from_this()));
                if (socket_.is_open())
                {
                    socket_.close();
                    timer_.cancel();
                }
            }

            void
            run(endpoint_type const& ep)
            {
                timer_.expires_from_now(std::chrono::seconds(3));
                timer_.async_wait(strand_.wrap(std::bind(&Connection::on_timer,
                    shared_from_this(), std::placeholders::_1)));
                socket_.async_connect(ep, strand_.wrap(std::bind(
                    &Connection::on_connect, shared_from_this(),
                        std::placeholders::_1)));
            }

            void
            fail (std::string const& what, error_code ec)
            {
                if (socket_.is_open())
                {
                    if (ec != boost::asio::error::operation_aborted)
                        test_.log << "[client] " << what <<
                            ": " << ec.message() << std::endl;
                    socket_.close();
                    timer_.cancel();
                }
            }

            void
            on_timer(error_code ec)
            {
                if (ec == boost::asio::error::operation_aborted)
                    return;
                if (ec)
                    return fail("timer", ec);
                test_.log << "[client] timeout";
                socket_.close();
            }

            void
            on_connect(error_code ec)
            {
                if (ec)
                    return fail("connect", ec);
                stream_.async_handshake(stream_type::client, strand_.wrap(
                    std::bind(&Connection::on_handshake, shared_from_this(),
                        std::placeholders::_1)));
            }

            void
            on_handshake(error_code ec)
            {
                if (ec)
                    return fail("handshake", ec);
                write(buf_, "HELLO\n");

#if 1
                boost::asio::async_write(stream_, buf_.data(), strand_.wrap(
                    std::bind(&Connection::on_write, shared_from_this(),
                        std::placeholders::_1,
                            std::placeholders::_2)));
#else
                stream_.async_shutdown(strand_.wrap(std::bind(
                    &Connection::on_shutdown, shared_from_this(),
                        std::placeholders::_1)));
#endif
            }

            void
            on_write(error_code ec, std::size_t bytes_transferred)
            {
                buf_.consume(bytes_transferred);
                if (ec)
                    return fail("write", ec);
#if 1
                boost::asio::async_read_until(stream_, buf_, "\n", strand_.wrap(
                    std::bind(&Connection::on_read, shared_from_this(),
                        std::placeholders::_1,
                            std::placeholders::_2)));
#else
                stream_.async_shutdown(strand_.wrap(std::bind(
                    &Connection::on_shutdown, shared_from_this(),
                        std::placeholders::_1)));
#endif
            }

            void
            on_read(error_code ec, std::size_t bytes_transferred)
            {
                if (ec)
                    return fail("read", ec);
                buf_.commit(bytes_transferred);
                stream_.async_shutdown(strand_.wrap(std::bind(
                    &Connection::on_shutdown, shared_from_this(),
                        std::placeholders::_1)));
            }

            void
            on_shutdown(error_code ec)
            {

                if (ec)
                    return fail("shutdown", ec);
                socket_.close();
                timer_.cancel();
            }
        };

    public:
        Client(short_read_test& test, endpoint_type const& ep)
            : test_(test)
        {
            auto const p = std::make_shared<Connection>(*this);
            add(p);
            p->run(ep);
        }

        ~Client()
        {
            close();
            wait();
        }
    };

public:
    short_read_test()
        : work_(boost::in_place(std::ref(io_service_)))
        , thread_(std::thread([this]()
            {
                beast::setCurrentThreadName("io_service");
                this->io_service_.run();
            }))
        , context_(make_SSLContext(""))
    {
    }

    ~short_read_test()
    {
        work_ = boost::none;
        thread_.join();
    }

    void run() override
    {
        Server s(*this);
        Client c(*this, s.endpoint());
        c.wait();
        pass();
    }
};

BEAST_DEFINE_TESTSUITE(short_read,overlay,ripple);

}
