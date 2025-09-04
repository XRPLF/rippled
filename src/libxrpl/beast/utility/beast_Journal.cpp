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

#include <xrpl/beast/utility/Journal.h>

#include <ios>
#include <ostream>
#include <ranges>
#include <string>
#include <thread>
#include <chrono>

namespace beast {

namespace {

// Fast timestamp to ISO string conversion
// Returns string like "2024-01-15T10:30:45.123Z"
std::string_view
fastTimestampToString(std::int64_t milliseconds_since_epoch)
{
    thread_local char buffer[64]; // "2024-01-15T10:30:45.123Z"
    
    // Precomputed lookup table for 2-digit numbers 00-99
    static constexpr char digits[200] = {
        '0','0','0','1','0','2','0','3','0','4','0','5','0','6','0','7','0','8','0','9',
        '1','0','1','1','1','2','1','3','1','4','1','5','1','6','1','7','1','8','1','9',
        '2','0','2','1','2','2','2','3','2','4','2','5','2','6','2','7','2','8','2','9',
        '3','0','3','1','3','2','3','3','3','4','3','5','3','6','3','7','3','8','3','9',
        '4','0','4','1','4','2','4','3','4','4','4','5','4','6','4','7','4','8','4','9',
        '5','0','5','1','5','2','5','3','5','4','5','5','5','6','5','7','5','8','5','9',
        '6','0','6','1','6','2','6','3','6','4','6','5','6','6','6','7','6','8','6','9',
        '7','0','7','1','7','2','7','3','7','4','7','5','7','6','7','7','7','8','7','9',
        '8','0','8','1','8','2','8','3','8','4','8','5','8','6','8','7','8','8','8','9',
        '9','0','9','1','9','2','9','3','9','4','9','5','9','6','9','7','9','8','9','9'
    };
    
    constexpr std::int64_t UNIX_EPOCH_DAYS = 719468; // Days from year 0 to 1970-01-01
    
    std::int64_t seconds = milliseconds_since_epoch / 1000;
    int ms = milliseconds_since_epoch % 1000;
    std::int64_t days = seconds / 86400 + UNIX_EPOCH_DAYS;
    int sec_of_day = seconds % 86400;
    
    // Calculate year, month, day from days using Gregorian calendar algorithm
    int era = (days >= 0 ? days : days - 146096) / 146097;
    int doe = days - era * 146097;
    int yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
    int year = yoe + era * 400;
    int doy = doe - (365*yoe + yoe/4 - yoe/100);
    int mp = (5*doy + 2)/153;
    int day = doy - (153*mp+2)/5 + 1;
    int month = mp + (mp < 10 ? 3 : -9);
    year += (month <= 2);
    
    // Calculate hour, minute, second
    int hour = sec_of_day / 3600;
    int min = (sec_of_day % 3600) / 60;
    int sec = sec_of_day % 60;
    
    // Format: "2024-01-15T10:30:45.123Z"
    buffer[0] = '0' + year / 1000;
    buffer[1] = '0' + (year / 100) % 10;
    buffer[2] = '0' + (year / 10) % 10;
    buffer[3] = '0' + year % 10;
    buffer[4] = '-';
    buffer[5] = digits[month * 2];
    buffer[6] = digits[month * 2 + 1];
    buffer[7] = '-';
    buffer[8] = digits[day * 2];
    buffer[9] = digits[day * 2 + 1];
    buffer[10] = 'T';
    buffer[11] = digits[hour * 2];
    buffer[12] = digits[hour * 2 + 1];
    buffer[13] = ':';
    buffer[14] = digits[min * 2];
    buffer[15] = digits[min * 2 + 1];
    buffer[16] = ':';
    buffer[17] = digits[sec * 2];
    buffer[18] = digits[sec * 2 + 1];
    buffer[19] = '.';
    buffer[20] = '0' + ms / 100;
    buffer[21] = '0' + (ms / 10) % 10;
    buffer[22] = '0' + ms % 10;
    buffer[23] = 'Z';
    
    return {buffer, 24};
}

}  // anonymous namespace

std::atomic<std::shared_ptr<const std::string>> Journal::globalLogAttributesJson_;
std::size_t Journal::m_filePathOffset_ = 0;
bool Journal::m_jsonLogsEnabled = false;
thread_local Journal::JsonLogContext Journal::currentJsonLogContext_{};

//------------------------------------------------------------------------------

// A Sink that does nothing.
class NullJournalSink : public Journal::Sink
{
public:
    NullJournalSink() : Sink(severities::kDisabled, false)
    {
    }

    ~NullJournalSink() override = default;

    bool
    active(severities::Severity) const override
    {
        return false;
    }

    bool
    console() const override
    {
        return false;
    }

    void
    console(bool) override
    {
    }

    severities::Severity
    threshold() const override
    {
        return severities::kDisabled;
    }

    void
    threshold(severities::Severity) override
    {
    }

    void
    write(severities::Severity, std::string&&) override
    {
    }

