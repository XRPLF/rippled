#include <xrpl/json/Loggable.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/MaskSecrets.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>

#include <string>

namespace json {

namespace {

/**
 * Renders a value that carries no credential for a log line.
 *
 * @param value The value to render.
 * @return The compact text, truncated to kMaxLoggedChars.
 */
std::string
truncated(Value const& value)
{
    auto text = to_string(value);
    if (text.size() > kMaxLoggedChars)
        text.resize(kMaxLoggedChars);
    return text;
}

}  // namespace

beast::Journal::ScopedStream
operator<<(beast::Journal::Stream const& stream, Masked const& masked)
{
    return {stream, loggable(masked.value())};
}

beast::Journal::ScopedStream const&
operator<<(beast::Journal::ScopedStream const& stream, Masked const& masked)
{
    return stream << loggable(masked.value());
}

beast::Journal::ScopedStream
operator<<(beast::Journal::Stream const& stream, Unmasked const& unmasked)
{
    return {stream, truncated(unmasked.value())};
}

beast::Journal::ScopedStream const&
operator<<(beast::Journal::ScopedStream const& stream, Unmasked const& unmasked)
{
    return stream << truncated(unmasked.value());
}

}  // namespace json
