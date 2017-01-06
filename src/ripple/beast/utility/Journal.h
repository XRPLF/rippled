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

#include <cassert>
#include <sstream>

namespace beast {

/** A namespace for easy access to logging severity values. */
namespace severities
{
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
}

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
    class Sink;

private:
    // Severity level / threshold of a Journal message.
    using Severity = severities::Severity;

    Sink& m_sink;

public:
    //--------------------------------------------------------------------------

    /** Abstraction for the underlying message destination. */
    class Sink
    {
    protected:
        Sink () = delete;
        explicit Sink(Sink const& sink) = default;
        Sink (Severity thresh, bool console);
        Sink& operator= (Sink const& lhs) = delete;

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

#ifndef __INTELLISENSE__
static_assert(std::is_default_constructible<Sink>::value == false, "");
static_assert(std::is_copy_constructible<Sink>::value == false, "");
static_assert(std::is_move_constructible<Sink>::value == false, "");
static_assert(std::is_copy_assignable<Sink>::value == false, "");
static_assert(std::is_move_assignable<Sink>::value == false, "");
static_assert(std::is_nothrow_destructible<Sink>::value == true, "");
#endif

    /** Returns a Sink which does nothing. */
    static Sink& getNullSink ();

    //--------------------------------------------------------------------------

    class Stream;

private:
    /* Scoped ostream-based container for writing messages to a Journal. */
    class ScopedStream
    {
    public:
        ScopedStream (ScopedStream const& other)
            : ScopedStream (other.m_sink, other.m_level)
        { }

        ScopedStream (Sink& sink, Severity level);

        template <typename T>
        ScopedStream (Stream const& stream, T const& t);

        ScopedStream (
            Stream const& stream, std::ostream& manip (std::ostream&));

        ScopedStream& operator= (ScopedStream const&) = delete;

        ~ScopedStream ();

        std::ostringstream& ostream () const
        {
            return m_ostream;
        }

        std::ostream& operator<< (
            std::ostream& manip (std::ostream&)) const;

        template <typename T>
        std::ostream& operator<< (T const& t) const;

    private:
        Sink& m_sink;
        Severity const m_level;
        std::ostringstream mutable m_ostream;
    };

#ifndef __INTELLISENSE__
static_assert(std::is_default_constructible<ScopedStream>::value == false, "");
static_assert(std::is_copy_constructible<ScopedStream>::value == true, "");
static_assert(std::is_move_constructible<ScopedStream>::value == true, "");
static_assert(std::is_copy_assignable<ScopedStream>::value == false, "");
static_assert(std::is_move_assignable<ScopedStream>::value == false, "");
static_assert(std::is_nothrow_destructible<ScopedStream>::value == true, "");
#endif

    //--------------------------------------------------------------------------
public:
    /** Provide a light-weight way to check active() before string formatting */
    class Stream
    {
    public:
        /** Create a stream which produces no output. */
        explicit Stream ()
            : m_sink (getNullSink())
            , m_level (severities::kDisabled)
        { }

        /** Create a stream that writes at the given level.

            Constructor is inlined so checking active() very inexpensive.
        */
        Stream (Sink& sink, Severity level)
            : m_sink (sink)
            , m_level (level)
        {
            assert (m_level < severities::kDisabled);
        }

        /** Construct or copy another Stream. */
        Stream (Stream const& other)
            : Stream (other.m_sink, other.m_level)
        { }

        Stream& operator= (Stream const& other) = delete;

        /** Returns the Sink that this Stream writes to. */
        Sink& sink() const
        {
            return m_sink;
        }

        /** Returns the Severity level of messages this Stream reports. */
        Severity level() const
        {
            return m_level;
        }

        /** Returns `true` if sink logs anything at this stream's level. */
        /** @{ */
        bool active() const
        {
            return m_sink.active (m_level);
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
        ScopedStream operator<< (T const& t) const;
        /** @} */

    private:
        Sink& m_sink;
        Severity m_level;
    };

#ifndef __INTELLISENSE__
static_assert(std::is_default_constructible<Stream>::value == true, "");
static_assert(std::is_copy_constructible<Stream>::value == true, "");
static_assert(std::is_move_constructible<Stream>::value == true, "");
static_assert(std::is_copy_assignable<Stream>::value == false, "");
static_assert(std::is_move_assignable<Stream>::value == false, "");
static_assert(std::is_nothrow_destructible<Stream>::value == true, "");
#endif

    //--------------------------------------------------------------------------

    /** Create a journal that writes to the null sink. */
    Journal ()
    : Journal (getNullSink())
    { }

    /** Create a journal that writes to the specified sink. */
    explicit Journal (Sink& sink)
    : m_sink (sink)
    { }

    /** Create a journal from another journal. */
    Journal (Journal const& other)
    : Journal (other.m_sink)
    { }

    // Disallowed.
    Journal& operator= (Journal const& other) = delete;

    /** Destroy the journal. */
    ~Journal () = default;

    /** Returns the Sink associated with this Journal. */
    Sink& sink() const
    {
        return m_sink;
    }

    /** Returns a stream for this sink, with the specified severity level. */
    Stream stream (Severity level) const
    {
        return Stream (m_sink, level);
    }

    /** Returns `true` if any message would be logged at this severity level.
        For a message to be logged, the severity must be at or above the
        sink's severity threshold.
    */
    bool active (Severity level) const
    {
        return m_sink.active (level);
    }

    /** Severity stream access functions. */
    /** @{ */
    Stream trace() const
    {
        return { m_sink, severities::kTrace };
    }

    Stream debug() const
    {
        return { m_sink, severities::kDebug };
    }

    Stream info() const
    {
        return { m_sink, severities::kInfo };
    }

    Stream warn() const
    {
        return { m_sink, severities::kWarning };
    }

    Stream error() const
    {
        return { m_sink, severities::kError };
    }

    Stream fatal() const
    {
        return { m_sink, severities::kFatal };
    }
    /** @} */
};

#ifndef __INTELLISENSE__
static_assert(std::is_default_constructible<Journal>::value == true, "");
static_assert(std::is_copy_constructible<Journal>::value == true, "");
static_assert(std::is_move_constructible<Journal>::value == true, "");
static_assert(std::is_copy_assignable<Journal>::value == false, "");
static_assert(std::is_move_assignable<Journal>::value == false, "");
static_assert(std::is_nothrow_destructible<Journal>::value == true, "");
#endif

//------------------------------------------------------------------------------

template <typename T>
Journal::ScopedStream::ScopedStream (Journal::Stream const& stream, T const& t)
   : ScopedStream (stream.sink(), stream.level())
{
    m_ostream << t;
}

template <typename T>
std::ostream&
Journal::ScopedStream::operator<< (T const& t) const
{
    m_ostream << t;
    return m_ostream;
}

//------------------------------------------------------------------------------

template <typename T>
Journal::ScopedStream
Journal::Stream::operator<< (T const& t) const
{
    return ScopedStream (*this, t);
}

} // beast

#endif
