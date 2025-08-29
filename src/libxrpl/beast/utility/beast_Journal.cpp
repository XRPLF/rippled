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

std::string_view
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
    contextValues_ = {};
    contextValues_.emplace_object();
}

Journal::JsonLogAttributes::JsonLogAttributes(JsonLogAttributes const& other)
{
    contextValues_ = other.contextValues_;
}

Journal::JsonLogAttributes::JsonLogAttributes(JsonLogAttributes&& other)
{
    contextValues_ = std::move(other.contextValues_);
}

Journal::JsonLogAttributes&
Journal::JsonLogAttributes::operator=(JsonLogAttributes const& other)
{
    if (&other == this)
    {
        return *this;
    }
    contextValues_ = other.contextValues_;
    return *this;
}

Journal::JsonLogAttributes&
Journal::JsonLogAttributes::operator=(JsonLogAttributes&& other)
{
    if (&other == this)
    {
        return *this;
    }

    contextValues_ = std::move(other.contextValues_);
    return *this;
}

void
Journal::JsonLogAttributes::setModuleName(std::string const& name)
{
    contextValues_.as_object()["Module"] = name;
}

void
Journal::JsonLogAttributes::combine(AttributeFields const& from)
{
    for (auto& [key, value] : from.as_object())
    {
        contextValues_.as_object()[key] = value;
    }
}

void
Journal::JsonLogContext::reset(
    std::source_location location,
    severities::Severity severity,
    std::optional<JsonLogAttributes> const& attributes) noexcept
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

    attributes_.emplace_null();
    location_ = location;
    if (globalLogAttributes_.has_value())
    {
        attributes_ = globalLogAttributes_->contextValues();

        if (attributes.has_value())
        {
            for (auto& [key, value] : attributes->contextValues().as_object())
            {
                attributes_.as_object()[key] = value;
            }
        }
    }
    else if (attributes.has_value())
    {
        attributes_ = attributes->contextValues();
    }

    if (attributes_.is_null())
    {
        attributes_ = {};
        attributes_.emplace_object();
    }

    attributes_.as_object()["Params"] = boost::json::object{};
    attributes_.as_object()["Function"] =
        currentJsonLogContext_.location().function_name();
    attributes_.as_object()["File"] =
        currentJsonLogContext_.location().file_name();
    attributes_.as_object()["Line"] = currentJsonLogContext_.location().line();
    attributes_.as_object()["ThreadId"] = threadId.value;
    attributes_.as_object()["Level"] = to_string(severity);
    attributes_.as_object()["Time"] =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
}

void
Journal::initMessageContext(
    std::source_location location,
    severities::Severity severity) const
{
    currentJsonLogContext_.reset(location, severity, m_attributes);
}

std::string
Journal::formatLog(std::string const& message)
{
    if (!m_jsonLogsEnabled)
    {
        return message;
    }

    auto& attributes = currentJsonLogContext_.attributes().as_object();

    attributes["Message"] = message;

    return boost::json::serialize(attributes);
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
    std::string const& s(m_ostream.str());
    if (!s.empty())
    {
        if (s == "\n")
            m_sink.write(m_level, formatLog(""));
        else
            m_sink.write(m_level, formatLog(s));
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
