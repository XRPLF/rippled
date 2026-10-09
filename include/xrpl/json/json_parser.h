#pragma once

#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>

#include <boost/asio/buffer.hpp>
#include <boost/json/basic_parser_impl.hpp>
#include <boost/json/parse_options.hpp>
#include <boost/json/string_view.hpp>
#include <boost/system/error_code.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <ios>
#include <istream>
#include <limits>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace json {

// clang-format off
/**
 * A Visitor is any type; the Parser calls each of the hooks below only if
 * the visitor declares it (checked at compile time, no vtable). This allows
 * the user to create a visitor with only the hooks they need. The following
 * is the complete set of hooks the Parser recognizes. The author can add any
 * additional methods or members to the visitor that they wish.
 *
 * Because each hook is optional, a hook that is misspelled or whose
 * signature cannot accept the Parser's arguments is silently ignored rather
 * than rejected at compile time.
 *
 * @code
 * struct FullVisitor
 * {
 *     using ReturnType = std::expected<void, std::string>;
 *
 *     ReturnType onDocumentBegin();
 *     ReturnType onDocumentEnd(std::size_t documentSize);
 *     ReturnType onKey(std::string_view value);
 *     ReturnType onString(std::string_view value);
 *     ReturnType onComment(std::string_view value);
 *     ReturnType onInt(Value::Int value);
 *     ReturnType onUInt(Value::UInt value);
 *     ReturnType onDouble(double value);
 *     ReturnType onBool(bool value);
 *     ReturnType onNull();
 *     ReturnType onObjectBegin();
 *     ReturnType onObjectEnd(std::size_t memberCount);
 *     ReturnType onArrayBegin();
 *     ReturnType onArrayEnd(std::size_t elementCount);
 * };
 * @endcode
 */
// clang-format on

// Offers CALL to every visitor that declares it, in order, stopping at the
// first one that rejects it. Evaluates to false if any visitor rejected the
// event, with that visitor's message left in pendingError_.
#define DISPATCH_VISITORS(CALL)                                                              \
    dispatch([&](auto* dispatchVisitorsVisitor) -> decltype(dispatchVisitorsVisitor->CALL) { \
        return dispatchVisitorsVisitor->CALL;                                                \
    })

template <typename... Visitor>
class Parser
{
    class ErrorInfo
    {
    public:
        explicit ErrorInfo() = default;

        std::int64_t line{};
        std::int64_t column{};
        std::string message;
    };

    using Errors = std::vector<ErrorInfo>;

    // An object or array that has been opened and not yet closed.
    struct Container
    {
        bool isObject{};
        std::size_t count{};
    };

    std::tuple<Visitor*...> visitors_;
    Errors errors_;

    // Scratch state for the parse in progress.
    std::vector<Container> containers_;
    // The key, string, or number being assembled from Boost's partial events.
    std::string text_;
    bool inText_{};
    std::string comment_;
    // Set once the root value is complete; nothing after it is examined.
    bool rootComplete_{};
    // Set when the Parser, rather than Boost, stopped the parse.
    bool rejected_{};
    std::string pendingError_;
    // Set once nothing more is to be offered to Boost: after the final write,
    // an error, or text following the root value.
    bool stopped_{};
    // The line and column (both from 1) of the next character to be fed.
    std::int64_t line_{1};
    std::int64_t column_{1};
    // Whether the last character fed was '\r', so that a '\n' following it,
    // possibly in the next buffer, does not start another line.
    bool afterCarriageReturn_{};

public:
    using Char = char;
    using Location = Char const*;

    Parser(Parser const&) = delete;
    Parser&
    operator=(Parser const&) = delete;
    Parser(Parser&& other) noexcept = default;
    Parser&
    operator=(Parser&& other) noexcept = default;

    // clang-format off
    /**
     * @brief Construct a Parser reporting to @a visitors.
     * @param visitors Retained by reference; each must outlive the Parser.
     *     Anything a visitor accumulates is therefore read back from the
     *     caller's own object once parsing completes.
     */
    // clang-format on
    explicit Parser(Visitor&... visitors) : visitors_{&visitors...}
    {
    }

    /**
     * @brief The default for depthLimit.
     */
    static constexpr std::size_t kDefaultDepthLimit = 25;

