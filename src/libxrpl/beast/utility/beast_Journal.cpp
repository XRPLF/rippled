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
    contextValues_.reset(yyjson_mut_doc_new(nullptr));
    yyjson_mut_doc_set_root(
        contextValues_.get(), yyjson_mut_obj(contextValues_.get()));
}

Journal::JsonLogAttributes::JsonLogAttributes(JsonLogAttributes const& other)
{
    contextValues_.reset(
        yyjson_mut_doc_mut_copy(other.contextValues_.get(), nullptr));
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
    contextValues_.reset(
        yyjson_mut_doc_mut_copy(other.contextValues_.get(), nullptr));
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
    auto root = yyjson_mut_doc_get_root(contextValues_.get());
    ripple::log::detail::setJsonField(
        "Module", name, contextValues_.get(), root);
}

Journal::JsonLogAttributes
Journal::JsonLogAttributes::combine(
    AttributeFields const& a,
    AttributeFields const& b)
{
    JsonLogAttributes result;

    result.contextValues_.reset(yyjson_mut_doc_mut_copy(a.get(), nullptr));

    auto bRoot = yyjson_mut_doc_get_root(b.get());
    auto root = yyjson_mut_doc_get_root(result.contextValues_.get());
    for (auto iter = yyjson_mut_obj_iter_with(bRoot);
         yyjson_mut_obj_iter_has_next(&iter);
         yyjson_mut_obj_iter_next(&iter))
    {
        auto key = iter.cur;
        auto val = yyjson_mut_obj_iter_get_val(key);

        auto keyCopied =
            yyjson_mut_val_mut_copy(result.contextValues_.get(), key);
        auto valueCopied =
            yyjson_mut_val_mut_copy(result.contextValues_.get(), val);

        yyjson_mut_obj_put(root, keyCopied, valueCopied);
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

    yyjson::YYJsonDocPtr doc{yyjson_mut_doc_new(nullptr)};
    auto logContext = yyjson_mut_obj(doc.get());
    yyjson_mut_doc_set_root(doc.get(), logContext);

    if (globalLogAttributes_)
    {
        auto root = yyjson_mut_doc_get_root(
            globalLogAttributes_->contextValues().get());
        for (auto iter = yyjson_mut_obj_iter_with(root);
             yyjson_mut_obj_iter_has_next(&iter);
             yyjson_mut_obj_iter_next(&iter))
        {
            auto key = iter.cur;
            auto val = yyjson_mut_obj_iter_get_val(key);

            auto keyCopied = yyjson_mut_val_mut_copy(doc.get(), key);
            auto valueCopied = yyjson_mut_val_mut_copy(doc.get(), val);

            yyjson_mut_obj_put(logContext, keyCopied, valueCopied);
        }
    }

    if (attributes.has_value())
    {
        auto root = yyjson_mut_doc_get_root(attributes->contextValues().get());
        for (auto iter = yyjson_mut_obj_iter_with(root);
             yyjson_mut_obj_iter_has_next(&iter);
             yyjson_mut_obj_iter_next(&iter))
        {
            auto key = iter.cur;
            auto val = yyjson_mut_obj_iter_get_val(key);

            auto keyCopied = yyjson_mut_val_mut_copy(doc.get(), key);
            auto valueCopied = yyjson_mut_val_mut_copy(doc.get(), val);

            yyjson_mut_obj_put(logContext, keyCopied, valueCopied);
        }
    }

    ripple::log::detail::setJsonField(
        "Function",
        currentJsonLogContext_.location.function_name(),
        doc.get(),
        logContext);
    ripple::log::detail::setJsonField(
        "File",
        currentJsonLogContext_.location.file_name(),
        doc.get(),
        logContext);
    ripple::log::detail::setJsonField(
        "Line",
        static_cast<std::uint64_t>(currentJsonLogContext_.location.line()),
        doc.get(),
        logContext);

    std::stringstream threadIdStream;
    threadIdStream << std::this_thread::get_id();
    auto threadIdStr = threadIdStream.str();

    auto messageParamsRoot =
        yyjson_mut_doc_get_root(currentJsonLogContext_.messageParams.get());
    ripple::log::detail::setJsonField(
        "ThreadId", threadIdStr, doc.get(), logContext);
    ripple::log::detail::setJsonField(
        "Params", messageParamsRoot, doc.get(), logContext);

    yyjson_mut_doc_set_root(
        currentJsonLogContext_.messageParams.get(), nullptr);
    auto severityStr = to_string(severity);
    ripple::log::detail::setJsonField(
        "Level", severityStr, doc.get(), logContext);
    ripple::log::detail::setJsonField(
        "Message", message, doc.get(), logContext);
    ripple::log::detail::setJsonField(
        "Time",
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count(),
        doc.get(),
        logContext);

    std::string result = yyjson_mut_write(doc.get(), 0, nullptr);
    return result;
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
