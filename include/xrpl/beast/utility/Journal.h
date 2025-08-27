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

#include <xrpl/beast/utility/instrumentation.h>

#include <memory>
#include <source_location>
#include <sstream>

namespace beast {

/** A namespace for easy access to logging severity values. */
namespace severities {
/** Severity level / threshold of a Journal message. */
enum Severity {
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

std::string
to_string(Severity severity);
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

    class StructuredJournalImpl;

    class StructuredLogAttributes;

private:
    // Severity level / threshold of a Journal message.
    using Severity = severities::Severity;

    std::unique_ptr<StructuredLogAttributes> m_attributes;

    static std::unique_ptr<StructuredJournalImpl> m_structuredJournalImpl;

    // Invariant: m_sink always points to a valid Sink
    Sink* m_sink = nullptr;

public:
    //--------------------------------------------------------------------------

    static void
    enableStructuredJournal(std::unique_ptr<StructuredJournalImpl> impl)
    {
        m_structuredJournalImpl = std::move(impl);
    }

    static void
    disableStructuredJournal()
    {
        m_structuredJournalImpl = nullptr;
    }

    static bool
    isStructuredJournalEnabled()
    {
        return m_structuredJournalImpl != nullptr;
    }

    class StructuredJournalImpl
    {
    public:
        StructuredJournalImpl() = default;
        StructuredJournalImpl(StructuredJournalImpl const&) = default;
        virtual void
        initMessageContext(std::source_location location) = 0;
        virtual void
        flush(
            Sink* sink,
            severities::Severity level,
            std::string const& text,
            StructuredLogAttributes* attributes) = 0;
        virtual ~StructuredJournalImpl() = default;
    };

    class StructuredLogAttributes
    {
    public:
        StructuredLogAttributes() = default;
        StructuredLogAttributes(StructuredLogAttributes const&) = default;
        virtual void
        setModuleName(std::string const& name) = 0;
        virtual std::unique_ptr<StructuredLogAttributes>
        clone() const = 0;
        virtual void
        combine(std::unique_ptr<StructuredLogAttributes> const& attributes) = 0;
        virtual void
        combine(std::unique_ptr<StructuredLogAttributes>&& attributes) = 0;
        virtual ~StructuredLogAttributes() = default;
    };

    /** Abstraction for the underlying message destination. */
    class Sink
    {
    protected:
        Sink() = delete;
        explicit Sink(Sink const& sink) = default;
        Sink(Severity thresh, bool console);
        Sink&
        operator=(Sink const& lhs) = delete;

    public:
        virtual ~Sink() = 0;

        /** Returns `true` if text at the passed severity produces output. */
        virtual bool
        active(Severity level) const;

        /** Returns `true` if a message is also written to the Output Window
         * (MSVC). */
        virtual bool
        console() const;

        /** Set whether messages are also written to the Output Window (MSVC).
         */
        virtual void
        console(bool output);

        /** Returns the minimum severity level this sink will report. */
        virtual Severity
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
    static_assert(std::is_default_constructible<Sink>::value == false, "");
    static_assert(std::is_copy_constructible<Sink>::value == false, "");
    static_assert(std::is_move_constructible<Sink>::value == false, "");
    static_assert(std::is_copy_assignable<Sink>::value == false, "");
    static_assert(std::is_move_assignable<Sink>::value == false, "");
    static_assert(std::is_nothrow_destructible<Sink>::value == true, "");
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
        ScopedStream(ScopedStream const& other)
            : ScopedStream(
                  other.m_attributes ? other.m_attributes->clone() : nullptr,
                  other.m_sink,
                  other.m_level)
        {
        }

        ScopedStream(
            std::unique_ptr<StructuredLogAttributes> attributes,
            Sink& sink,
            Severity level);

        template <typename T>
        ScopedStream(
            std::unique_ptr<StructuredLogAttributes> attributes,
            Stream const& stream,
            T const& t);