    /**
     * @brief The maximum depth of the JSON document.
     */
    std::size_t depthLimit{kDefaultDepthLimit};
    /**
     * @brief The maximum size of the JSON document.
     */
    std::size_t documentSizeLimit{std::numeric_limits<std::size_t>::max()};
    /**
     * @brief The maximum size of a key in the JSON document.
     */
    std::size_t keySizeLimit{std::numeric_limits<std::size_t>::max()};
    /**
     * @brief The maximum size of a string in the JSON document.
     */
    std::size_t stringSizeLimit{std::numeric_limits<std::size_t>::max()};
    /**
     * @brief The maximum number of members in an object in the JSON document.
     */
    std::size_t objectMembersLimit{std::numeric_limits<std::size_t>::max()};
    /**
     * @brief The maximum number of elements in an array in the JSON document.
     */
    std::size_t arrayElementsLimit{std::numeric_limits<std::size_t>::max()};

    template <std::size_t I>
    auto&
    visitor()
    {
        return *std::get<I>(visitors_);
    }

    /**
     * @brief Re-point the parser at @a visitors, which must outlive it. A move
     * carries the source's visitor pointers across, so an owner holding its
     * visitors as members must rebind them.
     */
    void
    visitors(Visitor&... visitors)
    {
        visitors_ = {&visitors...};
    }

    // clang-format off
    /**
     * @brief Report a <a HREF="http://www.json.org">JSON</a> document to the
     *     visitors as a stream of events. No Value is built; a caller wanting a
     *     tree should use json::Reader.
     * @param document UTF-8 encoded document.
     * @return @c true if the document was parsed and every visitor accepted
     *     every event, @c false otherwise. See getFormattedErrorMessages().
     */
    // clang-format on
    bool
    parse(std::string document);

    // clang-format off
    /**
     * @brief Report the document in [@a beginDoc, @a endDoc) to the visitors as
     *     a stream of events.
     * @param beginDoc Start of a UTF-8 encoded document owned by the caller,
     *     which must outlive the parse.
     * @param endDoc One past the end of that document.
     * @return @c true if the document was parsed and every visitor accepted
     *     every event, @c false otherwise. See getFormattedErrorMessages().
     */
    // clang-format on
    bool
    parse(char const* beginDoc, char const* endDoc);

    // clang-format off
    /**
     * @brief Read @a is to end of stream, parsing each chunk as it is read.
     * @see parse(std::string).
     */
    // clang-format on
    bool
    parse(std::istream& is);

    // clang-format off
    /**
     * @brief Parse a document split across a buffer sequence, one buffer at a
     *     time, without copying it.
     * @param bs UTF-8 encoded buffer sequence.
     * @see parse(std::string).
     */
    // clang-format on
    template <class BufferSequence>
        requires boost::asio::is_const_buffer_sequence<BufferSequence>::value
    bool
    parse(BufferSequence const& bs);

    // clang-format off
    /**
     * @brief Returns a user friendly string that list errors in the parsed document.
     * @return Formatted error message with the list of errors with
     *     their location in the parsed document. An empty string is returned if no
     *     error occurred during parsing.
     */
    // clang-format on
    [[nodiscard]] std::string
    getFormattedErrorMessages() const;

private:
    // Receives Boost.JSON's SAX events and forwards each one to the Parser,
    // which applies the limits and offers the event to the visitors.
    class Handler;

    using BasicParser = boost::json::basic_parser<Handler>;

    template <class Call>
    bool
    dispatch(Call const& call);

    bool
    beginValue();
    void
    endValue();
    bool
    reject(std::string message);

    bool
    onDocumentBegin();
    bool
    onObjectBegin();
    bool
    onObjectEnd();
    bool
    onArrayBegin();
    bool
    onArrayEnd();
    bool
    onKeyPart(std::string_view part);
    bool
    onKey(std::string_view part);
    bool
    onStringPart(std::string_view part);
    bool
    onString(std::string_view part);
    bool
    onNumberPart(std::string_view part);
    bool
    onInteger(std::int64_t value, std::string_view part);
    bool
    onUnsigned(std::uint64_t value, std::string_view part);
    bool
    onDouble(double value, std::string_view part);
    bool
    onBool(bool value);
    bool
    onNull();
    bool
    onCommentPart(std::string_view part);
    bool
    onComment(std::string_view part);

