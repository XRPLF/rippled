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
#include <shared_mutex>
#include <string>
#include <thread>

namespace beast {

std::string Journal::globalLogAttributesJson_;
std::shared_mutex Journal::globalLogAttributesMutex_;
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
        std::shared_lock lock(globalLogAttributesMutex_);
        if (!globalLogAttributesJson_.empty())
        {
            writer().writeKey("GlobalParams");
            writer().writeRaw(globalLogAttributesJson_);
            writer().endObject();
        }
    }

    writer().writeKey("ModuleName");
    writer().writeString(moduleName);
    writer().writeKey("MessageParams");
    writer().startObject();

    writer().writeKey("Function");
    writer().writeString(location.function_name());

    writer().writeKey("File");
    writer().writeString(location.file_name());

    writer().writeKey("Line");
    writer().writeUInt(location.line());

    writer().writeKey("ThreadId");
    writer().writeString(threadId.value);

    auto severityStr = to_string(severity);
    writer().writeKey("Level");
    writer().writeString(severityStr);

    writer().writeKey("Time");
    writer().writeInt(std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count());
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
