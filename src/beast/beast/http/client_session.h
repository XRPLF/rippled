//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_ASIO_HTTP_BASIC_SESSION_H_INCLUDED
#define BEAST_ASIO_HTTP_BASIC_SESSION_H_INCLUDED

#include <beast/http/basic_url.h>
#include <beast/http/raw_parser.h>
#include <beast/http/detail/header_traits.h>

#include <beast/asio/bind_handler.h>
#include <beast/asio/enable_wait_for_async.h>
#include <beast/asio/placeholders.h>
#include <beast/asio/shared_handler.h>
#include <beast/utility/is_call_possible.h>
#include <beast/utility/ci_char_traits.h>

#include <boost/asio/buffer.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/streambuf.hpp>
//#include <boost/optional.hpp>
#include <boost/utility/string_ref.hpp>
#include <boost/logic/tribool.hpp>

#include <beast/cxx14/memory.h> // <memory>
#include <beast/cxx14/type_traits.h> // <type_traits>

#include <sstream> // REMOVE ASAP!

namespace beast {
namespace http {

template <class T, class Alloc>
boost::asio::basic_streambuf <Alloc>&
operator<< (boost::asio::basic_streambuf <Alloc>& stream, T const& t)
{
    std::stringstream ss;
    ss << t;
    std::string const s (ss.str());
    auto const b (boost::asio::buffer (s));
    auto const len (boost::asio::buffer_size (b));
    boost::asio::buffer_copy (stream.prepare (len), b);
    stream.commit (len);
    return stream;
}

template <class Alloc>
boost::asio::basic_streambuf <Alloc>&
operator<< (boost::asio::basic_streambuf <Alloc>& stream,
    std::string const& s)
{
    auto const b (boost::asio::buffer (s));
    auto const len (boost::asio::buffer_size (b));
    boost::asio::buffer_copy (stream.prepare (len), b);
    stream.commit (len);
    return stream;
}

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable: 4100) // unreferenced formal parameter
#endif

/** Provides asynchronous HTTP client service on a socket. */
template <class Socket>
class client_session
    : public asio::enable_wait_for_async <client_session <Socket>>
    , private raw_parser::callback
{
private:
    BOOST_TRIBOOL_THIRD_STATE(unspecified);

    BEAST_DEFINE_IS_CALL_POSSIBLE(has_keep_alive, keep_alive);

    template <class Cond>
    struct Enabled : public std::integral_constant <bool, Cond::value>
    {
    };

    static_assert (! std::is_const <Socket>::value,
        "Socket cannot be const");

    typedef boost::system::error_code error_code;
    typedef boost::asio::streambuf write_buffers;
    typedef boost::basic_string_ref <
        char, ci_char_traits> ci_string_ref;

    class abstract_request;
    class abstract_response;

    Socket m_socket;
    boost::asio::io_service::strand m_strand;
    boost::asio::deadline_timer m_timer;

    asio::shared_handler <void(error_code)> m_handler;
    std::unique_ptr <abstract_request> m_request;
    std::unique_ptr <abstract_response> m_response;

    raw_parser m_parser;
    write_buffers m_write_buffer;
    boost::asio::mutable_buffer m_read_buffer;
    std::string m_field;
    std::string m_value;

    bool m_complete : 1;
    bool m_keep_alive : 1;

public:
    typedef Socket stream_type;

    client_session& operator= (client_session const&) = delete;

    template <class... Args>
    explicit client_session (Args&&... args)
        : m_socket (std::forward <Args> (args)...)
        , m_strand (m_socket.get_io_service())
        , m_timer (m_socket.get_io_service())
        , m_parser (*this)
    {
    }

    ~client_session() = default;

    /** Returns the stream associated with the session. */
    /** @{ */
    stream_type&
    stream()
    {
        return m_socket;
    }

    stream_type const&
    stream() const
    {
        return m_socket;
    }
    /** @} */

    void
    cancel()
    {
        error_code ec;
        m_socket.cancel(ec);
    }

    /** Fetch a resource asynchronously. */
    template <
        class Request,
        class Response,
        class Handler
    >
    void
    async_get (Request request, Response response, Handler&& handler)
    {
        m_handler = std::forward <Handler> (handler);

        m_request = std::make_unique <
            wrapped_request <Request>> (request);
        
        m_response = std::make_unique <
            wrapped_response <Response>> (response);
            
        start();
    }

    template <
        class Handler
    >
    void
    async_get (std::string const&) noexcept
    {
    }

private:
    class abstract_request
    {
    public:
        virtual
        ~abstract_request()
        {
        }

        virtual
        boost::tribool
        keep_alive () = 0;

        virtual
        void
        headers (write_buffers& buffer) = 0;
    };

    template <class Request>
    class wrapped_request : public abstract_request
    {
    private:
        typedef std::remove_reference_t <Request> request_type;

        Request m_request;

    public:
        explicit wrapped_request (Request request)
            : m_request (request)
        {
        }

        wrapped_request (wrapped_request const&) = delete;

    private:
        boost::tribool
        keep_alive() override
        {
            return keep_alive (Enabled <has_keep_alive <
                request_type, bool ()>>());
        }

        boost::tribool
        keep_alive (std::true_type)
        {
            return m_request.keep_alive();
        }

        boost::tribool
        keep_alive (std::false_type)
        {
            return unspecified;
        }

        class submit
        {
        private:
            write_buffers& m_buffer;

        public:
            explicit submit (write_buffers& buffer)
                : m_buffer (buffer)
            {
            }

            // Throws if an invalid request field is specified.
            // Invalid fields are ones that the client_session inserts
            // itself, such as keep-alive.
            //
            static void check_request_field (std::string const& field)
            {
                static std::vector <std::string> reserved =
                {
                    "Content-Length",
                    "Connection"
                };

                if (std::any_of (reserved.cbegin(), reserved.cend(),
                    [&](typename decltype(reserved)::value_type const& s)
                    {
                        return detail::field_eq (field, s);
                    }))
                    throw std::invalid_argument (
                        "Reserved HTTP header in request");
            }

            template <class F, class V>
            void
            operator() (F const& f, V const& v)
            {
                check_request_field (f);

                auto const fb (boost::asio::buffer (f));
                m_buffer.commit (boost::asio::buffer_copy (
                    m_buffer.prepare (boost::asio::buffer_size (fb)), fb));
                m_buffer << ": ";
                auto const vb (boost::asio::buffer (v));
                m_buffer.commit (boost::asio::buffer_copy (
                    m_buffer.prepare (boost::asio::buffer_size (vb)), vb));
                m_buffer << "\r\n";
            }
        };

        void
        headers (write_buffers& buffer) override
        {
            submit f (buffer);
            m_request.template headers <
                std::add_lvalue_reference_t <submit>> (f);
        }
    };

    class abstract_response
    {
    public:
        abstract_response() = default;
        abstract_response (abstract_response const&) = default;
        abstract_response& operator= (abstract_response const&) = default;

        virtual
        ~abstract_response() = default;

        virtual
        boost::asio::mutable_buffer
        buffer () = 0;

        virtual
        error_code
        header (std::string const& field,
            std::string const& value) = 0;

        virtual
        error_code
        body (boost::asio::const_buffer in) = 0;

    };

    template <class Response>
    class wrapped_response : public abstract_response
    {
    private:
        Response m_response;

    public:
        explicit wrapped_response (Response response)
            : m_response (response)
        {
        }

        wrapped_response (wrapped_response const&) = delete;

        boost::asio::mutable_buffer
        buffer() override
        {
            return m_response.buffer();
        }

        error_code
        header (std::string const& field,
            std::string const& value) override
        {
            return m_response.header (field, value);
        }

        virtual
        error_code
        body (boost::asio::const_buffer in)
        {
            return m_response.body (in);
        }
    };

    void upcall (error_code const& ec, bool continuation = true)
    {
        if (m_handler)
        {
            // TODO cancel all pending i/o here?

            if (continuation)
            {
                m_handler (ec);
                m_handler = nullptr;
            }
            else
            {
                m_socket.get_io_service().post (
                    asio::bind_handler (
                        std::move (m_handler), ec));
                assert (! m_handler);
            }
        }
    }

    void start()
    {
        // reset and setup state

        m_parser.reset (raw_parser::response);

        m_write_buffer.consume (m_write_buffer.size());
        m_write_buffer <<
            "GET / HTTP/1.0\r\n";
        m_request->headers (m_write_buffer);
        m_write_buffer <<
            "Content-Length: 0\r\n"
            "\r\n";

        m_read_buffer = m_response->buffer();

        m_field = std::string();
        m_value = std::string();

        m_complete = false;
        m_keep_alive = false;

        async_write_some (false);
    }

    //
    // request
    //

    bool
    async_write_some (bool continuation = true)
    {
        auto const& data (m_write_buffer.data());
        auto const size (boost::asio::buffer_size (
            m_write_buffer.data()));

        if (size > 0)
        {
            m_socket.async_write_some (data, this->wrap_with_counter (
                m_strand.wrap (asio::wrap_handler (std::bind (
                    &client_session::handle_write, this,
                        asio::placeholders::error,
                            asio::placeholders::bytes_transferred),
                                continuation))));
            return true;
        }
        return false;
    }

    void
    handle_write (error_code ec, std::size_t bytes_transferred)
    {
        if (ec)
            return upcall (ec);

        m_write_buffer.consume (bytes_transferred);

        if (async_write_some())
            return;

        // write finished

        //if (! keep_alive)
        {
            m_socket.shutdown (
                boost::asio::socket_base::shutdown_send, ec);
            // VFALCO What do we do with ec?
        }

        // now read

        async_read_some (true);
    }

    //
    // response
    //

    void
    async_read_some (bool continuation = true)
    {
        m_socket.async_read_some (boost::asio::mutable_buffers_1 (
            m_read_buffer), this->wrap_with_counter (
                m_strand.wrap (asio::wrap_handler (std::bind (
                    &client_session::handle_read, this,
                        asio::placeholders::error,
                            asio::placeholders::bytes_transferred),
                                continuation))));
    };

    void
    handle_read (error_code ec, std::size_t bytes_transferred)
    {
        if (ec != boost::asio::error::eof)
        {
            if (ec)
                return upcall (ec);

            std::size_t bytes_consumed;
            std::tie (ec, bytes_consumed) = m_parser.process_data (
                boost::asio::buffer_cast <void const*> (m_read_buffer),
                    bytes_transferred);

            // TODO Handle leftover bytes
            //assert (ec || bytes_consumed == bytes_transferred);

            if (ec)
                return upcall (ec);

            if (! m_complete)
                return async_read_some();
        }
        else
        {
            // This is here for when we expect keep-alive but
            // the server ends up closing the connection erroneously.
            if (m_keep_alive)
            {
                m_keep_alive = false;
                // warning: Got EOF on keep-alive
            }

            ec = m_parser.process_eof();
        }

        if (! m_complete)
        {
            // custom error
            ec = error_code (boost::system::errc::no_message_available,
                boost::system::generic_category());
        }

        if (ec)
            return upcall (ec);

        // We have a complete response

        if (! m_keep_alive)
        {
            // VFALCO NOTE This is surely wrong for ssl::stream
            {
                error_code ec_;
                m_socket.shutdown (
                    boost::asio::socket_base::shutdown_receive, ec_);
            }

            {
                error_code ec_;
                m_socket.close (ec_);
                assert (! ec_);
            }
        }

        m_request.reset();
        m_response.reset();
        m_handler (ec);

        // done
    }

    //
    // parser
    //

    error_code
    do_header()
    {
        error_code ec;

        if (! m_value.empty())
        {
            ec = m_response->header (m_field, m_value);

            m_field.clear();
            m_value.clear();
        }

        return ec;
    }

    error_code
    on_response () override
    {
        m_field = decltype(m_field)();
        m_value = decltype(m_value)();
        return error_code();
    }
        
    error_code
    on_url (
        void const* in, std::size_t bytes) override
    {
        // Shouldn't be called for HTTP responses
        assert (false);
        return error_code();
    }

    error_code
    on_status (int status_code,
        void const* in, std::size_t bytes) override
    {
        return error_code();
    }

    error_code
    on_header_field (
        void const* in, std::size_t bytes) override
    {
        do_header();
        m_field.append (static_cast <char const*> (in), bytes);
        return error_code();
    }

    error_code
    on_header_value (
        void const* in, std::size_t bytes) override
    {
        m_value.append (static_cast <char const*> (in), bytes);
        return error_code();
    }

    error_code
    on_headers_done (
        bool keep_alive) override
    {
        do_header();

        return error_code();
    }

    error_code
    on_body (bool,
        void const* in, std::size_t bytes) override
    {
        m_response->body (
            boost::asio::const_buffer (in, bytes));
        return error_code();
    }
        
    error_code
    on_message_complete (bool keep_alive) override
    {
        m_keep_alive = keep_alive;
        m_complete = true;
        return error_code();
    }
};

#ifdef _MSC_VER
#pragma warning (pop)
#endif

//------------------------------------------------------------------------------

/** Synchronous HTTP client session. */
template <class Socket>
class sync_client_session
{
private:
    typedef boost::system::error_code error_code;