    void
    writeAlways(severities::Severity, std::string&&) override
    {
    }
};

//------------------------------------------------------------------------------

Journal::Sink&
Journal::getNullSink()
{
    static NullJournalSink sink;
    return sink;
}

//------------------------------------------------------------------------------

std::string_view
severities::to_string(Severity severity)
{
    using namespace std::string_view_literals;
    switch (severity)
    {
        case kDisabled:
            return "disabled"sv;
        case kTrace:
            return "trace"sv;
        case kDebug:
            return "debug"sv;
        case kInfo:
            return "info"sv;
        case kWarning:
            return "warning"sv;
        case kError:
            return "error"sv;
        case kFatal:
            return "fatal"sv;
        default:
            UNREACHABLE("Unexpected severity value!");
    }
    return ""sv;
}

void
Journal::JsonLogContext::reset(
    std::source_location location,
    severities::Severity severity,
    std::string_view moduleName,
    std::string_view journalAttributesJson) noexcept
{
    struct ThreadIdStringInitializer
    {
        std::string value;
        ThreadIdStringInitializer()
        {
            std::stringstream threadIdStream;
            threadIdStream.imbue(std::locale::classic());
            threadIdStream << std::this_thread::get_id();
            value = threadIdStream.str();
        }
    };
    thread_local ThreadIdStringInitializer const threadId;

    buffer_.clear();

    writer().startObject();

    if (!journalAttributesJson.empty())
    {
        writer().writeKey("JournalParams");
        writer().writeRaw(journalAttributesJson);
        writer().endObject();
    }

    {
        auto globalAttrs = globalLogAttributesJson_.load();
        if (globalAttrs && !globalAttrs->empty())
        {
            writer().writeKey("GlobalParams");
            writer().writeRaw(*globalAttrs);
            writer().endObject();
        }
    }

    writer().writeKey("ModuleName");
    writer().writeString(moduleName);
    writer().writeKey("Metadata");
    writer().startObject();

    writer().writeKey("Function");
    writer().writeString(location.function_name());

    writer().writeKey("File");
    std::string_view fileName = location.file_name();
    std::string_view trimmedFileName = (m_filePathOffset_ < fileName.size()) 
        ? fileName.substr(m_filePathOffset_) 
        : fileName;
    writer().writeString(trimmedFileName);

    writer().writeKey("Line");
    writer().writeUInt(location.line());

    writer().writeKey("ThreadId");
    writer().writeString(threadId.value);

    auto severityStr = to_string(severity);
    writer().writeKey("Level");
    writer().writeString(severityStr);

    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch()).count();
    writer().writeKey("Timestamp");
    writer().writeInt(nowMs);
    writer().writeKey("Time");
    writer().writeString(fastTimestampToString(nowMs));

    writer().endObject();

    writer().writeKey("MessageParams");
    writer().startObject();
}

void
Journal::initMessageContext(
    std::source_location location,
    severities::Severity severity) const
{
    currentJsonLogContext_.reset(location, severity, m_name, m_attributesJson);
}

std::string_view
Journal::formatLog(std::string&& message)
{
    if (!m_jsonLogsEnabled)
    {
        return message;
    }

    auto& writer = currentJsonLogContext_.writer();

    writer.endObject();

    writer.writeKey("Message");
    writer.writeString(message);

    writer.endObject();

    return writer.finish();
}
void
Journal::setRootPath(std::string_view fullPath, std::string_view relativePath)
{
    if (relativePath.size() <= fullPath.size() && 
        fullPath.ends_with(relativePath))
    {
        m_filePathOffset_ = fullPath.size() - relativePath.size();
    }
    else
    {
        m_filePathOffset_ = 0;
    }
}

void
Journal::enableStructuredJournal()
{
    m_jsonLogsEnabled = true;
}

void
Journal::disableStructuredJournal()
{
    m_jsonLogsEnabled = false;
    resetGlobalAttributes();
}

bool
Journal::isStructuredJournalEnabled()
{
    return m_jsonLogsEnabled;
}

Journal::Sink::Sink(Severity thresh, bool console)
    : thresh_(thresh), m_console(console)
{
}

Journal::Sink::~Sink() = default;

bool
Journal::Sink::active(Severity level) const
{
    return level >= thresh_;
}

bool
Journal::Sink::console() const
{
    return m_console;
}

void
Journal::Sink::console(bool output)
{
    m_console = output;
}

severities::Severity
Journal::Sink::threshold() const
{
    return thresh_;
}

void
Journal::Sink::threshold(Severity thresh)
{
    thresh_ = thresh;
}

//------------------------------------------------------------------------------

Journal::ScopedStream::ScopedStream(Sink& sink, Severity level)
    : m_sink(sink), m_level(level)
{
    // Modifiers applied from all ctors
    m_ostream << std::boolalpha << std::showbase;
}

Journal::ScopedStream::ScopedStream(
    Stream const& stream,
    std::ostream& manip(std::ostream&))
    : ScopedStream(stream.sink(), stream.level())
{
    m_ostream << manip;
}

Journal::ScopedStream::~ScopedStream()
{
    std::string s = m_ostream.str();
    if (!s.empty())
    {
        if (s == "\n")
            m_sink.write(m_level, std::string{formatLog("")});
        else
            m_sink.write(m_level, std::string{formatLog(std::move(s))});
    }
}

std::ostream&
Journal::ScopedStream::operator<<(std::ostream& manip(std::ostream&)) const
{
    return m_ostream << manip;
}

//------------------------------------------------------------------------------

Journal::ScopedStream
Journal::Stream::operator<<(std::ostream& manip(std::ostream&)) const
{
    return {*this, manip};
}

}  // namespace beast
