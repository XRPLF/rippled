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
#include <cassert>

namespace beast {

//------------------------------------------------------------------------------

// A Sink that does nothing.
class NullJournalSink : public Journal::Sink
{
public:
    NullJournalSink ()
    : Sink (severities::kDisabled, false)
    { }

    ~NullJournalSink() override = default;

    bool active (severities::Severity) const override
    {
        return false;
    }

    bool console() const override
    {
        return false;
    }

    void console (bool) override
    {
    }

    severities::Severity threshold() const override
    {
        return severities::kDisabled;
    }

    void threshold (severities::Severity) override
    {
    }

    void write (severities::Severity, std::string const&) override
    {
    }
};

//------------------------------------------------------------------------------

Journal::Sink& Journal::getNullSink ()
{
    static NullJournalSink sink;
    return sink;
}

//------------------------------------------------------------------------------

Journal::Sink::Sink (Severity thresh, bool console)
    : thresh_ (thresh)
    , m_console (console)
{
}

Journal::Sink::~Sink ()
{
}

bool Journal::Sink::active (Severity level) const
{
    return level >= thresh_;
}

bool Journal::Sink::console () const
{
    return m_console;
}

void Journal::Sink::console (bool output)
{
    m_console = output;
}

Journal::Severity Journal::Sink::threshold () const
{
    return thresh_;
}

void Journal::Sink::threshold (Severity thresh)
{
    thresh_ = thresh;
}

//------------------------------------------------------------------------------

Journal::ScopedStream::ScopedStream (ScopedStream const& other)
    : m_sink (other.m_sink)
    , m_level (other.m_level)
{
    init ();
}

Journal::ScopedStream::ScopedStream (Sink& sink, Severity level)
    : m_sink (sink)
    , m_level (level)
{
    init ();
}

Journal::ScopedStream::~ScopedStream ()
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

void Journal::ScopedStream::init ()
{
    // Modifiers applied from all ctors
    m_ostream
        << std::boolalpha
        << std::showbase;
}

std::ostream& Journal::ScopedStream::operator<< (std::ostream& manip (std::ostream&)) const
{
    return m_ostream << manip;
}

//------------------------------------------------------------------------------

Journal::Stream::Stream ()
    : m_sink (&getNullSink ())
    , m_level (severities::kDisabled)
{
}

Journal::Stream::Stream (Sink& sink, Severity level)
    : m_sink (&sink)
    , m_level (level)
{
    assert (level != severities::kDisabled);
}

Journal::Stream::Stream (Stream const& other)
    : m_sink (other.m_sink)
    , m_level (other.m_level)
{
}

Journal::Sink& Journal::Stream::sink () const
{
    return *m_sink;
}

severities::Severity Journal::Stream::severity () const
{
    return m_level;
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
    , trace   (stream (severities::kTrace))
    , debug   (stream (severities::kDebug))
    , info    (stream (severities::kInfo))
    , warning (stream (severities::kWarning))
    , error   (stream (severities::kError))
    , fatal   (stream (severities::kFatal))
{
}

Journal::Journal (Sink& sink)
    : m_sink  (&sink)
    , trace   (stream (severities::kTrace))
    , debug   (stream (severities::kDebug))
    , info    (stream (severities::kInfo))
    , warning (stream (severities::kWarning))
    , error   (stream (severities::kError))
    , fatal   (stream (severities::kFatal))
{
}

Journal::Journal (Journal const& other)
    : Journal  (*other.m_sink)
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
    return Stream (*m_sink, level);
}

bool Journal::active (Severity level) const
{
    return m_sink->active (level);
}

}