    bool
    beginText(bool isKey);
    bool
    appendText(std::string_view part, std::size_t sizeLimit, std::string_view what);
    bool
    dispatchInteger(std::int64_t value);

    BasicParser
    start();
    void
    feed(BasicParser& parser, char const* data, std::size_t size, bool more);
    bool
    finish(BasicParser& parser, std::size_t documentSize);
    bool
    documentTooLarge();
    void
    advance(std::string_view consumed);

    bool
    addError(std::string message);
    bool
    addError(std::string message, std::int64_t line, std::int64_t column);
};

template <typename... Visitor>
class Parser<Visitor...>::Handler
{
    Parser& parser_;

public:
    // Holds a reference to its Parser, so it can be neither defaulted,
    // copied, nor moved. basic_parser constructs it in place.
    Handler() = delete;
    Handler(Handler const&) = delete;
    Handler&
    operator=(Handler const&) = delete;
    Handler(Handler&&) = delete;
    Handler&
    operator=(Handler&&) = delete;

    explicit Handler(Parser& parser) : parser_{parser}
    {
    }

    // The members below are Boost.JSON's handler interface, whose names
    // basic_parser requires exactly as written.
    // NOLINTBEGIN(readability-identifier-naming)

    // The Parser enforces its own runtime limits, so Boost's are left open.
    static constexpr std::size_t max_object_size = std::numeric_limits<std::size_t>::max();
    static constexpr std::size_t max_array_size = std::numeric_limits<std::size_t>::max();
    static constexpr std::size_t max_key_size = std::numeric_limits<std::size_t>::max();
    static constexpr std::size_t max_string_size = std::numeric_limits<std::size_t>::max();

    bool
    on_document_begin(boost::system::error_code& ec)
    {
        return check(parser_.onDocumentBegin(), ec);
    }

    // The Parser reports the end of the document itself, once it knows any
    // trailing comments were accepted.
    static bool
    on_document_end(boost::system::error_code&)
    {
        return true;
    }

    bool
    on_object_begin(boost::system::error_code& ec)
    {
        return check(parser_.onObjectBegin(), ec);
    }

    bool
    on_object_end(std::size_t, boost::system::error_code& ec)
    {
        return check(parser_.onObjectEnd(), ec);
    }

    bool
    on_array_begin(boost::system::error_code& ec)
    {
        return check(parser_.onArrayBegin(), ec);
    }

    bool
    on_array_end(std::size_t, boost::system::error_code& ec)
    {
        return check(parser_.onArrayEnd(), ec);
    }

    bool
    on_key_part(boost::json::string_view s, std::size_t, boost::system::error_code& ec)
    {
        return check(parser_.onKeyPart(view(s)), ec);
    }

    bool
    on_key(boost::json::string_view s, std::size_t, boost::system::error_code& ec)
    {
        return check(parser_.onKey(view(s)), ec);
    }

    bool
    on_string_part(boost::json::string_view s, std::size_t, boost::system::error_code& ec)
    {
        return check(parser_.onStringPart(view(s)), ec);
    }

    bool
    on_string(boost::json::string_view s, std::size_t, boost::system::error_code& ec)
    {
        return check(parser_.onString(view(s)), ec);
    }

    bool
    on_number_part(boost::json::string_view s, boost::system::error_code& ec)
    {
        return check(parser_.onNumberPart(view(s)), ec);
    }

    bool
    on_int64(std::int64_t i, boost::json::string_view s, boost::system::error_code& ec)
    {
        return check(parser_.onInteger(i, view(s)), ec);
    }

    bool
    on_uint64(std::uint64_t u, boost::json::string_view s, boost::system::error_code& ec)
    {
        return check(parser_.onUnsigned(u, view(s)), ec);
    }

    bool
    on_double(double d, boost::json::string_view s, boost::system::error_code& ec)
    {
        return check(parser_.onDouble(d, view(s)), ec);
    }

    bool
    on_bool(bool b, boost::system::error_code& ec)
    {
        return check(parser_.onBool(b), ec);
    }

