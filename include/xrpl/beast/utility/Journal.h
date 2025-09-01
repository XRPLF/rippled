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

#include <boost/asio/execution/allocator.hpp>
#include <boost/coroutine/attributes.hpp>
#include <boost/system/result.hpp>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <deque>
#include <optional>
#include <source_location>
#include <sstream>
#include <utility>

namespace ripple::log {
template <typename T>
class LogParameter
{
public:
    template <typename TArg>
    LogParameter(char const* name, TArg&& value)
        : name_(name), value_(std::forward<TArg>(value))
    {
    }

private:
    char const* name_;
    T value_;

    template <typename U>
    friend std::ostream&
    operator<<(std::ostream& os, LogParameter<U> const&);
};

template <typename T>
class LogField
{
public:
    template <typename TArg>
    LogField(char const* name, TArg&& value)
        : name_(name), value_(std::forward<TArg>(value))
    {
    }

private:
    char const* name_;
    T value_;

    template <typename U>
    friend std::ostream&
    operator<<(std::ostream& os, LogField<U> const&);
};

template <typename T>
std::ostream&
operator<<(std::ostream& os, LogField<T> const& param);

template <typename T>
std::ostream&
operator<<(std::ostream& os, LogParameter<T> const& param);
}  // namespace ripple::log

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
    template <typename T>
    friend std::ostream&
    ripple::log::operator<<(
        std::ostream& os,
        ripple::log::LogField<T> const& param);

    template <typename T>
    friend std::ostream&
    ripple::log::operator<<(
        std::ostream& os,
        ripple::log::LogParameter<T> const& param);

    class Sink;

    class JsonLogAttributes
    {
    public:
        using AttributeFields = rapidjson::Value;

        JsonLogAttributes();
        JsonLogAttributes(JsonLogAttributes const& other);

        JsonLogAttributes&
        operator=(JsonLogAttributes const& other);

        void
        setModuleName(std::string const& name);

        void
        combine(AttributeFields const& from);

        AttributeFields&
        contextValues()
        {
            return contextValues_;
        }

        [[nodiscard]] AttributeFields const&
        contextValues() const
        {
            return contextValues_;
        }

        rapidjson::MemoryPoolAllocator<>&
        allocator()
        {
            return allocator_;
        }

    private:
        AttributeFields contextValues_;
        rapidjson::MemoryPoolAllocator<> allocator_;

        friend class Journal;
    };

    class JsonLogContext
    {
        rapidjson::StringBuffer buffer_;
        rapidjson::Writer<rapidjson::StringBuffer> messageParamsWriter_;
        std::optional<std::string> journalAttributesJson_;

    public:
        JsonLogContext()
            : messageParamsWriter_(buffer_)
        {

        }

        rapidjson::Writer<rapidjson::StringBuffer>&
        writer()
        {
            return messageParamsWriter_;
        }

        char const*
        messageParams()
        {
            return buffer_.GetString();
        }

        std::optional<std::string>&
        journalAttributesJson()
        {
            return journalAttributesJson_;
        }

        void
        reset(
            std::source_location location,
            severities::Severity severity,
            std::optional<std::string> const& journalAttributesJson) noexcept;
    };

private:
    // Severity level / threshold of a Journal message.
    using Severity = severities::Severity;

    std::optional<JsonLogAttributes> m_attributes;
    std::optional<std::string> m_attributesJson;
    static std::optional<JsonLogAttributes> globalLogAttributes_;
    static std::optional<std::string> globalLogAttributesJson_;
    static std::mutex globalLogAttributesMutex_;
    static bool m_jsonLogsEnabled;

    static thread_local JsonLogContext currentJsonLogContext_;

    // Invariant: m_sink always points to a valid Sink
    Sink* m_sink = nullptr;

    void
    initMessageContext(
        std::source_location location,
        severities::Severity severity) const;

    static std::string
    formatLog(std::string&& message);

    void
    rebuildAttributeJson();

