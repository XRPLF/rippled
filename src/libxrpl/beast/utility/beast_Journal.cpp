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

#include <date/date.h>

#include <chrono>
#include <ios>
#include <ostream>
#include <ranges>
#include <string>
#include <thread>

namespace beast {

std::string Journal::globalLogAttributes_;
std::shared_mutex Journal::globalLogAttributesMutex_;
bool Journal::jsonLogsEnabled_ = false;
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
    write(severities::Severity, std::string const&) override
    {
    }

    void
    writeAlways(severities::Severity, std::string const&) override
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
Journal::JsonLogContext::start(
    std::source_location location,
    severities::Severity severity,
    std::string_view moduleName,
    std::string_view journalAttributes) noexcept
{
    struct ThreadIdStringInitializer
    {
        std::string value;
        ThreadIdStringInitializer()
        {
            std::stringstream threadIdStream;
            threadIdStream << std::this_thread::get_id();
            value = threadIdStream.str();
        }
    };
    thread_local ThreadIdStringInitializer const threadId;

    messageOffset_ = 0;
    messageBuffer_.clear();
    jsonWriter_ = detail::SimpleJsonWriter{&messageBuffer_};

    if (!jsonLogsEnabled_)
    {
        messageBuffer_ = journalAttributes;
        return;
    }

    writer().startObject();

    if (!journalAttributes.empty())
    {
        writer().writeKey("Jnl");
        writer().writeRaw(journalAttributes);
        writer().endObject();
    }

    {
        std::shared_lock lock(globalLogAttributesMutex_);
        if (!globalLogAttributes_.empty())
        {
            writer().writeKey("Glb");
            writer().writeRaw(globalLogAttributes_);
            writer().endObject();
        }
    }

    writer().writeKey("Mtd");
    writer().startObject();

    writer().writeKey("Mdl");
    writer().writeString(moduleName);

    writer().writeKey("Fl");
    constexpr size_t FILE_NAME_KEEP_CHARS = 20;
    std::string_view fileName = location.file_name();
    std::string_view trimmedFileName = (fileName.size() > FILE_NAME_KEEP_CHARS)
        ? fileName.substr(fileName.size() - FILE_NAME_KEEP_CHARS)
        : fileName;
    writer().writeString(trimmedFileName);

    writer().writeKey("Ln");
    writer().writeUInt(location.line());

    writer().writeKey("ThId");
    writer().writeString(threadId.value);

    auto severityStr = to_string(severity);
    writer().writeKey("Lv");
    writer().writeString(severityStr);

    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::system_clock::now().time_since_epoch());
    writer().writeKey("Tm");
    writer().writeString(date::format("%Y-%b-%d %T %Z", nowMs));

    writer().endObject();

    hasMessageParams_ = false;
}

void
Journal::JsonLogContext::reuseJson()
{
    messageOffset_ = messageBuffer_.size();
}

void
Journal::JsonLogContext::finish()
{
    if (messageOffset_ != 0)
    {
        messageBuffer_.erase(messageOffset_);
    }
    else
    {
        messageBuffer_.clear();
    }

    jsonWriter_ = detail::SimpleJsonWriter{&messageBuffer_};
}

void
Journal::initMessageContext(
    std::source_location location,
    severities::Severity severity) const
{
    currentJsonLogContext_.start(location, severity, name_, attributes_);
}

std::string&
Journal::formatLog(std::string const& message)
{
    if (!jsonLogsEnabled_)
    {
        currentJsonLogContext_.writer().buffer() += message;
        return currentJsonLogContext_.messageBuffer();
    }

    auto& writer = currentJsonLogContext_.writer();

    currentJsonLogContext_.endMessageParams();

    writer.writeKey("Msg");
    writer.writeString(message);

    writer.endObject();

    writer.finish();

    return currentJsonLogContext_.messageBuffer();
}

void
Journal::enableStructuredJournal()
{
    jsonLogsEnabled_ = true;
}

void
Journal::disableStructuredJournal()
{
    jsonLogsEnabled_ = false;
    resetGlobalAttributes();
}

bool
Journal::isStructuredJournalEnabled()
{
    return jsonLogsEnabled_;
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
            s = "";

        m_sink.write(m_level, formatLog(s));
        currentJsonLogContext_.finish();
    }
}

std::ostream&
Journal::ScopedStream::operator<<(std::ostream& manip(std::ostream&)) const
{
    return m_ostream << manip;
}

}  // namespace beast
