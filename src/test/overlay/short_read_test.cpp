#include <test/jtx/envconfig.h>

#include <xrpl/basics/make_SSLContext.h>
#include <xrpl/beast/core/CurrentThreadName.h>
#include <xrpl/beast/unit_test.h>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/utility/in_place_factory.hpp>

#include <condition_variable>
#include <functional>
#include <thread>
#include <utility>

namespace xrpl {
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
    using io_context_type = boost::asio::io_context;
    using strand_type = boost::asio::strand<io_context_type::executor_type>;
    using timer_type = boost::asio::basic_waitable_timer<std::chrono::steady_clock>;
    using acceptor_type = boost::asio::ip::tcp::acceptor;
    using socket_type = boost::asio::ip::tcp::socket;
    using stream_type = boost::asio::ssl::stream<socket_type&>;
    using error_code = boost::system::error_code;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;

    io_context_type io_context_;
    boost::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_;
    std::thread thread_;
    std::shared_ptr<boost::asio::ssl::context> context_;

    template <class Streambuf>
    static void
    write(Streambuf& sb, std::string const& s)
    {
        using boost::asio::buffer;
        using boost::asio::buffer_copy;
        using boost::asio::buffer_size;
        boost::asio::const_buffer buf(s.data(), s.size());
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
            explicit Child(Base& base) : base_(base)
            {
            }

            virtual ~Child()
            {
                base_.remove(this);
            }

            virtual void
            close() = 0;
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
        add(std::shared_ptr<Child> const& child)
        {
            std::lock_guard lock(mutex_);
            list_.emplace(child.get(), child);
        }

        void
        remove(Child* child)
        {
            std::lock_guard lock(mutex_);
            list_.erase(child);
            if (list_.empty())
                cond_.notify_one();
        }

        void
        close()
        {
            std::vector<std::shared_ptr<Child>> v;
            {
                std::lock_guard lock(mutex_);
                v.reserve(list_.size());
                if (closed_)
                    return;
                closed_ = true;
                for (auto const& c : list_)
                {
                    if (auto p = c.second.lock())
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
            while (!list_.empty())
                cond_.wait(lock);
        }
    };

    //--------------------------------------------------------------------------

    class Server : public Base
    {
    private:
        short_read_test& test_;
        endpoint_type endpoint_;

        struct Acceptor : Child, std::enable_shared_from_this<Acceptor>
        {
            Server& server;
            short_read_test& test;
            acceptor_type acceptor;
            socket_type socket;
            strand_type strand;

            explicit Acceptor(Server& server)
                : Child(server)
                , server(server)
                , test(server.test_)
                , acceptor(
                      test.io_context_,
                      endpoint_type(boost::asio::ip::make_address(test::getEnvLocalhostAddr()), 0))
                , socket(test.io_context_)
                , strand(boost::asio::make_strand(test.io_context_))
            {
                acceptor.listen();
                server.endpoint_ = acceptor.local_endpoint();
            }

            void
            close() override
            {
                if (!strand.running_in_this_thread())
                    return post(strand, std::bind(&Acceptor::close, shared_from_this()));
                acceptor.close();
            }

            void
            run()
            {
                acceptor.async_accept(
                    socket,
                    bind_executor(
                        strand,
                        std::bind(
                            &Acceptor::on_accept, shared_from_this(), std::placeholders::_1)));
            }

            void
            fail(std::string const& what, error_code ec)
            {
                if (acceptor.is_open())
                {
                    if (ec != boost::asio::error::operation_aborted)
                        test.log << what << ": " << ec.message() << std::endl;
                    acceptor.close();
                }
            }

            void
            on_accept(error_code ec)
            {
                if (ec)
                    return fail("accept", ec);
                auto const p = std::make_shared<Connection>(server, std::move(socket));
                server.add(p);
                p->run();
                acceptor.async_accept(
                    socket,
                    bind_executor(
                        strand,
                        std::bind(
                            &Acceptor::on_accept, shared_from_this(), std::placeholders::_1)));
            }
        };

        struct Connection : Child, std::enable_shared_from_this<Connection>
        {
            Server& server;
            short_read_test& test;
            socket_type socket;
            stream_type stream;
            strand_type strand;
            timer_type timer;
            boost::asio::streambuf buf;

            Connection(Server& server, socket_type&& socket)
                : Child(server)
                , server(server)
                , test(server.test_)
                , socket(std::move(socket))
                , stream(socket, *test.context_)
                , strand(boost::asio::make_strand(test.io_context_))
                , timer(test.io_context_)
            {
            }

            void
            close() override
            {
                if (!strand.running_in_this_thread())
                    return post(strand, std::bind(&Connection::close, shared_from_this()));
                if (socket.is_open())
                {
                    socket.close();
                    timer.cancel();
                }
            }

            void
            run()
            {
                timer.expires_after(std::chrono::seconds(3));
                timer.async_wait(bind_executor(
                    strand,
                    std::bind(&Connection::on_timer, shared_from_this(), std::placeholders::_1)));
                stream.async_handshake(
                    stream_type::server,
                    bind_executor(
                        strand,
                        std::bind(
                            &Connection::on_handshake, shared_from_this(), std::placeholders::_1)));
            }

            void
            fail(std::string const& what, error_code ec)
            {
                if (socket.is_open())
                {
                    if (ec != boost::asio::error::operation_aborted)
                        test.log << "[server] " << what << ": " << ec.message() << std::endl;
                    socket.close();
                    timer.cancel();
                }
            }

            void
            on_timer(error_code ec)
            {
                if (ec == boost::asio::error::operation_aborted)
                    return;
                if (ec)
                    return fail("timer", ec);
                test.log << "[server] timeout" << std::endl;
                socket.close();
            }

            void
            on_handshake(error_code ec)
            {
                if (ec)
                    return fail("handshake", ec);
#if 1
                boost::asio::async_read_until(
                    stream,
                    buf,
                    "\n",
                    bind_executor(
                        strand,
                        std::bind(
                            &Connection::on_read,
                            shared_from_this(),
                            std::placeholders::_1,
                            std::placeholders::_2)));
#else
                close();
#endif
            }

            void
            on_read(error_code ec, std::size_t bytesTransferred)
            {
                if (ec == boost::asio::error::eof)
                {
                    server.test_.log << "[server] read: EOF" << std::endl;
                    return stream.async_shutdown(bind_executor(
                        strand,
                        std::bind(
                            &Connection::on_shutdown, shared_from_this(), std::placeholders::_1)));
                }
                if (ec)
                    return fail("read", ec);

                buf.commit(bytesTransferred);
                buf.consume(bytesTransferred);
                write(buf, "BYE\n");
                boost::asio::async_write(
                    stream,
                    buf.data(),
                    bind_executor(
                        strand,
                        std::bind(
                            &Connection::on_write,
                            shared_from_this(),
                            std::placeholders::_1,
                            std::placeholders::_2)));
            }

            void
            on_write(error_code ec, std::size_t bytesTransferred)
            {
                buf.consume(bytesTransferred);
                if (ec)
                    return fail("write", ec);
                stream.async_shutdown(bind_executor(
                    strand,
                    std::bind(
                        &Connection::on_shutdown, shared_from_this(), std::placeholders::_1)));
            }

            void
            on_shutdown(error_code ec)
            {
                if (ec)
                    return fail("shutdown", ec);
                socket.close();
                timer.cancel();
            }
        };

    public:
        explicit Server(short_read_test& test) : test_(test)
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
        endpoint() const
        {
            return endpoint_;
        }
    };

    //--------------------------------------------------------------------------
    class Client : public Base

    {
    private:
        short_read_test& test_;

        struct Connection : Child, std::enable_shared_from_this<Connection>
        {
            Client& client;
            short_read_test& test;
            socket_type socket;
            stream_type stream;
            strand_type strand;
            timer_type timer;
            boost::asio::streambuf buf;
            endpoint_type const& ep;

            Connection(Client& client, endpoint_type const& ep)
                : Child(client)
                , client(client)
                , test(client.test_)
                , socket(test.io_context_)
                , stream(socket, *test.context_)
                , strand(boost::asio::make_strand(test.io_context_))
                , timer(test.io_context_)
                , ep(ep)
            {
            }

            void
            close() override
            {
                if (!strand.running_in_this_thread())
                    return post(strand, std::bind(&Connection::close, shared_from_this()));
                if (socket.is_open())
                {
                    socket.close();
                    timer.cancel();
                }
            }

            void
            run(endpoint_type const& ep)
            {
                timer.expires_after(std::chrono::seconds(3));
                timer.async_wait(bind_executor(
                    strand,
                    std::bind(&Connection::on_timer, shared_from_this(), std::placeholders::_1)));
                socket.async_connect(
                    ep,
                    bind_executor(
                        strand,
                        std::bind(
                            &Connection::on_connect, shared_from_this(), std::placeholders::_1)));
            }

            void
            fail(std::string const& what, error_code ec)
            {
                if (socket.is_open())
                {
                    if (ec != boost::asio::error::operation_aborted)
                        test.log << "[client] " << what << ": " << ec.message() << std::endl;
                    socket.close();
                    timer.cancel();
                }
            }

            void
            on_timer(error_code ec)
            {
                if (ec == boost::asio::error::operation_aborted)
                    return;
                if (ec)
                    return fail("timer", ec);
                test.log << "[client] timeout";
                socket.close();
            }

            void
            on_connect(error_code ec)
            {
                if (ec)
                    return fail("connect", ec);
                stream.async_handshake(
                    stream_type::client,
                    bind_executor(
                        strand,
                        std::bind(
                            &Connection::on_handshake, shared_from_this(), std::placeholders::_1)));
            }

            void
            on_handshake(error_code ec)
            {
                if (ec)
                    return fail("handshake", ec);
                write(buf, "HELLO\n");

#if 1
                boost::asio::async_write(
                    stream,
                    buf.data(),
                    bind_executor(
                        strand,
                        std::bind(
                            &Connection::on_write,
                            shared_from_this(),
                            std::placeholders::_1,
                            std::placeholders::_2)));
#else
                stream_.async_shutdown(bind_executor(
                    strand_,
                    std::bind(
                        &Connection::on_shutdown, shared_from_this(), std::placeholders::_1)));
#endif
            }

            void
            on_write(error_code ec, std::size_t bytesTransferred)
            {
                buf.consume(bytesTransferred);
                if (ec)
                    return fail("write", ec);
#if 1
                boost::asio::async_read_until(
                    stream,
                    buf,
                    "\n",
                    bind_executor(
                        strand,
                        std::bind(
                            &Connection::on_read,
                            shared_from_this(),
                            std::placeholders::_1,
                            std::placeholders::_2)));
#else
                stream_.async_shutdown(bind_executor(
                    strand_,
                    std::bind(
                        &Connection::on_shutdown, shared_from_this(), std::placeholders::_1)));
#endif
            }

            void
            on_read(error_code ec, std::size_t bytesTransferred)
            {
                if (ec)
                    return fail("read", ec);
                buf.commit(bytesTransferred);
                stream.async_shutdown(bind_executor(
                    strand,
                    std::bind(
                        &Connection::on_shutdown, shared_from_this(), std::placeholders::_1)));
            }

            void
            on_shutdown(error_code ec)
            {
                if (ec)
                    return fail("shutdown", ec);
                socket.close();
                timer.cancel();
            }
        };

    public:
        Client(short_read_test& test, endpoint_type const& ep) : test_(test)
        {
            auto const p = std::make_shared<Connection>(*this, ep);
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
        : work_(io_context_.get_executor())
        , thread_(std::thread([this]() {
            beast::setCurrentThreadName("io_context");
            this->io_context_.run();
        }))
        , context_(make_SSLContext(""))
    {
    }

    ~short_read_test()
    {
        work_.reset();
        thread_.join();
    }

    void
    run() override
    {
        Server s(*this);
        Client c(*this, s.endpoint());
        c.wait();
        pass();
    }
};

BEAST_DEFINE_TESTSUITE(short_read, overlay, xrpl);

}  // namespace xrpl
