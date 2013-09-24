//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_HTTP_SESSION_H_INCLUDED
#define RIPPLE_HTTP_SESSION_H_INCLUDED

#include <ostream>

namespace ripple {
using namespace beast;

namespace HTTP {

/** Persistent state information for a connection session.
    These values are preserved between calls for efficiency.
    Some fields are input parameters, some are output parameters,
    and all only become defined during specific callbacks.
*/
class Session : public Uncopyable
{
public:
    /** A user-definable pointer.
        The initial value is always zero.
        Changes to the value are persisted between calls.
    */
    void* tag;

    /** Returns the Journal to use for logging. */
    virtual Journal journal() = 0;

    /** Returns the remote address of the connection. */
    virtual IPEndpoint remoteAddress() = 0;

    /** Returns `true` if the full HTTP headers have been received. */
    virtual bool headersComplete() = 0;

    /** Returns the currently known set of headers. */
    virtual HTTPHeaders headers() = 0;

    /** Returns the complete HTTP request when it is known. */
    virtual SharedPtr <beast::HTTPRequest> const& request() = 0;

    /** Returns the entire Content-Body, if the request is complete. */
    virtual std::string content() = 0;

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
    ScopedStream operator<< (std::ostream& manip (std::ostream&))
    {
        return ScopedStream (*this, manip);
    }
        
    template <typename T>
    ScopedStream operator<< (T const& t)
    {
        return ScopedStream (*this, t);
    }
    /** @} */

    /** Detach the session.
        This holds the session open so that the response can be sent
        asynchronously. Calls to io_service::run made by the server
        will not return until all detached sessions are closed.
    */
    virtual void detach() = 0;

    /** Close the session.
        This will be performed asynchronously. The session will be
        closed gracefully after all pending writes have completed.
    */
    virtual void close() = 0;
};

}
}

#endif
