#pragma once

#include <xrpl/beast/utility/instrumentation.h>

#include <sstream>

namespace beast {

/** A namespace for easy access to logging severity values. */
namespace severities {
/** Severity level / threshold of a Journal message. */
enum Severity {
    kAll = 0,

    kTrace = kAll,
    kDebug = 1,
    kInfo = 2,
    kWarning = 3,
    kError = 4,
    kFatal = 5,

    kDisabled = 6,
    kNone = kDisabled
};
}  // namespace severities

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

    // Invariant: m_sink always points to a valid Sink
    Sink* m_sink;

public:
    //--------------------------------------------------------------------------

    /** Abstraction for the underlying message destination. */
    class Sink
    {
    protected:
        explicit Sink(Sink const& sink) = default;
        Sink(Severity thresh, bool console);

    public:
        virtual ~Sink() = 0;

        Sink() = delete;
        Sink&
        operator=(Sink const& lhs) = delete;

        /** Returns `true` if text at the passed severity produces output. */
        [[nodiscard]] virtual bool
        active(Severity level) const;

        /** Returns `true` if a message is also written to the Output Window
         * (MSVC). */
        [[nodiscard]] virtual bool
        console() const;

        /** Set whether messages are also written to the Output Window (MSVC).
         */
        virtual void
        console(bool output);

        /** Returns the minimum severity level this sink will report. */
        [[nodiscard]] virtual Severity
        threshold() const;

        /** Set the minimum severity this sink will report. */
        virtual void
        threshold(Severity thresh);

        /** Write text to the sink at the specified severity.
            A conforming implementation will not write the text if the passed
            level is below the current threshold().
        */
        virtual void
        write(Severity level, std::string const& text) = 0;

        /** Bypass filter and write text to the sink at the specified severity.
         * Always write the message, but maintain the same formatting as if
         * it passed through a level filter.
         *
         * @param level Level to display in message.
         * @param text Text to write to sink.
         */
        virtual void
        writeAlways(Severity level, std::string const& text) = 0;

    private:
        Severity thresh_;
        bool m_console;
    };

#ifndef __INTELLISENSE__
    static_assert(!std::is_default_constructible_v<Sink>, "");
    static_assert(!std::is_copy_constructible_v<Sink>, "");
    static_assert(!std::is_move_constructible_v<Sink>, "");
    static_assert(!std::is_copy_assignable_v<Sink>, "");
    static_assert(!std::is_move_assignable_v<Sink>, "");
    static_assert(std::is_nothrow_destructible_v<Sink>, "");
#endif

    /** Returns a Sink which does nothing. */
    static Sink&
    getNullSink();

    //--------------------------------------------------------------------------

    class Stream;

    /* Scoped ostream-based container for writing messages to a Journal. */
    class ScopedStream
    {
    public:
        ScopedStream(ScopedStream const& other) : ScopedStream(other.m_sink, other.m_level)
        {
        }

        ScopedStream(Sink& sink, Severity level);

        template <typename T>
        ScopedStream(Stream const& stream, T const& t);

        ScopedStream(Stream const& stream, std::ostream& manip(std::ostream&));

        ScopedStream&
        operator=(ScopedStream const&) = delete;

        ~ScopedStream();

        std::ostringstream&
        ostream() const
        {
            return m_ostream;
        }

        std::ostream&
        operator<<(std::ostream& manip(std::ostream&)) const;

        template <typename T>
        std::ostream&
        operator<<(T const& t) const;

    private:
        Sink& m_sink;
        Severity const m_level;
        std::ostringstream mutable m_ostream;
    };

#ifndef __INTELLISENSE__
    static_assert(!std::is_default_constructible_v<ScopedStream>, "");
    static_assert(std::is_copy_constructible_v<ScopedStream>, "");
    static_assert(std::is_move_constructible_v<ScopedStream>, "");
    static_assert(!std::is_copy_assignable_v<ScopedStream>, "");
    static_assert(!std::is_move_assignable_v<ScopedStream>, "");
    static_assert(std::is_nothrow_destructible_v<ScopedStream>, "");
#endif

    //--------------------------------------------------------------------------
public:
    /** Provide a light-weight way to check active() before string formatting */
    class Stream
    {
    public:
        /** Create a stream which produces no output. */
        explicit Stream() : m_sink(getNullSink()), m_level(severities::kDisabled)
        {
        }

        /** Create a stream that writes at the given level.

            Constructor is inlined so checking active() very inexpensive.
        */
        Stream(Sink& sink, Severity level) : m_sink(sink), m_level(level)
        {
            XRPL_ASSERT(
                m_level < severities::kDisabled, "beast::Journal::Stream::Stream : maximum level");
        }

        /** Construct or copy another Stream. */
        Stream(Stream const& other) : Stream(other.m_sink, other.m_level)
        {
        }

        Stream&
        operator=(Stream const& other) = delete;

        /** Returns the Sink that this Stream writes to. */
        [[nodiscard]] Sink&
        sink() const
        {
            return m_sink;
        }

