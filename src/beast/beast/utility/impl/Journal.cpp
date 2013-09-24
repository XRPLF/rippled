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

#include "../Journal.h"

namespace beast
{

bool Journal::Sink::active (Severity)
{
    return true;
}

//------------------------------------------------------------------------------

Journal::Sink& Journal::getNullSink ()
{
    // A Sink that does nothing.
    class NullSink : public Sink
    {
    public:
        void write (Severity, std::string const&)
        {
        }

        bool active (Severity)
        {
            return false;
        }
    };

    return *SharedSingleton <NullSink>::get (
        SingletonLifetime::neverDestroyed);
}

//------------------------------------------------------------------------------

Journal::ScopedStream::ScopedStream (Stream const& stream)
    : m_sink (stream.sink())
    , m_severity (stream.severity())
{
}

Journal::ScopedStream::ScopedStream (ScopedStream const& other)
    : m_sink (other.m_sink)
    , m_severity (other.m_severity)
{
}

Journal::ScopedStream::ScopedStream (Stream const& stream, std::ostream& manip (std::ostream&))
    : m_sink (stream.sink())
    , m_severity (stream.severity())
{
    m_ostream << manip;
}

Journal::ScopedStream::~ScopedStream ()
{
    if (m_sink.active (m_severity))
    {
        if (! m_ostream.str().empty())
            m_sink.write (m_severity, m_ostream.str());
    }
}

std::ostream& Journal::ScopedStream::operator<< (std::ostream& manip (std::ostream&)) const
{
    return m_ostream << manip;
}

std::ostringstream& Journal::ScopedStream::ostream () const
{
    return m_ostream;
}

//------------------------------------------------------------------------------

Journal::Stream::Stream ()
    : m_sink (&getNullSink ())
    , m_severity (kFatal)
{
}

Journal::Stream::Stream (Sink& sink, Severity severity)
    : m_sink (&sink)
    , m_severity (severity)
{
}

Journal::Stream::Stream (Stream const& other)
    : m_sink (other.m_sink)
    , m_severity (other.m_severity)
{
}

bool Journal::Stream::active () const
{
    return m_sink->active (m_severity);
}

Journal::Sink& Journal::Stream::sink () const
{
    return *m_sink;
}

Journal::Severity Journal::Stream::severity () const
{
    return m_severity;
}

Journal::Stream& Journal::Stream::operator= (Stream const& other)
{
    m_sink = other.m_sink;
    m_severity = other.m_severity;
    return *this;
}

Journal::ScopedStream Journal::Stream::operator<< (std::ostream& manip (std::ostream&)) const
{
    return ScopedStream (*this, manip);
}

//------------------------------------------------------------------------------

Journal::Journal ()
    : m_sink  (&getNullSink())
    , trace   (stream (kTrace))
    , debug   (stream (kDebug))
    , info    (stream (kInfo))
    , warning (stream (kWarning))
    , error   (stream (kError))
    , fatal   (stream (kFatal))
{
}

Journal::Journal (Sink& sink)
    : m_sink  (&sink)
    , trace   (stream (kTrace))
    , debug   (stream (kDebug))
    , info    (stream (kInfo))
    , warning (stream (kWarning))
    , error   (stream (kError))
    , fatal   (stream (kFatal))
{
}

Journal::Journal (Journal const& other)
    : m_sink  (other.m_sink)
    , trace   (stream (kTrace))
    , debug   (stream (kDebug))
    , info    (stream (kInfo))
    , warning (stream (kWarning))
    , error   (stream (kError))
    , fatal   (stream (kFatal))
{
}

Journal::~Journal ()
{
}

Journal::Stream Journal::stream (Severity severity) const
{
    return Stream (*m_sink, severity);
}

/** Returns `true` if the sink logs messages at that severity. */
bool Journal::active (Severity severity) const
{
    return m_sink->active (severity);
}

}