    bool
    on_null(boost::system::error_code& ec)
    {
        return check(parser_.onNull(), ec);
    }

    bool
    on_comment_part(boost::json::string_view s, boost::system::error_code& ec)
    {
        return check(parser_.onCommentPart(view(s)), ec);
    }

    bool
    on_comment(boost::json::string_view s, boost::system::error_code& ec)
    {
        return check(parser_.onComment(view(s)), ec);
    }

    // NOLINTEND(readability-identifier-naming)

private:
    static std::string_view
    view(boost::json::string_view s)
    {
        return {s.data(), s.size()};
    }

    // Boost stops the parse when a handler returns false, but only reports it
    // as an error if the error code is set.
    static bool
    check(bool ok, boost::system::error_code& ec)
    {
        if (!ok)
        {
            ec = boost::system::errc::make_error_code(boost::system::errc::operation_canceled);
        }

        return ok;
    }
};

template <typename... Visitor>
bool
Parser<Visitor...>::parse(std::string document)
{
    return parse(document.data(), document.data() + document.size());
}

template <typename... Visitor>
bool
Parser<Visitor...>::parse(std::istream& sin)
{
    // Bytes read from the stream per read() call.
    static constexpr std::size_t kReadChunkSize = 4096;

    // Reads at most one byte past documentSizeLimit, which is enough for the
    // size check to reject the document without buffering the rest of the
    // stream. Reading continues after parsing stops, so the size check always
    // sees the whole document.
    auto parser = start();
    auto chunk = std::array<char, kReadChunkSize>{};
    auto documentSize = std::size_t{0};

    while (sin && documentSize <= documentSizeLimit)
    {
        // Arranged so that a limit of max() cannot overflow.
        auto const count = std::min(chunk.size() - 1, documentSizeLimit - documentSize) + 1;
        sin.read(chunk.data(), static_cast<std::streamsize>(count));
        auto const read = static_cast<std::size_t>(sin.gcount());
        documentSize += read;

        if (documentSize <= documentSizeLimit)
        {
            feed(parser, chunk.data(), read, true);
        }
    }

    if (documentSize != 0 && sin.eof() && !sin.bad())
    {
        sin.clear(std::ios_base::eofbit);
    }

    if (documentSize > documentSizeLimit)
    {
        return documentTooLarge();
    }

    return finish(parser, documentSize);
}

template <typename... Visitor>
bool
Parser<Visitor...>::parse(char const* beginDoc, char const* endDoc)
{
    auto const documentSize = static_cast<std::size_t>(endDoc - beginDoc);

    if (documentSize > documentSizeLimit)
    {
        return documentTooLarge();
    }

    auto parser = start();
    feed(parser, beginDoc, documentSize, false);
    return finish(parser, documentSize);
}

template <typename... Visitor>
template <class BufferSequence>
    requires boost::asio::is_const_buffer_sequence<BufferSequence>::value
bool
Parser<Visitor...>::parse(BufferSequence const& bs)
{
    auto const documentSize = boost::asio::buffer_size(bs);

    if (documentSize > documentSizeLimit)
    {
        return documentTooLarge();
    }

    auto parser = start();

    for (auto it = boost::asio::buffer_sequence_begin(bs);
         it != boost::asio::buffer_sequence_end(bs);
         ++it)
    {
        auto const buffer = boost::asio::const_buffer{*it};
        feed(parser, static_cast<char const*>(buffer.data()), buffer.size(), true);
    }

    return finish(parser, documentSize);
}

template <typename... Visitor>
Parser<Visitor...>::BasicParser
Parser<Visitor...>::start()
{
    errors_.clear();
    containers_.clear();
    text_.clear();
    inText_ = false;
    comment_.clear();
    rootComplete_ = false;
    rejected_ = false;
    pendingError_.clear();
    stopped_ = false;
    line_ = 1;
    column_ = 1;
    afterCarriageReturn_ = false;

    auto options = boost::json::parse_options{};
    options.allow_comments = true;
    options.allow_trailing_commas = false;
    options.allow_invalid_utf8 = false;
    options.allow_invalid_utf16 = false;
    options.numbers = boost::json::number_precision::precise;
    // Boost counts only containers, and depthLimit allows one more container
    // than that when the innermost is empty (see beginValue). Leaving Boost
    // two levels of headroom lets depthLimit be the one that applies.
    options.max_depth = depthLimit < std::numeric_limits<std::size_t>::max() - 2
        ? depthLimit + 2
        : std::numeric_limits<std::size_t>::max();

    return BasicParser{options, *this};
}