        /** Returns the Severity level of messages this Stream reports. */
        [[nodiscard]] Severity
        level() const
        {
            return m_level;
        }

        /** Returns `true` if sink logs anything at this stream's level. */
        /** @{ */
        [[nodiscard]] bool
        active() const
        {
            return m_sink.active(m_level);
        }

        explicit
        operator bool() const
        {
            return active();
        }
        /** @} */

        /** Output stream support. */
        /** @{ */
        ScopedStream
        operator<<(std::ostream& manip(std::ostream&)) const;

        template <typename T>
        ScopedStream
        operator<<(T const& t) const;
        /** @} */

    private:
        Sink& m_sink;
        Severity m_level;
    };

#ifndef __INTELLISENSE__
    static_assert(std::is_default_constructible_v<Stream>, "");
    static_assert(std::is_copy_constructible_v<Stream>, "");
    static_assert(std::is_move_constructible_v<Stream>, "");
    static_assert(!std::is_copy_assignable_v<Stream>, "");
    static_assert(!std::is_move_assignable_v<Stream>, "");
    static_assert(std::is_nothrow_destructible_v<Stream>, "");
#endif

    //--------------------------------------------------------------------------

    /** Journal has no default constructor. */
    Journal() = delete;

    /** Create a journal that writes to the specified sink. */
    explicit Journal(Sink& sink) : m_sink(&sink)
    {
    }

    /** Returns the Sink associated with this Journal. */
    [[nodiscard]] Sink&
    sink() const
    {
        return *m_sink;
    }

    /** Returns a stream for this sink, with the specified severity level. */
    [[nodiscard]] Stream
    stream(Severity level) const
    {
        return Stream(*m_sink, level);
    }

    /** Returns `true` if any message would be logged at this severity level.
        For a message to be logged, the severity must be at or above the
        sink's severity threshold.
    */
    [[nodiscard]] bool
    active(Severity level) const
    {
        return m_sink->active(level);
    }

    /** Severity stream access functions. */
    /** @{ */
    [[nodiscard]] Stream
    trace() const
    {
        return {*m_sink, severities::kTrace};
    }

    [[nodiscard]] Stream
    debug() const
    {
        return {*m_sink, severities::kDebug};
    }

    [[nodiscard]] Stream
    info() const
    {
        return {*m_sink, severities::kInfo};
    }

    [[nodiscard]] Stream
    warn() const
    {
        return {*m_sink, severities::kWarning};
    }

    [[nodiscard]] Stream
    error() const
    {
        return {*m_sink, severities::kError};
    }

    [[nodiscard]] Stream
    fatal() const
    {
        return {*m_sink, severities::kFatal};
    }
    /** @} */
};

#ifndef __INTELLISENSE__
static_assert(!std::is_default_constructible_v<Journal>, "");
static_assert(std::is_copy_constructible_v<Journal>, "");
static_assert(std::is_move_constructible_v<Journal>, "");
static_assert(std::is_copy_assignable_v<Journal>, "");
static_assert(std::is_move_assignable_v<Journal>, "");
static_assert(std::is_nothrow_destructible_v<Journal>, "");
#endif

//------------------------------------------------------------------------------

template <typename T>
Journal::ScopedStream::ScopedStream(Journal::Stream const& stream, T const& t)
    : ScopedStream(stream.sink(), stream.level())
{
    m_ostream << t;
}

template <typename T>
std::ostream&
Journal::ScopedStream::operator<<(T const& t) const
{
    m_ostream << t;
    return m_ostream;
}

//------------------------------------------------------------------------------

template <typename T>
Journal::ScopedStream
Journal::Stream::operator<<(T const& t) const
{
    return ScopedStream(*this, t);
}

namespace detail {

template <class CharT, class Traits = std::char_traits<CharT>>
class logstream_buf : public std::basic_stringbuf<CharT, Traits>
{
    beast::Journal::Stream strm_;

    void
    write(char const* s)
    {
        if (strm_)
            strm_ << s;
    }

    void
    write(wchar_t const* s)
    {
        if (strm_)
            strm_ << s;
    }

public:
    explicit logstream_buf(beast::Journal::Stream const& strm) : strm_(strm)
    {
    }

    ~logstream_buf() override
    {
        sync();
    }

    int
    sync() override
    {
        write(this->str().c_str());
        this->str("");
        return 0;
    }

    template <class T>
    void
    write(T const*) = delete;
};

}  // namespace detail

template <class CharT, class Traits = std::char_traits<CharT>>
class basic_logstream : public std::basic_ostream<CharT, Traits>
{
    using char_type = CharT;
    using traits_type = Traits;
    using int_type = typename traits_type::int_type;
    using pos_type = typename traits_type::pos_type;
    using off_type = typename traits_type::off_type;

    detail::logstream_buf<CharT, Traits> buf_;

public:
    explicit basic_logstream(beast::Journal::Stream const& strm)
        : std::basic_ostream<CharT, Traits>(&buf_), buf_(strm)
    {
    }
};

using logstream = basic_logstream<char>;
using logwstream = basic_logstream<wchar_t>;

}  // namespace beast
