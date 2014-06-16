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

#include <beast/asio/wrap_handler.h>
#include <beast/asio/placeholders.h>
#include <beast/unit_test/suite.h>
#include <boost/asio/ssl/stream.hpp>
#include <beast/cxx14/memory.h> // <memory>

namespace beast {
namespace asio {

class HTTPClientType : public HTTPClientBase, public Uncopyable
{
public:
    class Session;

    struct State
    {
        List <Session> list;
    };

    typedef SharedData <State> SharedState;

    SharedState m_state;
    Journal m_journal;
    double m_timeoutSeconds;
    std::size_t m_messageLimitBytes;
    std::size_t m_bufferSize;
    boost::asio::io_service m_io_service;
    WaitableEvent m_stopped;

    //--------------------------------------------------------------------------

    HTTPClientType (
        Journal journal,
        double timeoutSeconds,
        std::size_t messageLimitBytes,
        std::size_t bufferSize)
        : m_journal (journal)
        , m_timeoutSeconds (timeoutSeconds)
        , m_messageLimitBytes (messageLimitBytes)
        , m_bufferSize (bufferSize)
        , m_stopped (true, true) // manual reset, initially signaled
    {
    }

    ~HTTPClientType ()
    {
        cancel();
        wait();
    }

    result_type get (URL const& url)
    {
        result_type result;
        boost::asio::io_service io_service;
        async_get (io_service, url, std::bind (
            &HTTPClientType::handle_get, std::placeholders::_1, &result));
        io_service.run ();
        return result;
    }

    void async_get (boost::asio::io_service& io_service, URL const& url,
        asio::shared_handler <void (result_type)> handler)
    {
        new Session (*this, io_service, url,
            handler, m_timeoutSeconds, m_messageLimitBytes, m_bufferSize);
    }

    void cancel ()
    {
        SharedState::Access state (m_state);
        for (List <Session>::iterator iter (state->list.begin());
            iter != state->list.end(); ++iter)
            iter->cancel();
    }

    void wait()
    {
        m_stopped.wait();
    }

    //--------------------------------------------------------------------------

    void add (Session& session)
    {
        SharedState::Access state (m_state);
        if (state->list.empty())
            m_stopped.reset();
        state->list.push_back (session);
    }

    void remove (Session& session)
    {
        SharedState::Access state (m_state);
        state->list.erase (state->list.iterator_to (session));
        if (state->list.empty())
            m_stopped.signal();
    }

    static void handle_get (result_type const& result, result_type* dest)
    {
        *dest = result;
    }

    Journal journal() const
    {
        return m_journal;
    }

    boost::asio::io_service& get_io_service()
    {
        return m_io_service;
    }

    //--------------------------------------------------------------------------

    /** Helper function to get a const_buffer from a String. */
    static boost::asio::const_buffers_1 stringBuffer (String const& s)
    {
        return boost::asio::const_buffers_1 (s.getCharPointer (), s.length ());
    }

    /** Helper function to fill out a Query from a URL. */
    template <typename Query>
    static Query queryFromURL (URL const& url)
    {
        if (url.port () != 0)
        {
            return Query (
                url.host().toStdString(),
                url.port_string().toStdString(),
                Query::numeric_service);
        }

        return Query (
            url.host().toStdString(),
            url.scheme().toStdString());
    }

    //--------------------------------------------------------------------------

