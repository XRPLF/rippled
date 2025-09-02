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

#include <charconv>
#include <source_location>
#include <sstream>
#include <string>
#include <string_view>
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

// Forward declaration for use in interfaces
namespace detail {
class SimpleJsonWriter;
}

// Type alias for journal attribute factory functions
using JournalAttributesFactory = void(detail::SimpleJsonWriter&);

namespace detail {

class SimpleJsonWriter
{
public:
    explicit SimpleJsonWriter(std::string& stream) : stream_(stream)
    {
    }

    void
    startObject() const
    {
        stream_.push_back('{');
    }
    void
    endObject() const
    {
        using namespace std::string_view_literals;
        stream_.pop_back();
        stream_.append("},"sv);
    }
    void
    writeKey(std::string_view key) const
    {
        writeString(key);
        stream_.back() = ':';
    }
    void
    startArray() const
    {
        stream_.push_back('[');
    }
    void
    endArray() const
    {
        using namespace std::string_view_literals;
        stream_.pop_back();
        stream_.append("],"sv);
    }
    void
    writeString(std::string_view str) const
    {
        using namespace std::string_view_literals;
        stream_.push_back('"');
        escape(str, stream_);
        stream_.append("\","sv);
    }
    std::string_view
    writeInt(std::int8_t val) const
    {
        return pushNumber(val, stream_);
    }
    std::string_view
    writeInt(std::int16_t val) const
    {
        return pushNumber(val, stream_);
    }
    std::string_view
    writeInt(std::int32_t val) const
    {
        return pushNumber(val, stream_);
    }
    std::string_view
    writeInt(std::int64_t val) const
    {
        return pushNumber(val, stream_);
    }
    std::string_view
    writeUInt(std::size_t val) const
    {
        return pushNumber(val, stream_);
    }
    std::string_view
    writeUInt(std::uint8_t val) const
    {
        return pushNumber(val, stream_);
    }
    std::string_view
    writeUInt(std::uint16_t val) const
    {
        return pushNumber(val, stream_);
    }
    std::string_view
    writeUInt(std::uint32_t val) const
    {
        return pushNumber(val, stream_);
    }
    std::string_view
    writeUInt(std::uint64_t val) const
    {
        return pushNumber(val, stream_);
    }
    std::string_view
    writeDouble(double val) const
    {
        return pushNumber(val, stream_);
    }
    std::string_view
    writeBool(bool val) const
    {
        using namespace std::string_view_literals;
        auto str = val ? "true,"sv : "false,"sv;
        stream_.append(str);
        return str;
    }
    void
    writeNull() const
    {
        using namespace std::string_view_literals;
        stream_.append("null,"sv);
    }
    void
    writeRaw(std::string_view str) const
    {
        stream_.append(str);
    }

    [[nodiscard]] std::string_view
    finish()
    {
        return std::string_view{stream_.c_str(), stream_.size() - 1};
    }

private:
    template <typename T>
    static std::string_view
    pushNumber(T val, std::string& stream)
    {
        thread_local char buffer[128];
        auto result = std::to_chars(std::begin(buffer), std::end(buffer), val);
        auto ptr = result.ptr;
        *ptr = ',';
        auto len = ptr - std::begin(buffer);
        stream.append(buffer, len + 1);
        return {buffer, static_cast<size_t>(len)};
    }

    static void
    escape(std::string_view str, std::string& os)
    {
        static constexpr char HEX[] = "0123456789ABCDEF";

        char const* p = str.data();
        char const* end = p + str.size();
        char const* chunk = p;

        while (p < end)
        {
            auto c = static_cast<unsigned char>(*p);

            // JSON requires escaping for <0x20 and the two specials below.
            bool needsEscape = (c < 0x20) || (c == '"') || (c == '\\');

            if (!needsEscape)
            {
                ++p;
                continue;
            }

            // Flush the preceding safe run in one go.
            if (chunk != p)
                os.append(chunk, p - chunk);

            switch (c)
            {
                case '"':
                    os.append("\\\"", 2);
                    break;
                case '\\':
                    os.append("\\\\", 2);
                    break;
                case '\b':
                    os.append("\\b", 2);
                    break;
                case '\f':
                    os.append("\\f", 2);
                    break;
                case '\n':
                    os.append("\\n", 2);
                    break;
                case '\r':
                    os.append("\\r", 2);
                    break;
                case '\t':
                    os.append("\\t", 2);
                    break;
                default: {
                    // Other C0 controls -> \u00XX (JSON compliant)
                    char buf[6]{
                        '\\', 'u', '0', '0', HEX[(c >> 4) & 0xF], HEX[c & 0xF]};
                    os.append(buf, 6);
                    break;
                }
            }

            ++p;
            chunk = p;
        }

        // Flush trailing safe run
        if (chunk != p)
            os.append(chunk, p - chunk);
    }

    std::string& stream_;
};

}  // namespace detail

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

    class JsonLogContext
    {
        std::string buffer_;
        detail::SimpleJsonWriter messageParamsWriter_;

    public:
        JsonLogContext() : messageParamsWriter_(buffer_)
        {
            buffer_.reserve(1024 * 5);
        }

        detail::SimpleJsonWriter&
        writer()
        {
            return messageParamsWriter_;
        }

        void
        reset(
            std::source_location location,
            severities::Severity severity,
            std::string_view journalAttributesJson) noexcept;
    };

