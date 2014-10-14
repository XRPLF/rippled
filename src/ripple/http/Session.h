//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_HTTP_SESSION_H_INCLUDED
#define RIPPLE_HTTP_SESSION_H_INCLUDED

#include <beast/http/message.h>
#include <beast/smart_ptr/SharedPtr.h>
#include <beast/net/IPEndpoint.h>
#include <beast/utility/Journal.h>
#include <beast/module/asio/http/HTTPRequest.h>
#include <ostream>

namespace ripple {

namespace HTTP {

class Session;

/** Scoped ostream-based RAII container for building the HTTP response. */
// VFALCO TODO Use abstract_ostream
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

//------------------------------------------------------------------------------

/** Persistent state information for a connection session.
    These values are preserved between calls for efficiency.
    Some fields are input parameters, some are output parameters,
    and all only become defined during specific callbacks.
*/
class Session
{
public:
    Session() = default;
    Session (Session const&) = delete;

    /** A user-definable pointer.
        The initial value is always zero.
        Changes to the value are persisted between calls.
    */
    void* tag = nullptr;

    /** Returns the Journal to use for logging. */
    virtual
    beast::Journal
    journal() = 0;

    /** Returns the remote address of the connection. */
    virtual
    beast::IP::Endpoint
    remoteAddress() = 0;

    /** Returns the currently known set of headers. */
    virtual
    beast::http::message&
    message() = 0;

    /** Send a copy of data asynchronously. */
    /** @{ */
    void
    write (std::string const& s)
    {
        if (! s.empty())
            write (&s[0],
                std::distance (s.begin(), s.end()));
    }

    template <typename BufferSequence>
    void
    write (BufferSequence const& buffers)
    {
        for (typename BufferSequence::const_iterator iter (buffers.begin());
            iter != buffers.end(); ++iter)
        {
            typename BufferSequence::value_type const& buffer (*iter);
            write (boost::asio::buffer_cast <void const*> (buffer),
                boost::asio::buffer_size (buffer));
        }
    }

    virtual
    void
    write (void const* buffer, std::size_t bytes) = 0;
    /** @} */

    /** Output support using ostream. */
    /** @{ */
    ScopedStream
    operator<< (std::ostream& manip (std::ostream&))
    {
        return ScopedStream (*this, manip);
    }

    template <typename T>
    ScopedStream
    operator<< (T const& t)
    {
        return ScopedStream (*this, t);
    }
    /** @} */

    /** Detach the session.
        This holds the session open so that the response can be sent
        asynchronously. Calls to io_service::run made by the server
        will not return until all detached sessions are closed.
    */
    virtual
    void
    detach() = 0;

    /** Indicate that the response is complete.
        The handler should call this when it has completed writing
        the response. If Keep-Alive is indicated on the connection,
        this will trigger a read for the next request; else, the
        connection will be closed when all remaining data has been sent.
    */
    virtual
    void
    complete() = 0;

    /** Close the session.
        This will be performed asynchronously. The session will be
        closed gracefully after all pending writes have completed.
        @param graceful `true` to wait until all data has finished sending.
    */
    virtual
    void
    close (bool graceful) = 0;
};

}  // namespace HTTP
}  // ripple

#endif