        ScopedStream(
            std::unique_ptr<StructuredLogAttributes> attributes,
            Stream const& stream,
            std::ostream& manip(std::ostream&));

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
        std::unique_ptr<StructuredLogAttributes> m_attributes;
        Sink& m_sink;
        Severity const m_level;
        std::ostringstream mutable m_ostream;
    };

#ifndef __INTELLISENSE__
    static_assert(
        std::is_default_constructible<ScopedStream>::value == false,
        "");
    static_assert(std::is_copy_constructible<ScopedStream>::value == true, "");
    static_assert(std::is_move_constructible<ScopedStream>::value == true, "");
    static_assert(std::is_copy_assignable<ScopedStream>::value == false, "");
    static_assert(std::is_move_assignable<ScopedStream>::value == false, "");
    static_assert(
        std::is_nothrow_destructible<ScopedStream>::value == true,
        "");
#endif

    //--------------------------------------------------------------------------
public:
    /** Provide a light-weight way to check active() before string formatting */
    class Stream
    {
    public:
        /** Create a stream which produces no output. */
        explicit Stream()
            : m_sink(getNullSink()), m_level(severities::kDisabled)
        {
        }

        /** Create a stream that writes at the given level.

            Constructor is inlined so checking active() very inexpensive.
        */
        Stream(
            std::unique_ptr<StructuredLogAttributes> attributes,
            Sink& sink,
            Severity level)
            : m_attributes(std::move(attributes)), m_sink(sink), m_level(level)
        {
            XRPL_ASSERT(
                m_level < severities::kDisabled,
                "beast::Journal::Stream::Stream : maximum level");
        }

        /** Construct or copy another Stream. */
        Stream(Stream const& other)
            : Stream(
                  other.m_attributes ? other.m_attributes->clone() : nullptr,
                  other.m_sink,
                  other.m_level)
        {
        }

        Stream&
        operator=(Stream const& other) = delete;

        /** Returns the Sink that this Stream writes to. */
        Sink&
        sink() const
        {
            return m_sink;
        }

        /** Returns the Severity level of messages this Stream reports. */
        Severity
        level() const
        {
            return m_level;
        }

        /** Returns `true` if sink logs anything at this stream's level. */
        /** @{ */
        bool
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
        std::unique_ptr<StructuredLogAttributes> m_attributes;
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

    /** Journal has no default constructor. */
    Journal() = delete;

    Journal(Journal const& other) : Journal(other, nullptr)
    {
    }

    Journal(
        Journal const& other,
        std::unique_ptr<StructuredLogAttributes> attributes)
        : m_sink(other.m_sink)
    {
        if (attributes)
            m_attributes = std::move(attributes);
        if (other.m_attributes)
        {
            if (m_attributes)
                m_attributes->combine(other.m_attributes);
            else
                m_attributes = other.m_attributes->clone();
        }
    }

    Journal(
        Journal&& other,
        std::unique_ptr<StructuredLogAttributes> attributes = {}) noexcept
        : m_sink(other.m_sink)
    {
        if (attributes)
            m_attributes = std::move(attributes);
        if (other.m_attributes)
        {
            if (m_attributes)
                m_attributes->combine(std::move(other.m_attributes));
            else
                m_attributes = std::move(other.m_attributes);
        }
    }

    /** Create a journal that writes to the specified sink. */
    Journal(
        Sink& sink,
        std::string const& name = {},
        std::unique_ptr<StructuredLogAttributes> attributes = {})
        : m_sink(&sink)
    {
        if (attributes)
        {
            m_attributes = std::move(attributes);
            m_attributes->setModuleName(name);
        }
    }

    Journal&
    operator=(Journal const& other)
    {
        m_sink = other.m_sink;
        if (other.m_attributes)
            m_attributes = other.m_attributes->clone();
        return *this;
    }

    Journal&
    operator=(Journal&& other) noexcept
    {
        m_sink = other.m_sink;
        if (other.m_attributes)
            m_attributes = std::move(other.m_attributes);
        return *this;
    }

