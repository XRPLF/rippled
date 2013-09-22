//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_FRAME_HTTPSERVER_H_INCLUDED
#define RIPPLE_FRAME_HTTPSERVER_H_INCLUDED

#include <ostream>

namespace ripple {

using namespace beast;

/** Multi-threaded, asynchronous HTTP server. */
class HTTPServer
{
public:
    /** Configuration information for a listening port. */
    struct Port
    {
        enum Security
        {
            no_ssl,
            allow_ssl,
            require_ssl
        };

        Port ();
        Port (Port const& other);
        Port& operator= (Port const& other);
        Port (uint16 port_, IPEndpoint const& addr_,
              Security security_, SSLContext* context_);

        uint16 port;
        IPEndpoint addr;
        Security security;
        SSLContext* context;
    };

    //--------------------------------------------------------------------------

    class Session;

    /** Scoped ostream-based RAII container for building the HTTP response. */
    class ScopedStream
    {
    public:
        explicit ScopedStream (Session& session);
        ScopedStream (ScopedStream const& other);

        template <typename T>
        ScopedStream (Session& session, T const& t)
            : m_session (session)
        {
            m_ostream << t;
        }

        ScopedStream (Session& session, std::ostream& manip (std::ostream&));

        ~ScopedStream ();

        std::ostringstream& ostream () const;

        std::ostream& operator<< (std::ostream& manip (std::ostream&)) const;

        template <typename T>
        std::ostream& operator<< (T const& t) const
        {
            return m_ostream << t;
        }

    private:
        ScopedStream& operator= (ScopedStream const&); // disallowed

        Session& m_session;
        std::ostringstream mutable m_ostream;
    };

    //--------------------------------------------------------------------------

    /** Persistent state information for a connection session.
        These values are preserved between calls for efficiency.
        Some fields are input parameters, some are output parameters,
        and all only become defined during specific callbacks.
    */
    class Session : public Uncopyable
    {
    public:
        Session ();

        /** Input: The Journal the HTTPServer is using. */
        Journal journal;

        /** Input: The remote address of the connection. */
        IPEndpoint remoteAddress;

        /** Input: `true` if all the headers have been received. */
        bool headersComplete;

        /** Input: The currently known set of HTTP headers. */
        HTTPHeaders headers;

        /** Input: The full HTTPRequest when it is known. */
        SharedPtr <HTTPRequest> request;

        /** Input: The Content-Body as a linear buffer if we have the HTTPRequest. */
        std::string content;

        /** Output: The buffer to send back as a reply.
            Upon each entry into the callback, reply.size() will be zero.
            If reply.size() is zero when the callback returns, no data is
            sent.
        */
        std::string reply;

        /** A user-definable pointer.
            The initial value is always zero.
            Changes to the value are persisted between calls.
        */
        void* tag;



        /** Send a copy of data asynchronously. */
        /** @{ */
        void write (std::string const& s)
        {
            if (! s.empty())
                write (&s.front(),
                    std::distance (s.begin(), s.end()));
        }

        template <typename BufferSequence>
        void write (BufferSequence const& buffers)
        {
            for (typename BufferSequence::const_iterator iter (buffers.begin());
                iter != buffers.end(); ++iter)
            {
                typename BufferSequence::value_type const& buffer (*iter);
                write (boost::asio::buffer_cast <void const*> (buffer),
                    boost::asio::buffer_size (buffer));
            }
        }

        virtual void write (void const* buffer, std::size_t bytes) = 0;
        /** @} */

        /** Output support using ostream. */
        /** @{ */
        ScopedStream operator<< (std::ostream& manip (std::ostream&));
        
        template <typename T>
        ScopedStream operator<< (T const& t)
        {
            return ScopedStream (*this, t);
        }
        /** @} */

        /** Detach the session.
            This holds the session open so that the response can be sent
            asynchronously. Calls to io_service::run made by the HTTPServer
            will not return until all detached sessions are closed.
        */
        virtual void detach() = 0;

        /** Close the session.
            This will be performed asynchronously. The session will be
            closed gracefully after all pending writes have completed.
        */
        virtual void close() = 0;
    };

    //--------------------------------------------------------------------------

    /** Processes all sessions.
        Thread safety:
            Must be safe to call concurrently from any number of foreign threads.
    */
    struct Handler
    {
        /** Called when the connection is accepted and we know remoteAddress. */
        virtual void onAccept (Session& session) = 0;

        /** Called repeatedly as new HTTP headers are received.
            Guaranteed to be called at least once.
        */
        virtual void onHeaders (Session& session) = 0;

        /** Called when we have the full Content-Body. */
        virtual void onRequest (Session& session) = 0;

        /** Called when the session ends.
            Guaranteed to be called once.
        */
        virtual void onClose (Session& session) = 0;

        /** Called when the HTTPServer has finished its stop. */
        virtual void onStopped (HTTPServer& server) = 0;
    };

    //--------------------------------------------------------------------------

    /** A set of listening ports settings. */
    typedef std::vector <Port> Ports;

    /** Create the server using the specified handler. */
    HTTPServer (Handler& handler, Journal journal);

    /** Destroy the server.
        This blocks until the server stops.
    */
    virtual ~HTTPServer ();

    /** Returns the Journal associated with the server. */
    Journal const& journal () const;

    /** Returns the listening ports settings.
        Thread safety:
            Safe to call from any thread.
            Cannot be called concurrently with setPorts.
    */
    Ports const& getPorts () const;

    /** Set the listening ports settings.
        These take effect immediately. Any current ports that are not in the
        new set will be closed. Established connections will not be disturbed.
        Thread safety:
            Cannot be called concurrently.
    */
    void setPorts (Ports const& ports);

    /** Notify the server to stop, without blocking.
        Thread safety:
            Safe to call concurrently from any thread.
    */
    void stopAsync ();

    /** Notify the server to stop, and block until the stop is complete.
        The handler's onStopped method will be called when the stop completes.
        Thread safety:
            Cannot be called concurrently.
            Cannot be called from the thread of execution of any Handler functions.
    */
    void stop ();

private:
    class Impl;
    ScopedPointer <Impl> m_impl;
};

int  compare    (HTTPServer::Port const& lhs, HTTPServer::Port const& rhs);
bool operator== (HTTPServer::Port const& lhs, HTTPServer::Port const& rhs);
bool operator!= (HTTPServer::Port const& lhs, HTTPServer::Port const& rhs);
bool operator<  (HTTPServer::Port const& lhs, HTTPServer::Port const& rhs);
bool operator<= (HTTPServer::Port const& lhs, HTTPServer::Port const& rhs);
bool operator>  (HTTPServer::Port const& lhs, HTTPServer::Port const& rhs);
bool operator>= (HTTPServer::Port const& lhs, HTTPServer::Port const& rhs);

}

#endif
