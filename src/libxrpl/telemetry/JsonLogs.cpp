//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#include <xrpl/json/to_string.h>
#include <xrpl/telemetry/JsonLogs.h>
#include <xrpl/basics/ToString.h>

#include <thread>

namespace ripple::log {

thread_local JsonStructuredJournal::Logger
    JsonStructuredJournal::currentLogger_{};

JsonLogAttributes::JsonLogAttributes(AttributeFields contextValues)
    : contextValues_(std::move(contextValues))
{
}

void
JsonLogAttributes::setModuleName(std::string const& name)
{
    contextValues()["Module"] = name;
}

std::unique_ptr<beast::Journal::StructuredLogAttributes>
JsonLogAttributes::clone() const
{
    return std::make_unique<JsonLogAttributes>(*this);
}

void
JsonLogAttributes::combine(
    std::unique_ptr<StructuredLogAttributes> const& context)
{
    auto structuredContext =
        static_cast<JsonLogAttributes const*>(context.get());
    contextValues_.merge(AttributeFields{structuredContext->contextValues_});
}

void
JsonLogAttributes::combine(std::unique_ptr<StructuredLogAttributes>&& context)
{
    auto structuredContext = static_cast<JsonLogAttributes*>(context.get());

    if (contextValues_.empty())
    {
        contextValues_ = std::move(structuredContext->contextValues_);
    }
    else
    {
        contextValues_.merge(structuredContext->contextValues_);
    }
}

JsonStructuredJournal::Logger::Logger(
    JsonStructuredJournal const* journal,
    std::source_location location)
    : location(location)
{
}

void
JsonStructuredJournal::Logger::write(
    beast::Journal::Sink* sink,
    beast::severities::Severity level,
    std::string const& text,
    beast::Journal::StructuredLogAttributes* context) const
{
    Json::Value globalContext;
    if (context)
    {
        auto jsonContext = static_cast<JsonLogAttributes*>(context);
        for (auto const& [key, value] : jsonContext->contextValues())
        {
            globalContext[key] = value;
        }
    }
    globalContext["Function"] = location.function_name();
    globalContext["File"] = location.file_name();
    globalContext["Line"] = location.line();
    std::stringstream threadIdStream;
    threadIdStream << std::this_thread::get_id();
    globalContext["ThreadId"] = threadIdStream.str();
    globalContext["Params"] = messageParams;
    globalContext["Level"] = to_string(level);
    globalContext["Message"] = text;
    globalContext["Time"] =
        to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count());

    sink->write(level, to_string(globalContext));
}

JsonStructuredJournal::Logger
JsonStructuredJournal::logger(std::source_location location) const
{
    return Logger{this, location};
}

void
JsonStructuredJournal::initMessageContext(std::source_location location)
{
    currentLogger_ = logger(location);
}

void
JsonStructuredJournal::flush(
    beast::Journal::Sink* sink,
    beast::severities::Severity level,
    std::string const& text,
    beast::Journal::StructuredLogAttributes* context)
{
    currentLogger_.write(sink, level, text, context);
}

}  // namespace ripple::log