    /** Returns the Sink associated with this Journal. */
    Sink&
    sink() const
    {
        return *m_sink;
    }

    /** Returns a stream for this sink, with the specified severity level. */
    Stream
    stream(Severity level) const
    {
        return Stream(
            m_attributes ? m_attributes->clone() : nullptr, *m_sink, level);
    }

    /** Returns `true` if any message would be logged at this severity level.
        For a message to be logged, the severity must be at or above the
        sink's severity threshold.
    */
    bool
    active(Severity level) const
    {
        return m_sink->active(level);
    }

    /** Severity stream access functions. */
    /** @{ */
    Stream
    trace(std::source_location location = std::source_location::current()) const
    {
        if (m_structuredJournalImpl)
            m_structuredJournalImpl->initMessageContext(location);
        return {
            m_attributes ? m_attributes->clone() : nullptr,
            *m_sink,
            severities::kTrace};
    }

    Stream
    debug(std::source_location location = std::source_location::current()) const
    {
        if (m_structuredJournalImpl)
            m_structuredJournalImpl->initMessageContext(location);
        return {
            m_attributes ? m_attributes->clone() : nullptr,
            *m_sink,
            severities::kDebug};
    }

    Stream
    info(std::source_location location = std::source_location::current()) const
    {
        if (m_structuredJournalImpl)
            m_structuredJournalImpl->initMessageContext(location);
        return {
            m_attributes ? m_attributes->clone() : nullptr,
            *m_sink,
            severities::kInfo};
    }

    Stream
    warn(std::source_location location = std::source_location::current()) const
    {
        if (m_structuredJournalImpl)
            m_structuredJournalImpl->initMessageContext(location);
        return {
            m_attributes ? m_attributes->clone() : nullptr,
            *m_sink,
            severities::kWarning};
    }

    Stream
    error(std::source_location location = std::source_location::current()) const
    {
        if (m_structuredJournalImpl)
            m_structuredJournalImpl->initMessageContext(location);
        return {
            m_attributes ? m_attributes->clone() : nullptr,
            *m_sink,
            severities::kError};
    }

    Stream
    fatal(std::source_location location = std::source_location::current()) const
    {
        if (m_structuredJournalImpl)
            m_structuredJournalImpl->initMessageContext(location);
        return {
            m_attributes ? m_attributes->clone() : nullptr,
            *m_sink,
            severities::kFatal};
    }
    /** @} */
};

#ifndef __INTELLISENSE__
static_assert(std::is_default_constructible<Journal>::value == false, "");
static_assert(std::is_copy_constructible<Journal>::value == true, "");
static_assert(std::is_move_constructible<Journal>::value == true, "");
static_assert(std::is_copy_assignable<Journal>::value == true, "");
static_assert(std::is_move_assignable<Journal>::value == true, "");
static_assert(std::is_nothrow_destructible<Journal>::value == true, "");
#endif

//------------------------------------------------------------------------------

template <typename T>
Journal::ScopedStream::ScopedStream(
    std::unique_ptr<StructuredLogAttributes> attributes,
    Stream const& stream,
    T const& t)
    : ScopedStream(std::move(attributes), stream.sink(), stream.level())
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
    return {m_attributes ? m_attributes->clone() : nullptr, *this, t};
}

namespace detail {

template <class CharT, class Traits = std::char_traits<CharT>>
class logstream_buf : public std::basic_stringbuf<CharT, Traits>
{
    beast::Journal::Stream strm_;

    template <class T>
    void
    write(T const*) = delete;

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

    ~logstream_buf()
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
};

}  // namespace detail

template <class CharT, class Traits = std::char_traits<CharT>>
class basic_logstream : public std::basic_ostream<CharT, Traits>
{
    typedef CharT char_type;
    typedef Traits traits_type;
    typedef typename traits_type::int_type int_type;
    typedef typename traits_type::pos_type pos_type;
    typedef typename traits_type::off_type off_type;

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

#endif