private:
    // Severity level / threshold of a Journal message.
    using Severity = severities::Severity;

    std::string m_name;
    std::string m_attributesJson;
    static std::string globalLogAttributesJson_;
    static std::mutex globalLogAttributesMutex_;
    static bool m_jsonLogsEnabled;

    static thread_local JsonLogContext currentJsonLogContext_;

    // Invariant: m_sink always points to a valid Sink
    Sink* m_sink = nullptr;

    void
    initMessageContext(
        std::source_location location,
        severities::Severity severity) const;

    static std::string_view
    formatLog(std::string&& message);

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

    Journal(Journal const& other)
        : m_name(other.m_name)
        , m_attributesJson(other.m_attributesJson)
        , m_sink(other.m_sink)
    {
    }

    template <typename TAttributesFactory>
    Journal(Journal const& other, TAttributesFactory&& attributesFactory)
        : m_name(other.m_name), m_sink(other.m_sink)
    {
        std::string stream{other.m_attributesJson};
        detail::SimpleJsonWriter writer{stream};
        if (other.m_attributesJson.empty())
        {
            writer.startObject();
        }
        attributesFactory(writer);
        m_attributesJson = std::move(stream);
    }

    /** Create a journal that writes to the specified sink. */
    explicit Journal(Sink& sink, std::string const& name = {})
        : m_name(name), m_sink(&sink)
    {
    }

    /** Create a journal that writes to the specified sink. */
    template <typename TAttributesFactory>
    explicit Journal(
        Sink& sink,
        std::string const& name,
        TAttributesFactory&& attributesFactory)
        : m_name(name), m_sink(&sink)
    {
        std::string stream;
        detail::SimpleJsonWriter writer{stream};
        writer.startObject();
        attributesFactory(writer);
        m_attributesJson = std::move(stream);
    }

    Journal&
    operator=(Journal const& other)
    {
        if (&other == this)
            return *this;

        m_sink = other.m_sink;
        m_name = other.m_name;
        m_attributesJson = other.m_attributesJson;
        return *this;
    }

    Journal&
    operator=(Journal&& other) noexcept
    {
        m_sink = other.m_sink;
        m_name = std::move(other.m_name);
        m_attributesJson = std::move(other.m_attributesJson);
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
    stream(
        Severity level,
        std::source_location location = std::source_location::current()) const
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
        globalLogAttributesJson_.clear();
    }

    template <typename TAttributesFactory>
    static void
    addGlobalAttributes(TAttributesFactory&& factory)
    {
        std::lock_guard lock(globalLogAttributesMutex_);

        auto isEmpty = globalLogAttributesJson_.empty();
        std::string stream{std::move(globalLogAttributesJson_)};
        detail::SimpleJsonWriter writer{stream};
        if (isEmpty)
        {
            writer.startObject();
        }
        factory(writer);
        globalLogAttributesJson_ = std::move(stream);
    }
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

template <typename T>
concept ToCharsFormattable = requires(T val) {
    {
        to_chars(std::declval<char*>(), std::declval<char*>(), val)
    } -> std::convertible_to<std::to_chars_result>;
};

template <typename T>
void
setJsonValue(
    beast::detail::SimpleJsonWriter& writer,
    char const* name,
    T&& value,
    std::ostream* outStream)
{
    using ValueType = std::decay_t<T>;
    writer.writeKey(name);
    if constexpr (std::is_same_v<ValueType, bool>)
    {
        auto sv = writer.writeBool(value);
        if (outStream)
        {
            outStream->write(sv.data(), sv.size());
        }
    }
    else if constexpr (std::is_integral_v<ValueType>)
    {
        std::string_view sv;
        if constexpr (std::is_signed_v<ValueType>)
        {
            sv = writer.writeInt(value);
        }
        else
        {
            sv = writer.writeUInt(value);
        }
        if (outStream)
        {
            outStream->write(sv.data(), sv.size());
        }
    }
    else if constexpr (std::is_floating_point_v<ValueType>)
    {
        auto sv = writer.writeDouble(value);

        if (outStream)
        {
            outStream->write(sv.data(), sv.size());
        }
    }
    else if constexpr (
        std::is_same_v<ValueType, char const*> ||
        std::is_same_v<ValueType, char*>)
    {
        writer.writeString(value);
        if (outStream)
        {
            outStream->write(value, std::strlen(value));
        }
    }
    else if constexpr (std::is_same_v<ValueType, std::string>)
    {
        writer.writeString(value);
        if (outStream)
        {
            outStream->write(value.c_str(), value.length());
        }
    }
    else
    {
        if constexpr (ToCharsFormattable<ValueType>)
        {
            char buffer[1024];
            std::to_chars_result result =
                to_chars(std::begin(buffer), std::end(buffer), value);
            if (result.ec == std::errc{})
            {
                std::string_view sv{std::begin(buffer), result.ptr};
                writer.writeString(sv);
                if (outStream)
                {
                    outStream->write(sv.data(), sv.size());
                }
                return;
            }
        }

        std::ostringstream oss;
        oss.imbue(std::locale::classic());
        oss << value;

        auto str = oss.str();

        writer.writeString(str);

        if (outStream)
        {
            outStream->write(
                str.c_str(), static_cast<std::streamsize>(str.size()));
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
    return [&](beast::detail::SimpleJsonWriter& writer) {
        (detail::setJsonValue(writer, pairs.first, pairs.second, nullptr), ...);
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
