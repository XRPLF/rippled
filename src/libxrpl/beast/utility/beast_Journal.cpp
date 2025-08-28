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
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

#include <thread>
#include <ranges>
#include <ios>
#include <ostream>
#include <string>

namespace beast {

std::optional<Journal::JsonLogAttributes> Journal::globalLogAttributes_;
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
        allocator_
    );
}

Journal::JsonLogAttributes
Journal::JsonLogAttributes::combine(
    AttributeFields const& a,
    AttributeFields const& b)
{
    JsonLogAttributes result;

    result.contextValues_.CopyFrom(a, result.allocator_);

    for (auto& member : b.GetObject())
    {
        auto val = rapidjson::Value{ member.value, result.allocator_ };
        if (result.contextValues_.HasMember(member.name))
        {
            result.contextValues_[member.name] = std::move(val);
        }
        else
        {
            result.contextValues_.AddMember(
                rapidjson::Value{member.name, result.allocator_},
                std::move(val),
                result.allocator_);
        }
    }

    return result;
}

void
Journal::initMessageContext(std::source_location location)
{
    currentJsonLogContext_.reset(location);
}

std::string
Journal::formatLog(
    std::string const& message,
    severities::Severity severity,
    std::optional<JsonLogAttributes> const& attributes)
{
    if (!m_jsonLogsEnabled)
    {
        return message;
    }

    rapidjson::Document doc{&currentJsonLogContext_.allocator};
    rapidjson::Value logContext;
    logContext.SetObject();

    if (globalLogAttributes_)
    {
        for (auto const& [key, value] : globalLogAttributes_->contextValues().GetObject())
        {
            rapidjson::Value jsonValue;
            jsonValue.CopyFrom(value, currentJsonLogContext_.allocator);

            logContext.AddMember(
                rapidjson::Value{key, currentJsonLogContext_.allocator},
                std::move(jsonValue),
                currentJsonLogContext_.allocator);
        }
    }

    if (attributes.has_value())
    {
        for (auto const& [key, value] : attributes->contextValues().GetObject())
        {
            rapidjson::Value jsonValue;
            jsonValue.CopyFrom(value, currentJsonLogContext_.allocator);

            logContext.AddMember(
                rapidjson::Value{key, currentJsonLogContext_.allocator},
                std::move(jsonValue),
                currentJsonLogContext_.allocator);
        }
    }
    logContext.AddMember(
        rapidjson::StringRef("Function"),
        rapidjson::StringRef(currentJsonLogContext_.location.function_name()),
        currentJsonLogContext_.allocator);

    logContext.AddMember(
        rapidjson::StringRef("File"),
        rapidjson::StringRef(currentJsonLogContext_.location.file_name()),
        currentJsonLogContext_.allocator);

    logContext.AddMember(
        rapidjson::StringRef("Line"),
        currentJsonLogContext_.location.line(),
        currentJsonLogContext_.allocator);
    std::stringstream threadIdStream;
    threadIdStream << std::this_thread::get_id();
    auto threadIdStr = threadIdStream.str();
    logContext.AddMember(
        rapidjson::StringRef("ThreadId"),
        rapidjson::StringRef(threadIdStr.c_str()),
        currentJsonLogContext_.allocator);
    logContext.AddMember(
        rapidjson::StringRef("Params"),
        std::move(currentJsonLogContext_.messageParams),
        currentJsonLogContext_.allocator);
    currentJsonLogContext_.messageParams = rapidjson::Value{};
    currentJsonLogContext_.messageParams.SetObject();
    auto severityStr = to_string(severity);
    logContext.AddMember(
        rapidjson::StringRef("Level"),
        rapidjson::StringRef(severityStr.c_str()),
        currentJsonLogContext_.allocator);
    logContext.AddMember(
        rapidjson::StringRef("Message"),
        rapidjson::StringRef(message.c_str()),
        currentJsonLogContext_.allocator);
    logContext.AddMember(
        rapidjson::StringRef("Time"),
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count(),
        currentJsonLogContext_.allocator);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer writer(buffer);

    logContext.Accept(writer);

    return {buffer.GetString()};
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

Journal::ScopedStream::ScopedStream(
    std::optional<JsonLogAttributes> attributes,
    Sink& sink,
    Severity level)
    : m_attributes(std::move(attributes)), m_sink(sink), m_level(level)
{
    // Modifiers applied from all ctors
    m_ostream << std::boolalpha << std::showbase;
}

Journal::ScopedStream::ScopedStream(
    std::optional<JsonLogAttributes> attributes,
    Stream const& stream,
    std::ostream& manip(std::ostream&))
    : ScopedStream(std::move(attributes), stream.sink(), stream.level())
{
    m_ostream << manip;
}

Journal::ScopedStream::~ScopedStream()
{
    std::string const& s(m_ostream.str());
    if (!s.empty())
    {
        if (s == "\n")
            m_sink.write(m_level, formatLog("", m_level, m_attributes));
        else
            m_sink.write(m_level, formatLog(s, m_level, m_attributes));
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
    return {m_attributes, *this, manip};
}

}  // namespace beast
