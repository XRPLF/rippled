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

namespace beast {

/** A generic endpoint for log messages.

    The Journal has a few simple goals:

     * To be light-weight and copied by value.
     * To allow logging statements to be left in source code.
     * The logging is controlled at run-time based on a logging threshold.

    It is advisable to check Journal::active(level) prior to formatting log
    text.  Doing so sidesteps expensive text formatting when the results
    will not be sent to the log.
*/
class Journal
{
public:
    /** Severity level / threshold of a Journal message. */
    enum Severity
    {
        kAll = 0,

        kTrace = kAll,
        kDebug,
        kInfo,
        kWarning,
        kError,
        kFatal,

        kDisabled,
        kNone = kDisabled
    };

    class Sink;

private:
    Sink* m_sink;

public:
    //--------------------------------------------------------------------------

    /** Abstraction for the underlying message destination. */
    class Sink
    {
    protected:
        Sink () = delete;
        explicit Sink(Sink const& sink) = default;
        Sink (Severity thresh, bool console);

    public:
        virtual ~Sink () = 0;

        /** Returns `true` if text at the passed severity produces output. */
        virtual bool active (Severity level) const;

        /** Returns `true` if a message is also written to the Output Window (MSVC). */
        virtual bool console () const;

        /** Set whether messages are also written to the Output Window (MSVC). */
        virtual void console (bool output);

        /** Returns the minimum severity level this sink will report. */
        virtual Severity threshold() const;

        /** Set the minimum severity this sink will report. */
        virtual void threshold (Severity thresh);

        /** Write text to the sink at the specified severity.
            A conforming implementation will not write the text if the passed
            level is below the current threshold().
        */
        virtual void write (Severity level, std::string const& text) = 0;

    private:
        Severity thresh_;
        bool m_console;
    };

    /** Returns a Sink which does nothing. */
    static Sink& getNullSink ();

    //--------------------------------------------------------------------------

    class Stream;
private:
    /** Scoped ostream-based container for writing messages to a Journal. */
    class ScopedStream
    {
    public:
        ScopedStream (ScopedStream const& other);
        ScopedStream (Sink& sink, Severity level);

        template <typename T>
        ScopedStream (Stream const& stream, T const& t)
            : m_sink (stream.sink())
            , m_level (stream.severity())
        {
            m_ostream << t;
        }

        ScopedStream& operator= (ScopedStream const&) = delete;

        ~ScopedStream ();

        std::ostream& operator<< (
            std::ostream& manip (std::ostream&)) const;

        template <typename T>
        std::ostream& operator<< (T const& t) const
        {
            m_ostream << t;
            return m_ostream;
        }

    private:
        void init ();

        Sink& m_sink;
        Severity const m_level; // Cached for call to m_sink.write().
        std::ostringstream mutable m_ostream;
    };

//------------------------------------------------------------------------------
private:
    /** Templated TScopedStream. */
    template <Severity LEVEL>
    class TScopedStream
    {
    public:
        static_assert (LEVEL < kDisabled, "Invalid streaming LEVEL");

        TScopedStream() = delete;
        TScopedStream (TScopedStream const& other);

        explicit TScopedStream (Sink& sink);

        TScopedStream& operator= (TScopedStream const&) = delete;

        ~TScopedStream ();

        std::ostream& operator<< (
            std::ostream& manip (std::ostream&)) const;

        template <typename T>
        std::ostream& operator<< (T const& t) const;

    private:
        Sink& m_sink;
        std::ostringstream mutable m_ostream;
    };

//------------------------------------------------------------------------------
private:
    /** Templated TScopedStreamProxy. */
    template <Severity LEVEL>
    class TScopedStreamProxy
    {
    public:
        static_assert (LEVEL < kDisabled, "Invalid streaming LEVEL");

        TScopedStreamProxy() = delete;
        TScopedStreamProxy (TScopedStreamProxy const&) = default;
        explicit TScopedStreamProxy (Journal const& j)
            : m_sink (j.sink())
        {
        }

        TScopedStreamProxy& operator= (TScopedStreamProxy const&) = delete;

        /** Returns `true` if m_sink logs anything at this stream's severity. */
        /** @{ */
        bool active() const
        {
            return m_sink.active (LEVEL);
        }

        explicit
        operator bool() const
        {
            return active();
        }
        /** @} */

        /** Postpone creating an actual TScopedStream until streaming. */
        /** @{ */
        TScopedStream<LEVEL> operator<< (
            std::ostream& manip (std::ostream&)) const;

        template <typename T>
        TScopedStream<LEVEL> operator<< (T const& t) const;
        /** @} */

    private:
        Sink& m_sink;
    };

    //--------------------------------------------------------------------------
public:
    class Stream
    {
    public:
        /** Create a stream which produces no output. */
        Stream ();

        /** Create stream that writes at the given level. */
        Stream (Sink& sink, Severity level);

