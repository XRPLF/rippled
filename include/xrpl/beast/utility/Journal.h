#pragma once

#include <xrpl/beast/utility/instrumentation.h>

#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>

namespace beast {

/** Severity level / threshold of a Journal message. */
enum class Severity : std::uint8_t {
    All = 0,

    Trace = All,
    Debug = 1,
    Info = 2,
    Warning = 3,
    Error = 4,
    Fatal = 5,

    Disabled = 6,
    None = Disabled
};

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
    // Invariant: sink_ always points to a valid Sink
    Sink* sink_;

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
        bool console_;
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
        ScopedStream(ScopedStream const& other) : ScopedStream(other.sink_, other.level_)
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
            return ostream_;
        }

        std::ostream&
        operator<<(std::ostream& manip(std::ostream&)) const;

        template <typename T>
        std::ostream&
        operator<<(T const& t) const;

    private:
        Sink& sink_;
        Severity const level_;
        std::ostringstream mutable ostream_;
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
        explicit Stream() : sink_(getNullSink()), level_(Severity::Disabled)
        {
        }

        /** Create a stream that writes at the given level.

            Constructor is inlined so checking active() very inexpensive.
        */
        Stream(Sink& sink, Severity level) : sink_(sink), level_(level)
        {
            XRPL_ASSERT(
                level_ < Severity::Disabled, "beast::Journal::Stream::Stream : maximum level");
        }

        /** Construct or copy another Stream. */
        Stream(Stream const& other) : Stream(other.sink_, other.level_)
        {
        }

        Stream&
        operator=(Stream const& other) = delete;

        /** Returns the Sink that this Stream writes to. */
        [[nodiscard]] Sink&
        sink() const
        {
            return sink_;
        }

        /** Returns the Severity level of messages this Stream reports. */
        [[nodiscard]] Severity
        level() const
        {
            return level_;
        }

        /** Returns `true` if sink logs anything at this stream's level. */
        /** @{ */
        [[nodiscard]] bool
        active() const
        {
            return sink_.active(level_);
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
        Sink& sink_;
        Severity level_;
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
    explicit Journal(Sink& sink) : sink_(&sink)
    {
    }

    /** Returns the Sink associated with this Journal. */
    [[nodiscard]] Sink&
    sink() const
    {
        return *sink_;
    }

    /** Returns a stream for this sink, with the specified severity level. */
    [[nodiscard]] Stream
    stream(Severity level) const
    {
        return Stream(*sink_, level);
    }

    /** Returns `true` if any message would be logged at this severity level.
        For a message to be logged, the severity must be at or above the
        sink's severity threshold.
    */
    [[nodiscard]] bool
    active(Severity level) const
    {
        return sink_->active(level);
    }

    /** Severity stream access functions. */
    /** @{ */
    [[nodiscard]] Stream
    trace() const
    {
        return {*sink_, Severity::Trace};
    }

    [[nodiscard]] Stream
    debug() const
    {
        return {*sink_, Severity::Debug};
    }

    [[nodiscard]] Stream
    info() const
    {
        return {*sink_, Severity::Info};
    }

    [[nodiscard]] Stream
    warn() const
    {
        return {*sink_, Severity::Warning};
    }

    [[nodiscard]] Stream
    error() const
    {
        return {*sink_, Severity::Error};
    }

    [[nodiscard]] Stream
    fatal() const
    {
        return {*sink_, Severity::Fatal};
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
    ostream_ << t;
}

template <typename T>
std::ostream&
Journal::ScopedStream::operator<<(T const& t) const
{
    ostream_ << t;
    return ostream_;
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
class LogStreamBuf : public std::basic_stringbuf<CharT, Traits>
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
    explicit LogStreamBuf(beast::Journal::Stream const& strm) : strm_(strm)
    {
    }

    ~LogStreamBuf() override
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
class BasicLogstream : public std::basic_ostream<CharT, Traits>
{
    using char_type = CharT;
    using traits_type = Traits;
    using int_type = traits_type::int_type;
    using pos_type = traits_type::pos_type;
    using off_type = traits_type::off_type;

    detail::LogStreamBuf<CharT, Traits> buf_;

public:
    explicit BasicLogstream(beast::Journal::Stream const& strm)
        : std::basic_ostream<CharT, Traits>(&buf_), buf_(strm)
    {
    }
};

using logstream = BasicLogstream<char>;
using logwstream = BasicLogstream<wchar_t>;

}  // namespace beast
