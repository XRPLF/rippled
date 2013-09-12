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

#ifndef BEAST_CORE_JOURNAL_H_INCLUDED
#define BEAST_CORE_JOURNAL_H_INCLUDED

/** A generic endpoint for log messages. */
class Journal
{
public:
    /** Severity level of the message. */
    enum Severity
    {
        kTrace,
        kDebug,
        kInfo,
        kWarning,
        kError,
        kFatal
    };

    //--------------------------------------------------------------------------

    /** Abstraction for the underlying message destination. */
    class Sink
    {
    public:
        virtual ~Sink () { }

        /** Write text to the sink at the specified severity. */
        virtual void write (Severity severity, std::string const& text) = 0;

        /** Returns `true` if text at the passed severity produces output. */
        virtual bool active (Severity)
        {
            return true;
        }
    };

    /** Returns a Sink which does nothing. */
    static Sink* getNullSink ();

    //--------------------------------------------------------------------------

    /** Scoped container for building journal messages. */
    class Stream
    {
    public:
        explicit Stream (Sink* sink, Severity severity)
            : m_sink (sink)
            , m_severity (severity)
        {
        }

        Stream (Stream const& other)
            : m_sink (other.m_sink)
            , m_severity (other.m_severity)
        {
        }

        Stream& operator= (Stream const& other)
        {
            m_sink = other.m_sink;
            m_severity = other.m_severity;
            return *this;
        }

        ~Stream ()
        {
            if (m_sink ->active (m_severity))
                m_sink->write (m_severity, m_stream.str ());
        }

        template <typename T>
        std::ostream& operator<< (T const& t) const
        {
            return m_stream << t;
        }

        std::ostringstream& stream () const
        {
            return m_stream;
        }

    private:
        Sink* m_sink;
        Severity m_severity;
        std::ostringstream mutable m_stream;
    };

    //--------------------------------------------------------------------------

    Journal (Sink* sink = getNullSink ())
        : m_sink (sink)
    {
    }

    bool reportActive (Severity severity) const
    {
        return m_sink->active (severity);
    }

    bool traceActive () const
    {
        return reportActive (kTrace);
    }

    bool debugActive () const
    {
        return reportActive (kDebug);
    }

    bool infoActive () const
    {
        return reportActive (kInfo);
    }

    bool warningActive () const
    {
        return reportActive (kWarning);
    }

    bool errorActive () const
    {
        return reportActive (kError);
    }

    bool fatalActive () const
    {
        return reportActive (kFatal);
    }

    Stream report (Severity severity) const
    {
        return Stream (m_sink, severity);
    }

    Stream trace ()  const
    {
        return report (kTrace);
    }

    Stream debug () const
    {
        return report (kDebug);
    }

    Stream info () const
    {
        return report (kInfo);
    }

    Stream warning () const
    {
        return report (kWarning);
    }

    Stream error () const
    {
        return report (kError);
    }

    Stream fatal () const
    {
        return Stream (m_sink, kFatal);
    }

private:
    Sink* m_sink;
};

#endif