    boost::asio::io_service m_ios;
    Socket m_socket;
    error_code m_ec;

    static_assert (std::is_same <Socket, std::decay_t <Socket>>::value,
        "Socket cannot be a reference or const type");

    struct sync_handler
    {
        std::reference_wrapper <sync_client_session> m_session;

        sync_handler (sync_client_session& session)
            : m_session (session)
        {
        }

        void operator() (boost::system::error_code ec)
        {
            m_session.get().m_ec = ec;
        }
    };

public:
    typedef std::remove_reference_t <Socket> next_layer_type;
    typedef typename next_layer_type::lowest_layer_type lowest_layer_type;

    sync_client_session()
        : m_socket (m_ios)
    {
    }

    sync_client_session (sync_client_session const&) = delete;

    // VFALCO We might be able to get away with having move ctor/assign

    ~sync_client_session() = default;

    next_layer_type&
    next_layer() noexcept
    {
        return m_socket;
    }

    next_layer_type const&
    next_layer() const noexcept
    {
    }

    lowest_layer_type&
    lowest_layer() noexcept
    {
        return m_socket.lowest_layer();
    }

    lowest_layer_type const&
    lowest_layer() const noexcept
    {
        return m_socket.lowest_layer();
    }

    template <class Request, class Response>
    error_code
    get (Request& request, Response& response)
    {
        client_session <Socket&> session (m_socket);
        session.template async_get <
            std::add_lvalue_reference_t <Request>,
            std::add_lvalue_reference_t <Response>,
            sync_handler> (
                request, response, sync_handler (*this));
        m_ios.run();
        m_ios.reset();
        return m_ec;
    }
};

}
}

#endif