public:
    //--------------------------------------------------------------------------

    static void
    enableStructuredJournal();

    static void
    disableStructuredJournal();

    static bool
    isStructuredJournalEnabled();

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
        write(Severity level, std::string&& text) = 0;

        /** Bypass filter and write text to the sink at the specified severity.
         * Always write the message, but maintain the same formatting as if
         * it passed through a level filter.
         *
         * @param level Level to display in message.
         * @param text Text to write to sink.
         */
        virtual void
        writeAlways(Severity level, std::string&& text) = 0;

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
            : ScopedStream(other.m_sink, other.m_level)
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
        Stream(Sink& sink, Severity level) : m_sink(sink), m_level(level)
        {
            XRPL_ASSERT(
                m_level < severities::kDisabled,
                "beast::Journal::Stream::Stream : maximum level");
        }

        /** Construct or copy another Stream. */
        Stream(Stream const& other) : Stream(other.m_sink, other.m_level)
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

    template <typename TAttributesFactory>
    Journal(
        Journal const& other,
        TAttributesFactory&& attributesFactory = nullptr)
        : m_attributes(other.m_attributes)
        , m_sink(other.m_sink)
        , m_attributesJson(other.m_attributesJson)
    {
/*
        if constexpr (!std::is_same_v<std::decay_t<TAttributesFactory>, std::nullptr_t>)
        {
            if (attributes.has_value())
            {
                if (m_attributes)
                    m_attributes->combine(attributes->contextValues_);
                else
                    m_attributes = std::move(attributes);
            }
            rebuildAttributeJson();
        }
*/
    }

    /** Create a journal that writes to the specified sink. */
    explicit Journal(
        Sink& sink,
        std::string const& name = {},
        std::optional<JsonLogAttributes> attributes = std::nullopt)
        : m_sink(&sink)
    {
        if (attributes)
        {
            m_attributes = std::move(attributes);
            m_attributes->setModuleName(name);
        }
        rebuildAttributeJson();
    }

    Journal&
    operator=(Journal const& other)
    {
        if (&other == this)
            return *this;

        m_sink = other.m_sink;
        m_attributes = other.m_attributes;
        rebuildAttributeJson();
        return *this;
    }

    Journal&
    operator=(Journal&& other) noexcept
    {
        m_sink = other.m_sink;
        m_attributes = std::move(other.m_attributes);
        rebuildAttributeJson();
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
    stream(Severity level, std::source_location location = std::source_location::current()) const
    {
        if (m_jsonLogsEnabled)
            initMessageContext(location, level);
        return Stream(*m_sink, level);
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
        if (m_jsonLogsEnabled)
            initMessageContext(location, severities::kTrace);
        return {*m_sink, severities::kTrace};
    }

    Stream
    debug(std::source_location location = std::source_location::current()) const
    {
        if (m_jsonLogsEnabled)
            initMessageContext(location, severities::kDebug);
        return {*m_sink, severities::kDebug};
    }

    Stream
    info(std::source_location location = std::source_location::current()) const
    {
        if (m_jsonLogsEnabled)
            initMessageContext(location, severities::kInfo);
        return {*m_sink, severities::kInfo};
    }

    Stream
    warn(std::source_location location = std::source_location::current()) const
    {
        char const* a = "a";
        rapidjson::Value v{a, 1};
        if (m_jsonLogsEnabled)
            initMessageContext(location, severities::kWarning);
        return {*m_sink, severities::kWarning};
    }

    Stream
    error(std::source_location location = std::source_location::current()) const
    {
        if (m_jsonLogsEnabled)
            initMessageContext(location, severities::kError);
        return {*m_sink, severities::kError};
    }

    Stream
    fatal(std::source_location location = std::source_location::current()) const
    {
        if (m_jsonLogsEnabled)
            initMessageContext(location, severities::kFatal);
        return {*m_sink, severities::kFatal};
    }
    /** @} */

    static void
    resetGlobalAttributes()
    {
        std::lock_guard lock(globalLogAttributesMutex_);
        globalLogAttributes_ = std::nullopt;
        globalLogAttributesJson_ = std::nullopt;
    }

    static void
    addGlobalAttributes(JsonLogAttributes globalLogAttributes);
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
Journal::ScopedStream::ScopedStream(Stream const& stream, T const& t)
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
    return {*this, t};
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

namespace ripple::log {

namespace detail {
template <typename T, typename OutputStream>
void
setJsonValue(
    rapidjson::Writer<OutputStream>& writer,
    char const* name,
    T&& value,
    std::ostream* outStream)
{
    using ValueType = std::decay_t<T>;
    writer.Key(name);
    if constexpr (std::is_integral_v<ValueType>)
    {
        if constexpr (std::is_signed_v<ValueType>)
        {
            writer.Int64(value);
        }
        else
        {
            writer.Uint64(value);
        }
        if (outStream)
        {
            (*outStream) << value;
        }
    }
    else if constexpr (std::is_floating_point_v<ValueType>)
    {
        writer.Double(value);

        if (outStream)
        {
            (*outStream) << value;
        }
    }
    else if constexpr (std::is_same_v<ValueType, bool>)
    {
        writer.Bool(value);
        if (outStream)
        {
            (*outStream) << value;
        }
    }
    else if constexpr (std::is_same_v<ValueType, char const*> || std::is_same_v<ValueType, char*>)
    {
        writer.String(value);
        if (outStream)
        {
            (*outStream) << value;
        }
    }
    else if constexpr (std::is_same_v<ValueType, std::string>)
    {
        writer.String(value.c_str(), value.length());
        if (outStream)
        {
            (*outStream) << value;
        }
    }
    else
    {
        std::ostringstream oss;
        oss << value;

        writer.String(oss.str().c_str(), oss.str().length());

        if (outStream)
        {
            (*outStream) << oss.str();
        }
    }
}
}  // namespace detail

template <typename T>
std::ostream&
operator<<(std::ostream& os, LogParameter<T> const& param)
{
    if (!beast::Journal::m_jsonLogsEnabled)
        return os;
    detail::setJsonValue(
        beast::Journal::currentJsonLogContext_.writer(),
        param.name_,
        param.value_,
        &os);
    return os;
}

template <typename T>
std::ostream&
operator<<(std::ostream& os, LogField<T> const& param)
{
    if (!beast::Journal::m_jsonLogsEnabled)
        return os;
    detail::setJsonValue(
        beast::Journal::currentJsonLogContext_.writer(),
        param.name_,
        param.value_,
        nullptr);
    return os;
}

template <typename T>
LogParameter<T>
param(char const* name, T&& value)
{
    return LogParameter<T>{name, std::forward<T>(value)};
}

template <typename T>
LogField<T>
field(char const* name, T&& value)
{
    return LogField<T>{name, std::forward<T>(value)};
}

template <typename... Pair>
[[nodiscard]] auto
attributes(Pair&&... pairs)
{
    return [&](rapidjson::Writer<rapidjson::Writer<char>>& writer) {
        (detail::setJsonValue(
             writer,
             pairs.first,
             pairs.second,
             nullptr),
         ...);
    };
}

template <typename T>
[[nodiscard]] std::pair<char const*, std::decay_t<T>>
attr(char const* name, T&& value)
{
    return std::make_pair(name, std::forward<T>(value));
}

}  // namespace ripple::log

#endif