    class Session
        : public SharedObject
        , public List <Session>::Node
    {
    public:
        typedef SharedPtr <Session>         Ptr;
        typedef boost::asio::ip::tcp        Protocol;
        typedef boost::system::error_code   error_code;
        typedef HTTPClientBase::error_type  error_type;
        typedef HTTPClientBase::value_type  value_type;
        typedef HTTPClientBase::result_type result_type;

        typedef Protocol::resolver          resolver;
        typedef Protocol::socket            socket;
        typedef resolver::query             query;
        typedef resolver::iterator          iterator;
        typedef iterator::value_type        resolver_entry;

        HTTPClientType& m_owner;
        boost::asio::io_service& m_io_service;
        boost::asio::io_service::strand m_strand;
        boost::asio::deadline_timer m_timer;
        resolver m_resolver;
        socket m_socket;
        asio::shared_handler <void (result_type)> m_handler;

        URL m_url;
        boost::asio::ssl::context m_context;
        MemoryBlock m_buffer;
        HTTPResponseParser m_parser;
        std::size_t m_messageLimitBytes;
        std::size_t m_bytesReceived;

        String m_get_string;
        WaitableEvent m_done;
        std::unique_ptr <abstract_socket> m_stream;

        struct State
        {
            State () : complete (false)
            {
            }

            bool complete;
            error_code error;
            SharedPtr <HTTPResponse> response;
        };
        typedef SharedData <State> SharedState;
        SharedState m_state;

        //----------------------------------------------------------------------

        Session (HTTPClientType& owner,
                 boost::asio::io_service& io_service,
                 URL const& url,
                 asio::shared_handler <void (result_type)> const& handler,
                 double timeoutSeconds,
                 std::size_t messageLimitBytes,
                 std::size_t bufferSize)
            : m_owner (owner)
            , m_io_service (io_service)
            , m_strand (io_service)
            , m_timer (io_service)
            , m_resolver (io_service)
            , m_socket (io_service)
            , m_handler (handler)
            , m_url (url)
            , m_context (boost::asio::ssl::context::sslv23)
            , m_buffer (bufferSize)
            , m_messageLimitBytes (messageLimitBytes)
            , m_bytesReceived (0)
        {
            m_owner.add (*this);

            // Configure the SSL context for certificate verification
            m_context.set_default_verify_paths ();
            m_context.set_options (
                boost::asio::ssl::context::no_sslv2 |
                boost::asio::ssl::context::single_dh_use |
                boost::asio::ssl::context::default_workarounds);
            //m_context.set_verify_mode (boost::asio::ssl::verify_peer);

            // Set the timer if a timeout is requested
            if (timeoutSeconds > 0)
            {
                m_timer.expires_from_now (
                    boost::posix_time::milliseconds (
                        long (timeoutSeconds * 1000)));

                m_timer.async_wait (m_strand.wrap (asio::wrap_handler (
                    std::bind (&Session::handle_timer, Ptr(this),
                        asio::placeholders::error), m_handler)));
            }

            // Start the operation on an io_service thread
            io_service.dispatch (m_strand.wrap (asio::wrap_handler (
                std::bind (&Session::handle_start, Ptr(this)), m_handler)));
        }

        ~Session ()
        {
            State result;
            {
                SharedState::ConstAccess state (m_state);
                result = *state;
            }

            m_io_service.post (bind_handler (m_handler,
                std::make_pair (result.error, result.response)));

            m_owner.remove (*this);
        }

        //----------------------------------------------------------------------

        // Called by the owner to cancel pending i/o.
        void cancel ()
        {
            {
                SharedState::Access state (m_state);
                if (! state->complete)
                {
                    state->complete = true;
                    state->error = boost::asio::error::operation_aborted;
                }
            }

            cancel_all();
        }

        // Cancel all pending I/O
        void cancel_all ()
        {
            error_code ec;
            m_timer.cancel (ec);
            m_resolver.cancel ();
            m_socket.cancel (ec);
            m_socket.shutdown (socket::shutdown_both, ec);
        }

        // Called by a completion handler when error is not eof or aborted.
        void failed (error_code ec)
        {
            {
                SharedState::Access state (m_state);
                if (! state->complete)
                {
                    state->complete = true;
                    state->error = ec;
                    state->response = nullptr;
                }
            }

            cancel_all();
        }

        void async_read_some ()
        {
            boost::asio::mutable_buffers_1 buf (
                m_buffer.getData (), m_buffer.getSize ());

            m_stream->async_read_some (buf, m_strand.wrap (
                asio::wrap_handler (std::bind (&Session::handle_read,
                    Ptr(this), asio::placeholders::error,
                        asio::placeholders::bytes_transferred), m_handler)));
        }

        //----------------------------------------------------------------------
        //
        // Completion handlers
        //

        // Called when the operation starts
        void handle_start ()
        {
            query q (queryFromURL <query> (m_url));

            m_resolver.async_resolve (q, m_strand.wrap (
                asio::wrap_handler (std::bind (&Session::handle_resolve,
                    Ptr(this), asio::placeholders::error,
                        asio::placeholders::iterator), m_handler)));
        }

        // Called when the timer completes
        void handle_timer (error_code ec)
        {
            if (ec == boost::asio::error::operation_aborted)
                return;

            if (ec != 0)
            {
                failed (ec);
                return;
            }

            failed (boost::system::errc::make_error_code (
                boost::system::errc::timed_out));
        }

        // Called when the resolver completes
        void handle_resolve (error_code ec, iterator iter)
        {
            if (ec == boost::asio::error::operation_aborted)
                return;

            if (ec != 0)
            {
                failed (ec);
                return;
            }

            resolver_entry const entry (*iter);
            m_socket.async_connect (entry.endpoint (), m_strand.wrap (
                asio::wrap_handler (std::bind (&Session::handle_connect,
                    Ptr(this), asio::placeholders::error), m_handler)));
        }

        // Called when the connection attempt completes
        void handle_connect (error_code ec)
        {
            if (ec == boost::asio::error::operation_aborted)
                return;

            if (ec != 0)
            {
                failed (ec);
                return;
            }

            if (m_url.scheme () == "https")
            {
                typedef boost::asio::ssl::stream <socket&> ssl_stream;
                m_stream = std::make_unique <
                    socket_wrapper <ssl_stream>> (m_socket, m_context);
                /*
                m_stream->set_verify_mode (
                    boost::asio::ssl::verify_peer |
                    boost::asio::ssl::verify_fail_if_no_peer_cert);
                */
                m_stream->async_handshake (abstract_socket::client, m_strand.wrap (
                    asio::wrap_handler (std::bind (&Session::handle_handshake,
                        Ptr(this), asio::placeholders::error), m_handler)));
                return;
            }

            m_stream = std::make_unique <socket_wrapper <socket&>> (m_socket);
            handle_handshake (ec);
        }

        // Called when the SSL handshake completes
        void handle_handshake (error_code ec)
        {
            if (ec == boost::asio::error::operation_aborted)
                return;

            if (ec != 0)
            {
                failed (ec);
                return;
            }

            m_get_string =
                "GET " + m_url.path() + " HTTP/1.1\r\n" +
                "Host: " + m_url.host() + "\r\n" +
                "Accept: */*\r\n" +
                "Connection: close\r\n\r\n";

            boost::asio::async_write (*m_stream, stringBuffer (
                m_get_string), m_strand.wrap (asio::wrap_handler (
                    std::bind (&Session::handle_write, Ptr(this),
                        asio::placeholders::error,
                            asio::placeholders::bytes_transferred), m_handler)));

            async_read_some ();
        }

        // Called when the write operation completes
        void handle_write (error_code ec, std::size_t)
        {
            if (ec == boost::asio::error::operation_aborted)
                return;

            if (ec != 0)
            {
                failed (ec);
                return;
            }

            if (! m_stream->needs_handshake ())
                m_socket.shutdown (socket::shutdown_send, ec);
        }

        void handle_read (error_code ec,
            std::size_t bytes_transferred)
        {
            if (ec == boost::asio::error::operation_aborted)
                return;

            if (ec != 0)
            {
                failed (ec);
                return;
            }

            m_bytesReceived += bytes_transferred;
            if (m_bytesReceived > m_messageLimitBytes)
            {
                failed (error_code (
                    boost::system::errc::invalid_argument,
                    boost::system::system_category ()));
                return;
            }

            std::size_t const bytes_parsed (m_parser.process (
                m_buffer.getData (), bytes_transferred));

            if (m_parser.error ())
            {
                failed (error_code (
                    boost::system::errc::invalid_argument,
                    boost::system::system_category ()));
                return;
            }

            if (bytes_parsed != bytes_transferred)
            {
                failed (error_code (
                    boost::system::errc::invalid_argument,
                    boost::system::system_category ()));
                return;
            }

            if (ec == boost::asio::error::eof)
                m_parser.process_eof ();

            if (m_parser.finished ())
            {
                if (m_stream->needs_handshake ())
                {
                    m_stream->async_shutdown (m_strand.wrap (asio::wrap_handler (
                        std::bind (&Session::handle_shutdown,
                            Ptr(this), asio::placeholders::error), m_handler)));
                }
                else
                {
                    handle_shutdown (error_code ());
                }
                return;
            }

            async_read_some ();
        }

        void handle_shutdown (error_code ec)
        {
            if (ec == boost::asio::error::operation_aborted)
                return;

            if (ec == boost::asio::error::eof)
                ec = error_code();

            if (ec != 0)
            {
                failed (ec);
                return;
            }

            {
                SharedState::Access state (m_state);
                if (! state->complete)
                {
                    state->complete = true;
                    state->response = m_parser.response();
                }
            }

            cancel_all();
        }
    };
};

//------------------------------------------------------------------------------

HTTPClientBase* HTTPClientBase::New (Journal journal,
    double timeoutSeconds, std::size_t messageLimitBytes, std::size_t bufferSize)
{
    return new HTTPClientType (
        journal, timeoutSeconds, messageLimitBytes, bufferSize);
}

//------------------------------------------------------------------------------

class HTTPClient_test : public unit_test::suite
{
public:
    typedef boost::system::error_code error_code;