        /** Construct or copy another Stream. */
        /** @{ */
        Stream (Stream const& other);
        Stream& operator= (Stream const& other);
        /** @} */

        /** Returns the Sink that this Stream writes to. */
        Sink& sink() const;

        /** Returns the Severity of messages this Stream reports. */
        Severity severity() const;

        /** Returns `true` if sink logs anything at this stream's severity. */
        /** @{ */
        bool active() const
        {
            return m_sink->active (m_level);
        }

        explicit
        operator bool() const
        {
            return active();
        }
        /** @} */

        /** Output stream support. */
        /** @{ */
        ScopedStream operator<< (std::ostream& manip (std::ostream&)) const;

        template <typename T>
        ScopedStream operator<< (T const& t) const
        {
            return ScopedStream (*this, t);
        }
        /** @} */

    private:
        Sink* m_sink;
        Severity m_level;
    };

    //--------------------------------------------------------------------------

    /** Create a journal that writes to the null sink. */
    Journal ();

    /** Create a journal that writes to the specified sink. */
    explicit Journal (Sink& sink);

    /** Create a journal from another journal. */
    Journal (Journal const& other);

    // Disallowed.
    Journal& operator= (Journal const& other) = delete;

    /** Destroy the journal. */
    ~Journal ();

    /** Returns the Sink associated with this Journal. */
    Sink& sink() const;

    /** Returns a stream for this sink, with the specified severity. */
    Stream stream (Severity level) const;

    /** Returns `true` if any message would be logged at this severity level.
        For a message to be logged, the severity must be at or above the
        sink's severity threshold.
    */
    bool active (Severity level) const;

    /** Convenience sink streams for each severity level. */
    Stream const trace;
    Stream const debug;
    Stream const info;
    Stream const warning;
    Stream const error;
    Stream const fatal;

    /** Severity stream access functions. Defined inside Journal for ADL.

        By declaring and defining these functions inside Journal, they
        will only be found through Argument Dependent Lookup.  That means
        we can use "good" names (like "warn") and not worry about those
        names hiding other uses of these good names outside of Journal.

        These functions want to be inlined for minimal cost active() check.
    */
    /** @{ */
    friend
    TScopedStreamProxy<kTrace>
    trace(Journal const& j)
    {
        return TScopedStreamProxy<kTrace>(j);
    }

    friend
    TScopedStreamProxy<kDebug>
    debug(Journal const& j)
    {
        return TScopedStreamProxy<kDebug>(j);
    }

    friend
    TScopedStreamProxy<kInfo>
    info(Journal const& j)
    {
        return TScopedStreamProxy<kInfo>(j);
    }

    friend
    TScopedStreamProxy<kWarning>
    warn(Journal const& j)
    {
        return TScopedStreamProxy<kWarning>(j);
    }

    friend
    TScopedStreamProxy<kError>
    error(Journal const& j)
    {
        return TScopedStreamProxy<kError>(j);
    }

    friend
    TScopedStreamProxy<kFatal>
    fatal(Journal const& j)
    {
        return TScopedStreamProxy<kFatal>(j);
    }
    /** @} */
};

//------------------------------------------------------------------------------

template <Journal::Severity LEVEL>
Journal::TScopedStream<LEVEL>::TScopedStream (TScopedStream const& other)
: TScopedStream (other.m_sink)
{
}

template <Journal::Severity LEVEL>
Journal::TScopedStream<LEVEL>::TScopedStream (Sink& sink)
: m_sink (sink)
{
    // All constructors start with these modifiers.
    m_ostream
        << std::boolalpha
        << std::showbase;
}

template <Journal::Severity LEVEL>
Journal::TScopedStream<LEVEL>::~TScopedStream ()
{
    std::string const& s (m_ostream.str());
    if (! s.empty ())
    {
        if (s == "\n")
            m_sink.write (LEVEL, "");
        else
            m_sink.write (LEVEL, s);
    }
}

template <Journal::Severity LEVEL>
std::ostream&
Journal::TScopedStream<LEVEL>::operator<< (
    std::ostream& manip (std::ostream&)) const
{
    return m_ostream << manip;
}

template <Journal::Severity LEVEL>
template <typename T>
std::ostream&
Journal::TScopedStream<LEVEL>::operator<< (T const& t) const
{
    m_ostream << t;
    return m_ostream;
}

//------------------------------------------------------------------------------

template <Journal::Severity LEVEL>
Journal::TScopedStream<LEVEL>
Journal::TScopedStreamProxy<LEVEL>::operator<< (
    std::ostream& manip (std::ostream&)) const
{
    TScopedStream<LEVEL> ss (m_sink);
    ss << manip;
    return ss;
}

template <Journal::Severity LEVEL>
template <typename T>
Journal::TScopedStream<LEVEL>
Journal::TScopedStreamProxy<LEVEL>::operator<< (T const& t) const
{
    TScopedStream<LEVEL> ss (m_sink);
    ss << t;
    return ss;
}

} // beast

#endif
