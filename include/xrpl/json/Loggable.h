#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_value.h>

namespace json {

/**
 * A view of a json::Value for one log statement.
 *
 * A raw json::Value does not stream into a beast::Journal: the log statement
 * names how the value is rendered by wrapping it in Masked or Unmasked. Both
 * do their work when streamed, so nothing runs when JLOG finds the sink
 * inactive.
 *
 * The view holds a reference and is neither copyable nor movable, so build it
 * inside the log statement, where the value outlives it.
 */
class LogView
{
public:
    /**
     * Wraps @p value for one log statement.
     *
     * @param value The value to log. It must outlive the view.
     */
    explicit LogView(Value const& value) : value_(value)
    {
    }

    LogView(LogView const&) = delete;
    LogView&
    operator=(LogView const&) = delete;

    /**
     * Returns the wrapped value.
     *
     * @return The value given to the constructor.
     */
    [[nodiscard]] Value const&
    value() const
    {
        return value_;
    }

protected:
    ~LogView() = default;

private:
    Value const& value_;
};

/**
 * Logs a value that can carry a credential: a client request, a field a client
 * supplied, or the reply of a command that generates or takes a key.
 *
 * Streams the text of loggable(): every field in kCredentialFields is masked
 * at every depth, and the text is truncated to kMaxLoggedChars. The value is
 * not copied when it carries no credential.
 */
class Masked final : public LogView
{
public:
    using LogView::LogView;
};

/**
 * Logs a value that carries no credential, such as ledger, transaction or
 * path data.
 *
 * Streams the compact text of the value, truncated to kMaxLoggedChars, and
 * masks nothing. The name is a claim about the value that a reviewer can
 * check.
 */
class Unmasked final : public LogView
{
public:
    using LogView::LogView;
};

/**
 * Starts a log message with the masked text of a value.
 *
 * @param stream The journal stream to write to.
 * @param masked The value to mask, truncate and write.
 * @return The scoped stream that carries the message.
 */
beast::Journal::ScopedStream
operator<<(beast::Journal::Stream const& stream, Masked const& masked);

/**
 * Appends the masked text of a value to a log message.
 *
 * @param stream The scoped stream that carries the message.
 * @param masked The value to mask, truncate and write.
 * @return @p stream.
 */
beast::Journal::ScopedStream const&
operator<<(beast::Journal::ScopedStream const& stream, Masked const& masked);

/**
 * Starts a log message with the text of a value that carries no credential.
 *
 * @param stream The journal stream to write to.
 * @param unmasked The value to truncate and write.
 * @return The scoped stream that carries the message.
 */
beast::Journal::ScopedStream
operator<<(beast::Journal::Stream const& stream, Unmasked const& unmasked);

/**
 * Appends the text of a value that carries no credential to a log message.
 *
 * @param stream The scoped stream that carries the message.
 * @param unmasked The value to truncate and write.
 * @return @p stream.
 */
beast::Journal::ScopedStream const&
operator<<(beast::Journal::ScopedStream const& stream, Unmasked const& unmasked);

}  // namespace json