    //--------------------------------------------------------------------------

    class IoServiceThread : protected Thread
    {
    public:
        explicit IoServiceThread (String name = "io_service")
            : Thread (name)
        {
        }

        ~IoServiceThread ()
        {
            join ();
        }

        boost::asio::io_service& get_io_service ()
        {
            return m_service;
        }

        void start ()
        {
            startThread ();
        }

        void join ()
        {
            this->waitForThreadToExit ();
        }

    private:
        void run ()
        {
            m_service.run ();
        }

    private:
        boost::asio::io_service m_service;
    };

    //--------------------------------------------------------------------------

    void print (HTTPMessage const& m)
    {
        for (int i = 0; i < m.headers().size(); ++i)
        {
            HTTPField const f (m.headers()[i]);
            std::stringstream ss;
            log <<
                "[ '" << f.name() <<
                "' , '" << f.value() + "' ]";
        }
    }

    void print (HTTPClientBase::error_type error,
        HTTPClientBase::value_type const& response)
    {
        if (error != 0)
        {
            log <<
                "HTTPClient error: '" + error.message() << "'";
        }
        else if (! response.empty ())
        {
            log <<
                "Status: " <<
                String::fromNumber (response->status()).toStdString();

            print (*response);
        }
        else
        {
            log <<
                "HTTPClient: no response";
        }
    }

    //--------------------------------------------------------------------------

    void handle_get (HTTPClientBase::result_type result)
    {
        print (result.first, result.second);
    }

    void testSync (String const& s, double timeoutSeconds)
    {
        std::unique_ptr <HTTPClientBase> client (
            HTTPClientBase::New (Journal(), timeoutSeconds));

        HTTPClientBase::result_type const& result (
            client->get (ParsedURL (s).url ()));

        print (result.first, result.second);
    }

    void testAsync (String const& s, double timeoutSeconds)
    {
        IoServiceThread t;
        std::unique_ptr <HTTPClientBase> client (
            HTTPClientBase::New (Journal(), timeoutSeconds));

        client->async_get (t.get_io_service (), ParsedURL (s).url (),
            std::bind (&HTTPClient_test::handle_get, this,
                std::placeholders::_1));

        t.start ();
        t.join ();
    }

    //--------------------------------------------------------------------------

    void run ()
    {
        testSync (
            "http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference.html",
            5);

        testAsync (
            "http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference.html",
            5);

        testAsync (
            "https://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference.html",
            5);

        pass ();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(HTTPClient,beast_asio,beast);

}
}
