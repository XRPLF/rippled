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

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <ios>
#include <ostream>
#include <ranges>
#include <string>
#include <thread>

namespace beast {

std::optional<Journal::JsonLogAttributes> Journal::globalLogAttributes_;
std::optional<std::string> Journal::globalLogAttributesJson_;

std::mutex Journal::globalLogAttributesMutex_;
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

std::string
severities::to_string(Severity severity)
{
    switch (severity)
    {
        case kDisabled:
            return "disabled";
        case kTrace:
            return "trace";
        case kDebug:
            return "debug";
        case kInfo:
            return "info";
        case kWarning:
            return "warning";
        case kError:
            return "error";
        case kFatal:
            return "fatal";
        default:
            UNREACHABLE("Unexpected severity value!");
    }
    return "";
}

Journal::JsonLogAttributes::JsonLogAttributes()
{
    contextValues_.SetObject();
}

Journal::JsonLogAttributes::JsonLogAttributes(JsonLogAttributes const& other)
{
    contextValues_.SetObject();
    contextValues_.CopyFrom(other.contextValues_, allocator_);
}

Journal::JsonLogAttributes&
Journal::JsonLogAttributes::operator=(JsonLogAttributes const& other)
{
    if (&other == this)
    {
        return *this;
    }
    contextValues_.CopyFrom(other.contextValues_, allocator_);
    return *this;
}

void
Journal::JsonLogAttributes::setModuleName(std::string const& name)
{
    contextValues_.AddMember(
        rapidjson::StringRef("Module"),
        rapidjson::Value{name.c_str(), allocator_},
        allocator_);
}

void
Journal::JsonLogAttributes::combine(
    AttributeFields const& from)
{
    for (auto& member : from.GetObject())
    {
        contextValues_.RemoveMember(member.name);

        contextValues_.AddMember(
            rapidjson::Value{member.name, allocator_},
            rapidjson::Value{member.value, allocator_},
            allocator_);
    }
}

void
Journal::addGlobalAttributes(JsonLogAttributes globalLogAttributes)
{
    std::lock_guard lock(globalLogAttributesMutex_);
    if (!globalLogAttributes_)
    {
        globalLogAttributes_ = JsonLogAttributes{};
    }
    globalLogAttributes_->combine(globalLogAttributes.contextValues());

    rapidjson::StringBuffer buffer;
    rapidjson::Writer writer{buffer};

    globalLogAttributes_->contextValues().Accept(writer);

    globalLogAttributesJson_ = {buffer.GetString()};
}

void
Journal::JsonLogContext::reset(
    std::source_location location,
    severities::Severity severity,
    std::optional<std::string> const& journalAttributesJson) noexcept
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

    journalAttributesJson_ = journalAttributesJson;

    buffer_.Clear();

    writer().StartObject();

    writer().Key("Function");
    writer().String(location.function_name());

    writer().Key("File");
    writer().String(location.file_name());

    writer().Key("Line");
    writer().Int64(location.line());

    writer().Key("ThreadId");
    writer().String(threadId.value.c_str());

    auto severityStr = to_string(severity);
    writer().Key("Level");
    writer().String(severityStr.c_str());

    writer().Key("Time");
    writer().Uint64(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

void
Journal::initMessageContext(
    std::source_location location,
    severities::Severity severity) const
{
    currentJsonLogContext_.reset(location, severity, m_attributesJson);
}

std::string
Journal::formatLog(std::string&& message)
{
    if (!m_jsonLogsEnabled)
    {
        return message;
    }

    currentJsonLogContext_.writer().EndObject();
    auto messageParams = currentJsonLogContext_.messageParams();

    rapidjson::StringBuffer buffer;
    rapidjson::Writer writer(buffer);

    writer.StartObject();
    if (globalLogAttributesJson_.has_value())
    {
        writer.Key("GlobalParams");
        writer.RawValue(
            globalLogAttributesJson_->c_str(),
            globalLogAttributesJson_->length(),
            rapidjson::kObjectType);
    }

    if (currentJsonLogContext_.journalAttributesJson().has_value())
    {
        writer.Key("JournalParams");
        writer.RawValue(
            currentJsonLogContext_.journalAttributesJson()->c_str(),
            currentJsonLogContext_.journalAttributesJson()->length(),
            rapidjson::kObjectType);
    }

    writer.Key("MessageParams");
    writer.RawValue(messageParams, std::strlen(messageParams), rapidjson::kObjectType);

    writer.Key("Message");
    writer.String(message.c_str(), message.length());

    writer.EndObject();

    return {buffer.GetString()};
}

void
Journal::rebuildAttributeJson()
{
    if (m_attributes.has_value())
    {
        rapidjson::StringBuffer buffer;
        rapidjson::Writer writer{buffer};

        m_attributes->contextValues().Accept(writer);

        m_attributesJson = {buffer.GetString()};
    }
    else
    {
        m_attributesJson = {};
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
            m_sink.write(m_level, formatLog(""));
        else
            m_sink.write(m_level, formatLog(std::move(s)));
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