template <typename... Visitor>
void
Parser<Visitor...>::feed(BasicParser& parser, char const* data, std::size_t size, bool more)
{
    if (stopped_)
    {
        return;
    }

    auto ec = boost::system::error_code{};
    auto const consumed = parser.write_some(more, data, size, ec);

    // Boost stops short of the input only once the root value is complete and
    // something other than whitespace or a comment follows it.
    stopped_ = !more || ec || consumed < size;
    advance({data, consumed});

    if (!ec)
    {
        return;
    }

    if (rejected_)
    {
        addError(pendingError_);
    }
    else if (!rootComplete_)
    {
        addError(ec.message());
    }

    // Otherwise Boost failed on whatever follows a complete root value, which
    // is ignored.
}

template <typename... Visitor>
bool
Parser<Visitor...>::finish(BasicParser& parser, std::size_t documentSize)
{
    // Tells Boost the document is complete. A real, if empty, range keeps the
    // pointer arithmetic in a resumed parse well defined.
    static constexpr char kNothing{};
    feed(parser, &kNothing, 0, false);

    if (!errors_.empty())
    {
        return false;
    }

    // onDocumentEnd rejects the document as a whole, so its errors belong at
    // the start rather than at wherever parsing stopped.
    if (!DISPATCH_VISITORS(onDocumentEnd(documentSize)))
    {
        return addError(pendingError_, 1, 1);
    }

    return true;
}

template <typename... Visitor>
bool
Parser<Visitor...>::documentTooLarge()
{
    errors_.clear();
    return addError(
        std::format(
            "Syntax error: document size exceeds the maximum allowed size of {} bytes",
            documentSizeLimit),
        1,
        1);
}

template <typename... Visitor>
void
Parser<Visitor...>::advance(std::string_view consumed)
{
    for (auto const c : consumed)
    {
        if (c == '\r')
        {
            ++line_;
            column_ = 1;
            afterCarriageReturn_ = true;
        }
        else if (c == '\n')
        {
            if (!afterCarriageReturn_)
            {
                ++line_;
                column_ = 1;
            }

            afterCarriageReturn_ = false;
        }
        else
        {
            ++column_;
            afterCarriageReturn_ = false;
        }
    }
}

template <typename... Visitor>
template <class Call>
bool
Parser<Visitor...>::dispatch(Call const& call)
{
    auto ok = true;

    std::apply(
        [&](auto*... visitors) {
            auto const offer = [&](auto* visitor) {
                if constexpr (std::is_invocable_v<Call const&, decltype(visitor)>)
                {
                    if (auto result = call(visitor); !result.has_value())
                    {
                        pendingError_ = std::move(result.error());
                        ok = false;
                    }
                }
            };

            ((ok ? offer(visitors) : void()), ...);
        },
        visitors_);

    if (!ok)
    {
        rejected_ = true;
    }

    return ok;
}

template <typename... Visitor>
bool
Parser<Visitor...>::reject(std::string message)
{
    pendingError_ = std::move(message);
    rejected_ = true;
    return false;
}

template <typename... Visitor>
bool
Parser<Visitor...>::beginValue()
{
    // Checked before the value is parsed, so a container at its limit costs
    // no more work and visitors never see an element that cannot be accepted.
    if (!containers_.empty() && !containers_.back().isObject)
    {
        auto& array = containers_.back();

        if (array.count >= arrayElementsLimit)
        {
            return reject(
                std::format(
                    "Syntax error: array element count exceeds the maximum allowed size of {} "
                    "elements",
                    arrayElementsLimit));
        }

        ++array.count;
    }

    // The root value is depth 0 and every value inside a container is one
    // deeper, so a scalar in the innermost container counts as a level while
    // an empty innermost container adds none.
    if (containers_.size() > depthLimit)
    {
        return reject("Syntax error: maximum nesting depth exceeded");
    }

    return true;
}

