#ifndef PROTOCOL_GET_OR_THROW_H_
#define PROTOCOL_GET_OR_THROW_H_

#include <ripple/basics/Buffer.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/basics/contract.h>
#include <ripple/json/json_value.h>
#include <ripple/protocol/SField.h>

#include <charconv>
#include <exception>
#include <optional>
#include <string>

namespace Json {
struct JsonMissingKeyError : std::exception
{
    char const* const key;
    mutable std::string msg;
    JsonMissingKeyError(Json::StaticString const& k) : key{k.c_str()}
    {
    }
    const char*
    what() const noexcept override
    {
        if (msg.empty())
        {
            msg = std::string("Missing json key: ") + key;
        }
        return msg.c_str();
    }
};

struct JsonTypeMismatchError : std::exception
{
    char const* const key;
    std::string const expectedType;
    mutable std::string msg;
    JsonTypeMismatchError(Json::StaticString const& k, std::string et)
        : key{k.c_str()}, expectedType{std::move(et)}
    {
    }
    const char*
    what() const noexcept override
    {
        if (msg.empty())
        {
            msg = std::string("Type mismatch on json key: ") + key +
                "; expected type: " + expectedType;
        }
        return msg.c_str();
    }
};

template <class T>
T
getOrThrow(Json::Value const& v, ripple::SField const& field)
{
    static_assert(sizeof(T) == -1, "This function must be specialized");
}

template <>
inline std::string
getOrThrow(Json::Value const& v, ripple::SField const& field)
{
    using namespace ripple;
    Json::StaticString const& key = field.getJsonName();
    if (!v.isMember(key))
        Throw<JsonMissingKeyError>(key);

    Json::Value const& inner = v[key];
    if (!inner.isString())
        Throw<JsonTypeMismatchError>(key, "string");
    return inner.asString();
}

// Note, this allows integer numeric fields to act as bools
template <>
inline bool
getOrThrow(Json::Value const& v, ripple::SField const& field)
{
    using namespace ripple;
    Json::StaticString const& key = field.getJsonName();
    if (!v.isMember(key))
        Throw<JsonMissingKeyError>(key);
    Json::Value const& inner = v[key];
    if (inner.isBool())
        return inner.asBool();
    if (!inner.isIntegral())
        Throw<JsonTypeMismatchError>(key, "bool");

    return inner.asInt() != 0;
}

template <>
inline std::uint64_t
getOrThrow(Json::Value const& v, ripple::SField const& field)
{
    using namespace ripple;
    Json::StaticString const& key = field.getJsonName();
    if (!v.isMember(key))
        Throw<JsonMissingKeyError>(key);
    Json::Value const& inner = v[key];
    if (inner.isUInt())
        return inner.asUInt();
    if (inner.isInt())
    {
        auto const r = inner.asInt();
        if (r < 0)
            Throw<JsonTypeMismatchError>(key, "uint64");
        return r;
    }
    if (inner.isString())
    {
        auto const s = inner.asString();
        // parse as hex
        std::uint64_t val;

        auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), val, 16);

        if (ec != std::errc() || (p != s.data() + s.size()))
            Throw<JsonTypeMismatchError>(key, "uint64");
        return val;
    }
    Throw<JsonTypeMismatchError>(key, "uint64");
}

template <>
inline ripple::Buffer
getOrThrow(Json::Value const& v, ripple::SField const& field)
{
    using namespace ripple;
    std::string const hex = getOrThrow<std::string>(v, field);
    if (auto const r = strUnHex(hex))
    {
        // TODO: mismatch between a buffer and a blob
        return Buffer{r->data(), r->size()};
    }
    Throw<JsonTypeMismatchError>(field.getJsonName(), "Buffer");
}

// This function may be used by external projects (like the witness server).
template <class T>
std::optional<T>
getOptional(Json::Value const& v, ripple::SField const& field)
{
    try
    {
        return getOrThrow<T>(v, field);
    }
    catch (...)
    {
    }
    return {};
}

}  // namespace Json

#endif  // PROTOCOL_GET_OR_THROW_H_
