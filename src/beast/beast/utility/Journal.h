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
    /** Severity level of the message. */
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

    /** Scoped ostream-based container for writing messages to a Journal. */
    class ScopedStream
    {
    public:
        explicit ScopedStream (Stream const& stream);
        ScopedStream (ScopedStream const& other);

        template <typename T>
        ScopedStream (Stream const& stream, T const& t)
            : m_sink (stream.sink())
            , m_level (stream.severity())
        {
            m_ostream << t;
        }

        ScopedStream (Stream const& stream,
            std::ostream& manip (std::ostream&));

        ~ScopedStream ();

        std::ostringstream& ostream () const;

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

        ScopedStream& operator= (ScopedStream const&); // disallowed

        Sink& m_sink;
        Severity const m_level; // cached from Stream for call to m_sink.write
        std::ostringstream mutable m_ostream;
    };

    //--------------------------------------------------------------------------

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
};

}

#endif