template <typename... Visitor>
void
Parser<Visitor...>::endValue()
{
    if (containers_.empty())
    {
        rootComplete_ = true;
    }
}

template <typename... Visitor>
bool
Parser<Visitor...>::onDocumentBegin()
{
    return DISPATCH_VISITORS(onDocumentBegin());
}

template <typename... Visitor>
bool
Parser<Visitor...>::onObjectBegin()
{
    if (!beginValue() || !DISPATCH_VISITORS(onObjectBegin()))
    {
        return false;
    }

    containers_.push_back({.isObject = true});
    return true;
}

template <typename... Visitor>
bool
Parser<Visitor...>::onObjectEnd()
{
    auto const memberCount = containers_.back().count;
    containers_.pop_back();

    if (!DISPATCH_VISITORS(onObjectEnd(memberCount)))
    {
        return false;
    }

    endValue();
    return true;
}

template <typename... Visitor>
bool
Parser<Visitor...>::onArrayBegin()
{
    if (!beginValue() || !DISPATCH_VISITORS(onArrayBegin()))
    {
        return false;
    }

    containers_.push_back({.isObject = false});
    return true;
}

template <typename... Visitor>
bool
Parser<Visitor...>::onArrayEnd()
{
    auto const elementCount = containers_.back().count;
    containers_.pop_back();

    if (!DISPATCH_VISITORS(onArrayEnd(elementCount)))
    {
        return false;
    }

    endValue();
    return true;
}

template <typename... Visitor>
bool
Parser<Visitor...>::beginText(bool isKey)
{
    if (inText_)
    {
        return true;
    }

    inText_ = true;
    text_.clear();

    if (!isKey)
    {
        return beginValue();
    }

    // Checked before the member is dispatched, so visitors never see a member
    // that cannot be accepted.
    auto& object = containers_.back();

    if (object.count >= objectMembersLimit)
    {
        return reject(
            std::format(
                "Syntax error: object member count exceeds the maximum allowed size of {} "
                "members",
                objectMembersLimit));
    }

    ++object.count;
    return true;
}

template <typename... Visitor>
bool
Parser<Visitor...>::appendText(std::string_view part, std::size_t sizeLimit, std::string_view what)
{
    // Boost delivers keys and strings already decoded, so the limit applies to
    // the decoded size, and is checked as each part arrives.
    if (part.size() > sizeLimit - std::min(sizeLimit, text_.size()))
    {
        return reject(
            std::format(
                "Syntax error: {} size exceeds the maximum allowed size of {} bytes",
                what,
                sizeLimit));
    }

    text_.append(part);
    return true;
}

template <typename... Visitor>
bool
Parser<Visitor...>::onKeyPart(std::string_view part)
{
    return beginText(true) && appendText(part, keySizeLimit, "key");
}

template <typename... Visitor>
bool
Parser<Visitor...>::onKey(std::string_view part)
{
    if (!onKeyPart(part))
    {
        return false;
    }

    inText_ = false;
    return DISPATCH_VISITORS(onKey(std::string_view{text_}));
}

template <typename... Visitor>
bool
Parser<Visitor...>::onStringPart(std::string_view part)
{
    return beginText(false) && appendText(part, stringSizeLimit, "string");
}

template <typename... Visitor>
bool
Parser<Visitor...>::onString(std::string_view part)
{
    if (!onStringPart(part))
    {
        return false;
    }

    inText_ = false;

    if (!DISPATCH_VISITORS(onString(std::string_view{text_})))
    {
        return false;
    }

    endValue();
    return true;
}

template <typename... Visitor>
bool
Parser<Visitor...>::onNumberPart(std::string_view part)
{
    if (!beginText(false))
    {
        return false;
    }

    text_.append(part);
    return true;
}

template <typename... Visitor>
bool
Parser<Visitor...>::dispatchInteger(std::int64_t value)
{
    // json::Value holds 32-bit integers, so anything wider is an error rather
    // than a promotion to double. Values that fit an Int are reported as one.
    if (value < Value::kMinInt || value > static_cast<std::int64_t>(Value::kMaxUInt))
    {
        return reject(std::format("'{}' exceeds the allowable range.", text_));
    }

    auto const ok = value <= Value::kMaxInt
        ? DISPATCH_VISITORS(onInt(static_cast<Value::Int>(value)))
        : DISPATCH_VISITORS(onUInt(static_cast<Value::UInt>(value)));

    if (!ok)
    {
        return false;
    }

    endValue();
    return true;
}

