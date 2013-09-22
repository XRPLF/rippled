//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_HTTP_SESSION_H_INCLUDED
#define RIPPLE_HTTP_SESSION_H_INCLUDED

#include <ostream>

namespace ripple {
namespace HTTP {

using namespace beast;

/** Persistent state information for a connection session.
    These values are preserved between calls for efficiency.
    Some fields are input parameters, some are output parameters,
    and all only become defined during specific callbacks.
*/
class Session : public Uncopyable
{
public:
    Session ();

    /** Input: The Journal the server is using. */
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
