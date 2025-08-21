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

#ifndef RIPPLE_LOGGING_STRUCTUREDJOURNAL_H_INCLUDED
#define RIPPLE_LOGGING_STRUCTUREDJOURNAL_H_INCLUDED

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_value.h>

#include <memory>
#include <source_location>
#include <unordered_map>
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

class JsonLogAttributes : public beast::Journal::StructuredLogAttributes
{
public:
    using AttributeFields = std::unordered_map<std::string, Json::Value>;
    using Pair = AttributeFields::value_type;

    explicit JsonLogAttributes(AttributeFields contextValues = {});

    void
    setModuleName(std::string const& name) override;

    [[nodiscard]] std::unique_ptr<StructuredLogAttributes>
    clone() const override;

    void
    combine(std::unique_ptr<StructuredLogAttributes> const& context) override;

    void
    combine(std::unique_ptr<StructuredLogAttributes>&& context) override;

    AttributeFields&
    contextValues()
    {
        return contextValues_;
    }

private:
    AttributeFields contextValues_;
};

class JsonStructuredJournal : public beast::Journal::StructuredJournalImpl
{
private:
    struct Logger
    {
        std::source_location location = {};
        Json::Value messageParams;

        Logger() = default;
        Logger(
            JsonStructuredJournal const* journal,
            std::source_location location);

        void
        write(
            beast::Journal::Sink* sink,
            beast::severities::Severity level,
            std::string const& text,
            beast::Journal::StructuredLogAttributes* context) const;
    };

    [[nodiscard]] Logger
    logger(std::source_location location) const;

    static thread_local Logger currentLogger_;

    template <typename T>
    friend std::ostream&
    operator<<(std::ostream& os, LogParameter<T> const&);

    template <typename T>
    friend std::ostream&
    operator<<(std::ostream& os, LogField<T> const&);

public:
    void
    initMessageContext(std::source_location location) override;

    void
    flush(
        beast::Journal::Sink* sink,
        beast::severities::Severity level,
        std::string const& text,
        beast::Journal::StructuredLogAttributes* context) override;
};

template <typename T>
std::ostream&
operator<<(std::ostream& os, LogParameter<T> const& param)
{
    using ValueType = std::decay_t<T>;
    // TODO: Update the Json library to support 64-bit integer values.
    if constexpr (
        std::constructible_from<Json::Value, ValueType> &&
        (!std::is_integral_v<ValueType> ||
         sizeof(ValueType) <= sizeof(Json::Int)))
    {
        JsonStructuredJournal::currentLogger_.messageParams[param.name_] =
            Json::Value{param.value_};
        return os << param.value_;
    }
    else
    {
        std::ostringstream oss;
        oss << param.value_;

        JsonStructuredJournal::currentLogger_.messageParams[param.name_] =
            oss.str();
        return os << oss.str();
    }
}

template <typename T>
std::ostream&
operator<<(std::ostream& os, LogField<T> const& param)
{
    using ValueType = std::decay_t<T>;
    // TODO: Update the Json library to support 64-bit integer values.
    if constexpr (
        std::constructible_from<Json::Value, ValueType> &&
        (!std::is_integral_v<ValueType> ||
         sizeof(ValueType) <= sizeof(Json::Int)))
    {
        JsonStructuredJournal::currentLogger_.messageParams[param.name_] =
            Json::Value{param.value_};
    }
    else
    {
        std::ostringstream oss;
        oss << param.value_;

        JsonStructuredJournal::currentLogger_.messageParams[param.name_] =
            oss.str();
    }
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

[[nodiscard]] inline std::unique_ptr<JsonLogAttributes>
attributes(std::initializer_list<JsonLogAttributes::Pair> const& fields)
{
    return std::make_unique<JsonLogAttributes>(fields);
}

}  // namespace ripple::log

#endif