template <typename... Visitor>
bool
Parser<Visitor...>::onInteger(std::int64_t value, std::string_view part)
{
    if (!onNumberPart(part))
    {
        return false;
    }

    inText_ = false;
    return dispatchInteger(value);
}

template <typename... Visitor>
bool
Parser<Visitor...>::onUnsigned(std::uint64_t value, std::string_view part)
{
    if (!onNumberPart(part))
    {
        return false;
    }

    inText_ = false;

    if (value > Value::kMaxUInt)
    {
        return reject(std::format("'{}' exceeds the allowable range.", text_));
    }

    return dispatchInteger(static_cast<std::int64_t>(value));
}

template <typename... Visitor>
bool
Parser<Visitor...>::onDouble(double value, std::string_view part)
{
    if (!onNumberPart(part))
    {
        return false;
    }

    inText_ = false;

    // Boost reports an integer as a double when it does not fit 64 bits, and
    // reports -0 as a double to keep its sign. Both were written as integers.
    if (text_.find_first_of(".eE") == std::string::npos)
    {
        if (value == 0)
        {
            return dispatchInteger(0);
        }

        return reject(std::format("'{}' exceeds the allowable range.", text_));
    }

    // Too large a magnitude arrives as infinity. Too small a magnitude arrives
    // as zero, which is only correct if every significant digit was zero.
    auto const significand = std::string_view{text_}.substr(0, text_.find_first_of("eE"));

    if (!std::isfinite(value) ||
        (value == 0 && significand.find_first_of("123456789") != std::string_view::npos))
    {
        return reject(std::format("'{}' is not a number.", text_));
    }

    if (!DISPATCH_VISITORS(onDouble(value)))
    {
        return false;
    }

    endValue();
    return true;
}

template <typename... Visitor>
bool
Parser<Visitor...>::onBool(bool value)
{
    if (!beginValue() || !DISPATCH_VISITORS(onBool(value)))
    {
        return false;
    }

    endValue();
    return true;
}

template <typename... Visitor>
bool
Parser<Visitor...>::onNull()
{
    if (!beginValue() || !DISPATCH_VISITORS(onNull()))
    {
        return false;
    }

    endValue();
    return true;
}

template <typename... Visitor>
bool
Parser<Visitor...>::onCommentPart(std::string_view part)
{
    // When a buffer ends just after a comment's opening '/', Boost.JSON resumes
    // in the next buffer without having reported that '/'. Every comment opens
    // with "//" or "/*", so a first part that does not is missing exactly it.
    if (comment_.empty() && !part.starts_with("//") && !part.starts_with("/*"))
    {
        comment_.push_back('/');
    }

    comment_.append(part);
    return true;
}

template <typename... Visitor>
bool
Parser<Visitor...>::onComment(std::string_view part)
{
    // Only a complete comment reaches the visitors; the parts of a malformed
    // one are dropped with it.
    onCommentPart(part);
    auto const ok = DISPATCH_VISITORS(onComment(std::string_view{comment_}));
    comment_.clear();
    return ok;
}

template <typename... Visitor>
bool
Parser<Visitor...>::addError(std::string message)
{
    return addError(std::move(message), line_, column_);
}

template <typename... Visitor>
bool
Parser<Visitor...>::addError(std::string message, std::int64_t line, std::int64_t column)
{
    errors_.emplace_back();
    auto& info = errors_.back();
    info.line = line;
    info.column = column;
    info.message = std::move(message);
    return false;
}

template <typename... Visitor>
std::string
Parser<Visitor...>::getFormattedErrorMessages() const
{
    auto formattedMessage = std::string{};

    for (auto const& error : errors_)
    {
        formattedMessage +=
            std::format("* Line {}, Column {}\n  {}\n", error.line, error.column, error.message);
    }

    return formattedMessage;
}

}  // namespace json

#undef DISPATCH_VISITORS
