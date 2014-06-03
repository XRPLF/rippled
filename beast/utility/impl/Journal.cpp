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

#include <beast/utility/Journal.h>
#include <beast/module/core/memory/SharedSingleton.h>

namespace beast {

//------------------------------------------------------------------------------

// A Sink that does nothing.
class NullJournalSink : public Journal::Sink
{
public:
    bool active (Journal::Severity) const
    {
        return false;
    }

    bool console() const
    {
        return false;
    }

    void console (bool)
    {
    }

    Journal::Severity severity() const
    {
        return Journal::kDisabled;
    }

    void severity (Journal::Severity)
    {
    }

    void write (Journal::Severity, std::string const&)
    {
    }
};

//------------------------------------------------------------------------------

Journal::Sink& Journal::getNullSink ()
{
    return *SharedSingleton <NullJournalSink>::get (
        SingletonLifetime::neverDestroyed);
}

//------------------------------------------------------------------------------

Journal::Sink::Sink ()
    : m_level (kAll)
    , m_console (false)
{
}

Journal::Sink::~Sink ()
{
}

bool Journal::Sink::active (Severity level) const
{
    return level >= m_level;
}

bool Journal::Sink::console () const
{
    return m_console;
}

void Journal::Sink::console (bool output)
{
    m_console = output;
}

Journal::Severity Journal::Sink::severity () const
{
    return m_level;
}

void Journal::Sink::severity (Severity level)
{
    m_level = level;
}

//------------------------------------------------------------------------------

Journal::ScopedStream::ScopedStream (Stream const& stream)
    : m_sink (stream.sink ())
    , m_level (stream.severity ())
    , m_active (stream.active ())
{
    init ();
}

Journal::ScopedStream::ScopedStream (ScopedStream const& other)
    : m_sink (other.m_sink)
    , m_level (other.m_level)
    , m_active (other.m_active)
{
    init ();
}

Journal::ScopedStream::ScopedStream (
    Stream const& stream, std::ostream& manip (std::ostream&))
    : m_sink (stream.sink ())
    , m_level (stream.severity ())
    , m_active (stream.active ())
{
    init ();
    if (active ())
        m_ostream << manip;
}

Journal::ScopedStream::~ScopedStream ()
{
    if (active ())
    {
        std::string const& s (m_ostream.str());
        if (! s.empty ())
        {
            if (s == "\n")
                m_sink.write (m_level, "");
            else
                m_sink.write (m_level, s);
        }
    }
}

void Journal::ScopedStream::init ()
{
    // Modifiers applied from all ctors
    m_ostream
        << std::boolalpha
        << std::showbase
        //<< std::hex
        ;

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
    , m_level (kDisabled)
    , m_disabled (true)
{
}

Journal::Stream::Stream (Sink& sink, Severity level, bool active)
    : m_sink (&sink)
    , m_level (level)
    , m_disabled (! active)
{
    bassert (level != kDisabled);
}

Journal::Stream::Stream (Stream const& stream, bool active)
    : m_sink (&stream.sink ())
    , m_level (stream.severity ())
    , m_disabled (! active)
{
}

Journal::Stream::Stream (Stream const& other)
    : m_sink (other.m_sink)
    , m_level (other.m_level)
    , m_disabled (other.m_disabled)
{
}

Journal::Sink& Journal::Stream::sink () const
{
    return *m_sink;
}

Journal::Severity Journal::Stream::severity () const
{
    return m_level;
}

bool Journal::Stream::active () const
{
    return ! m_disabled && m_sink->active (m_level);
}

Journal::Stream& Journal::Stream::operator= (Stream const& other)
{
    m_sink = other.m_sink;
    m_level = other.m_level;
    return *this;
}

Journal::ScopedStream Journal::Stream::operator<< (
    std::ostream& manip (std::ostream&)) const
{
    return ScopedStream (*this, manip);
}

//------------------------------------------------------------------------------

Journal::Journal ()
    : m_sink  (&getNullSink())
    , m_level (kDisabled)
    , trace   (stream (kTrace))
    , debug   (stream (kDebug))
    , info    (stream (kInfo))
    , warning (stream (kWarning))
    , error   (stream (kError))
    , fatal   (stream (kFatal))
{
}

Journal::Journal (Sink& sink, Severity level)
    : m_sink  (&sink)
    , m_level (level)
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
    , m_level (other.m_level)
    , trace   (stream (kTrace))
    , debug   (stream (kDebug))
    , info    (stream (kInfo))
    , warning (stream (kWarning))
    , error   (stream (kError))
    , fatal   (stream (kFatal))
{
}

Journal::Journal (Journal const& other, Severity level)
    : m_sink  (other.m_sink)
    , m_level (std::max (other.m_level, level))
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

Journal::Sink& Journal::sink() const
{
    return *m_sink;
}

Journal::Stream Journal::stream (Severity level) const
{
    return Stream (*m_sink, level, level >= m_level);
}

bool Journal::active (Severity level) const
{
    if (level == kDisabled)
        return false;
    if (level < m_level)
        return false;
    return m_sink->active (level);
}

Journal::Severity Journal::severity () const
{
    return m_level;
}

}
