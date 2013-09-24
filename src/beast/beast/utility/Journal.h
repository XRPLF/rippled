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

#ifndef BEAST_UTILITY_JOURNAL_H_INCLUDED
#define BEAST_UTILITY_JOURNAL_H_INCLUDED

#include <sstream>

namespace beast
{

/** A generic endpoint for log messages. */
class Journal
{
public:
    class Sink;

private:
    Sink* m_sink;

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

        /** Returns `true` if text at the passed severity produces output.
            The default implementation always returns `true`.
        */
        virtual bool active (Severity);
    };

    /** Returns a Sink which does nothing. */
    static Sink& getNullSink ();

    //--------------------------------------------------------------------------
    
    class Stream;

    /** Scoped ostream-based container for writing messages to a Journal. */
    class ScopedStream
    {
    public:
        explicit ScopedStream (Stream const& stream);
        ScopedStream (ScopedStream const& other);

        template <typename T>
        ScopedStream (Stream const& stream, T const& t)
            : m_sink (stream.sink())
            , m_severity (stream.severity())
        {
            m_ostream << t;
        }

        ScopedStream (Stream const& stream, std::ostream& manip (std::ostream&));

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

        Sink& m_sink;
        Severity const m_severity;
        std::ostringstream mutable m_ostream;
    };

    //--------------------------------------------------------------------------

    class Stream
    {
    public:
        /** Construct a stream which produces no logging output. */
        Stream ();

        Stream (Sink& sink, Severity severity);
        Stream (Stream const& other);
        Stream& operator= (Stream const& other);

        /** Returns `true` if the sink logs messages at the severity of this stream. */
        bool active() const;

        Sink& sink() const;
        Severity severity() const;

        ScopedStream operator<< (std::ostream& manip (std::ostream&)) const;

        template <typename T>
        ScopedStream operator<< (T const& t) const
        {
            return ScopedStream (*this, t);
        }

    private:
        Sink* m_sink;
        Severity m_severity;
    };

    //--------------------------------------------------------------------------

    Journal ();
    explicit Journal (Sink& sink);
    Journal (Journal const& other);
    ~Journal ();

    /** Returns a stream for this sink, with the specified severity. */
    Stream stream (Severity severity) const;

    /** Returns `true` if the sink logs messages at that severity. */
    bool active (Severity severity) const;

    /** Convenience sink streams for each severity level. */
    Stream const trace;
    Stream const debug;
    Stream const info;
    Stream const warning;
    Stream const error;
    Stream const fatal;

private:
    Journal& operator= (Journal const& other); // disallowed
};

}

#endif
