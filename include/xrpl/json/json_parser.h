#pragma once

#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>

#include <boost/asio/buffer.hpp>

#include <fast_float/fast_float.h>  // IWYU pragma: keep
#include <fast_float/parse_number.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <istream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>

namespace json {

/**
 * clang-format off
 * The Visitor is a "concept" that employs duck typing for the interface.
 * This allows the user to create a visitor with only the methods they need
 * along with bypassing introducing a vtable. The following is
 * the complete declaration of what a visitor can implement that the
 * Parser will expect on its interface.  The author can add any
 * additional methods or members to the visitor that they wish.
 *
 * struct VisitorConcept
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
 * clang-format on
 */
#define DISPATCH_VISITORS(__TOKEN__, ...)                                  \
    {                                                                      \
        auto __dispatchResult__ = true;                                    \
        auto __errorString__ = std::string{};                              \
        std::apply(                                                        \
            [&](auto&&... __visitor__) {                                   \
                (([&] {                                                    \
                     if (!__dispatchResult__)                              \
                     {                                                     \
                         return;                                           \
                     }                                                     \
                     if constexpr (requires { __visitor__->__VA_ARGS__; }) \
                     {                                                     \
                         auto __result__ = __visitor__->__VA_ARGS__;       \
                         if (!__result__.has_value())                      \
                         {                                                 \
                             __dispatchResult__ = false;                   \
                             __errorString__ = __result__.error();         \
                         }                                                 \
                     }                                                     \
                 }()),                                                     \
                 ...);                                                     \
            },                                                             \
            visitors_);                                                    \
        if (!__dispatchResult__)                                           \
        {                                                                  \
            return addError(__errorString__, __TOKEN__);                   \
        }                                                                  \
    }

template <typename... Visitor>
class Parser
{
public:
    using Char = char;
    using Location = Char const*;

    /**
     * clang-format off
     * @brief Construct a Parser reporting to @a visitors.
     * @param visitors Retained by reference; each must outlive the Parser.
     *     Anything a visitor accumulates is therefore read back from the
     *     caller's own object once parsing completes.
     * clang-format on
     */
    explicit Parser(Visitor&... visitors) : visitors_{&visitors...}
    {
    }

    Parser(Parser const&) = delete;
    Parser&
    operator=(Parser const&) = delete;
    Parser(Parser&& other) noexcept;
    Parser&
    operator=(Parser&& other) noexcept;

    /**
     * @brief The maximum depth of the JSON document.
     */
    std::size_t depthLimit{25};
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

    /**
     * clang-format off
     * @brief Read a Value from a <a HREF="http://www.json.org">JSON</a> document.
     * @param document UTF-8 encoded string containing the document to read.
     * @return @c true if the document was successfully parsed, @c false if an
     *     error occurred.
     * clang-format on
     */
    bool
    parse(std::string document);

    /**
     * clang-format off
     * @brief Read a Value from a <a HREF="http://www.json.org">JSON</a> document.
     * @param document UTF-8 encoded string containing the document to read.
     * @return @c true if the document was successfully parsed, @c false if an
     *     error occurred.
     * clang-format on
     */
    bool
    parse(char const* beginDoc, char const* endDoc);

    /**
     * clang-format off
     * @brief Parse from input stream.
     * @see json::operator>>(std::istream&, json::Value&).
     * clang-format on
     */
    bool
    parse(std::istream& is);

    /**
     * clang-format off
     * @brief Read a Value from a <a HREF="http://www.json.org">JSON</a> buffer sequence.
     * @param UTF-8 encoded buffer sequence.
     * @return @c true if the buffer was successfully parsed, @c false if an error
     *     occurred.
     * clang-format on
     */
    template <class BufferSequence>
        requires requires(BufferSequence const& buffers) { boost::asio::buffer_size(buffers); }
    bool
    parse(BufferSequence const& bs);

    /**
     * clang-format off
     * @brief Returns a user friendly string that list errors in the parsed document.
     * @return Formatted error message with the list of errors with
     *     their location in the parsed document. An empty string is returned if no
     *     error occurred during parsing.
     * clang-format on
     */
    [[nodiscard]] std::string
    getFormattedErrorMessages() const;

private:
    enum class TokenType {
        EndOfStream = 0,
        ObjectBegin,
        ObjectEnd,
        ArrayBegin,
        ArrayEnd,
        String,
        Integer,
        Double,
        True,
        False,
        Null,
        ArraySeparator,
        MemberSeparator,
        Comment,
        Error
    };

    class Token
    {
    public:
        explicit Token() = default;

        TokenType type{};
        Location start{};
        Location end{};
    };

    class ErrorInfo
    {
    public:
        explicit ErrorInfo() = default;

        Token token{};
        std::string message;
        Location extra{};
    };

    using Errors = std::deque<ErrorInfo>;

    static std::string
    codePointToUTF8(std::uint32_t cp);

    bool
    readToken(Token& token);
    void
    skipSpaces();
    bool
    match(Location pattern, std::int64_t patternLength);
    bool
    readComment();
    bool
    readCStyleComment();
    bool
    readCppStyleComment();
    bool
    readString();
    Parser::TokenType
    readNumber();
    bool
    readValue(std::size_t depth);
    bool
    readObject(Token& token, std::size_t depth);
    bool
    readArray(Token& token, std::size_t depth);
    bool
    decodeNumber(Token& token);
    bool
    decodeString(Token& token);
    bool
    decodeString(Token& token, std::string& decoded);
    bool
    decodeDouble(Token& token);
    bool
    decodeUnicodeCodePoint(Token& token, Location& current, Location end, std::uint32_t& unicode);
    bool
    decodeUnicodeEscapeSequence(
        Token& token,
        Location& current,
        Location end,
        std::uint32_t& unicode);
    bool
    addError(std::string const& message, Token const& token, Location extra = nullptr);
    bool
    recoverFromError(TokenType skipUntilToken);
    bool
    addErrorAndRecover(std::string const& message, Token const& token, TokenType skipUntilToken);
    Char
    getNextChar();
    void
    getLocationLineAndColumn(Location location, int64_t& line, int64_t& column) const;
    std::string
    getLocationLineAndColumn(Location location) const;
    bool
    skipCommentTokens(Token& token);
    void
    rebaseLocations();

    std::tuple<Visitor*...> visitors_;
    Errors errors_;
    std::string document_;
    Location begin_{};
    Location end_{};
    Location current_{};
    // Whether the Locations above point into document_ rather than a caller's
    // buffer. Only the former move with this parser, so only they are rebased.
    bool ownsDocument_{false};
};

template <typename... Visitor>
Parser<Visitor...>::Parser(Parser&& other) noexcept
    : depthLimit{other.depthLimit}
    , documentSizeLimit{other.documentSizeLimit}
    , keySizeLimit{other.keySizeLimit}
    , stringSizeLimit{other.stringSizeLimit}
    , objectMembersLimit{other.objectMembersLimit}
    , arrayElementsLimit{other.arrayElementsLimit}
    , visitors_{std::move(other.visitors_)}
    , errors_{std::move(other.errors_)}
    , document_{std::move(other.document_)}
    , begin_{other.begin_}
    , end_{other.end_}
    , current_{other.current_}
    , ownsDocument_{other.ownsDocument_}
{
    rebaseLocations();
}

template <typename... Visitor>
Parser<Visitor...>&
Parser<Visitor...>::operator=(Parser&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    depthLimit = other.depthLimit;
    documentSizeLimit = other.documentSizeLimit;
    keySizeLimit = other.keySizeLimit;
    stringSizeLimit = other.stringSizeLimit;
    objectMembersLimit = other.objectMembersLimit;
    arrayElementsLimit = other.arrayElementsLimit;
    visitors_ = other.visitors_;
    errors_ = std::move(other.errors_);
    begin_ = other.begin_;
    end_ = other.end_;
    current_ = other.current_;
    ownsDocument_ = other.ownsDocument_;
    document_ = std::move(other.document_);
    rebaseLocations();

    return *this;
}

template <typename... Visitor>
void
Parser<Visitor...>::rebaseLocations()
{
    // A caller's buffer was not relocated by the move.
    if (!ownsDocument_)
    {
        return;
    }

    // begin_ still holds the pre-move base, since parse(std::string) leaves it
    // at document_.data() and the shifts below have not run yet.
    auto const* const oldBase = begin_;
    auto const* const newBase = document_.data();

    if (oldBase == newBase)
    {
        return;
    }

    // Each location is turned into an offset within the old buffer and then
    // reapplied to the new one. Differencing the two bases directly would be
    // pointer arithmetic across unrelated arrays.
    auto const shift = [oldBase, newBase](Location& location) {
        if (location != nullptr)
        {
            location = newBase + (location - oldBase);
        }
    };

    shift(begin_);
    shift(end_);
    shift(current_);

    for (auto& error : errors_)
    {
        shift(error.token.start);
        shift(error.token.end);
        shift(error.extra);
    }
}

template <typename... Visitor>
bool
Parser<Visitor...>::parse(std::string document)
{
    document_ = std::move(document);
    auto const* begin = document_.c_str();
    auto const* end = begin + document_.length();
    auto const successful = parse(begin, end);
    // parse(begin, end) assumes a caller-owned buffer; this overload owns it.
    ownsDocument_ = true;
    return successful;
}

template <typename... Visitor>
bool
Parser<Visitor...>::parse(std::istream& sin)
{
    // std::istream_iterator<char> begin(sin);
    // std::istream_iterator<char> end;
    // Those would allow streamed input from a file, if parse() were a
    // template function.

    // Since std::string is reference-counted, this at least does not
    // create an extra copy.
    auto doc = std::string{};
    std::getline(sin, doc, (char)EOF);
    return parse(std::move(doc));
}

template <typename... Visitor>
bool
Parser<Visitor...>::parse(char const* beginDoc, char const* endDoc)
{
    begin_ = beginDoc;
    end_ = endDoc;
    current_ = begin_;
    errors_.clear();
    ownsDocument_ = false;

    auto documentSize = static_cast<std::size_t>(end_ - begin_);

    auto token = Token{};
    token.start = begin_;
    token.end = begin_;

    if (documentSize > documentSizeLimit)
    {
        return addError(
            "Syntax error: document size exceeds the maximum allowed size of " +
                std::to_string(documentSizeLimit) + " bytes",
            token);
    }

    DISPATCH_VISITORS(token, onDocumentBegin());

    // Trailing comments are skipped, and a visitor rejecting one fails the
    // parse even though the document itself was read successfully.
    auto const successful = readValue(0) && skipCommentTokens(token);

    if (successful)
    {
        // onDocumentEnd rejects the document as a whole, so its errors belong at
        // the start rather than at whatever token parsing stopped on.
        auto documentToken = Token{};
        documentToken.type = TokenType::Error;
        documentToken.start = begin_;
        documentToken.end = end_;

        DISPATCH_VISITORS(documentToken, onDocumentEnd(documentSize));
    }
    return successful;
}

template <typename... Visitor>
template <class BufferSequence>
        requires requires(BufferSequence const& buffers) { boost::asio::buffer_size(buffers); }
bool
Parser<Visitor...>::parse(BufferSequence const& bs)
{
    using namespace boost::asio;
    auto size = buffer_size(bs);
    if (size > documentSizeLimit)
    {
        auto token = Token{};
        token.start = begin_;
        token.end = begin_;
        return addError(
            "Syntax error: document size exceeds the maximum allowed size of " +
                std::to_string(documentSizeLimit) + " bytes",
            token);
    }
    auto s = std::string{};
    s.reserve(size);
    for (auto const& b : bs)
    {
        s.append(static_cast<char const*>(b.data()), size);
    }
    return parse(std::move(s));
}

template <typename... Visitor>
std::string
Parser<Visitor...>::codePointToUTF8(std::uint32_t cp)
{
    auto result = std::string{};

    // based on description from http://en.wikipedia.org/wiki/UTF-8

    if (cp <= 0x7f)
    {
        result.resize(1);
        result[0] = static_cast<char>(cp);
    }
    else if (cp <= 0x7FF)
    {
        result.resize(2);
        result[1] = static_cast<char>(0x80 | (0x3f & cp));
        result[0] = static_cast<char>(0xC0 | (0x1f & (cp >> 6)));
    }
    else if (cp <= 0xFFFF)
    {
        result.resize(3);
        result[2] = static_cast<char>(0x80 | (0x3f & cp));
        result[1] = 0x80 | static_cast<char>((0x3f & (cp >> 6)));
        result[0] = 0xE0 | static_cast<char>((0xf & (cp >> 12)));
    }
    else if (cp <= 0x10FFFF)
    {
        result.resize(4);
        result[3] = static_cast<char>(0x80 | (0x3f & cp));
        result[2] = static_cast<char>(0x80 | (0x3f & (cp >> 6)));
        result[1] = static_cast<char>(0x80 | (0x3f & (cp >> 12)));
        result[0] = static_cast<char>(0xF0 | (0x7 & (cp >> 18)));
    }

    return result;
}

template <typename... Visitor>
Parser<Visitor...>::Char
Parser<Visitor...>::getNextChar()
{
    if (current_ == end_)
    {
        return 0;
    }
    return *current_++;
}

template <typename... Visitor>
void
Parser<Visitor...>::skipSpaces()
{
    while (current_ != end_)
    {
        auto const c = *current_;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
        {
            ++current_;
        }
        else
        {
            break;
        }
    }
}

template <typename... Visitor>
bool
Parser<Visitor...>::readString()
{
    auto c = Char{0};

    while (current_ != end_)
    {
        c = getNextChar();

        if (c == '\\')
        {
            getNextChar();
        }
        else if (c == '"')
        {
            break;
        }
    }

    return c == '"';
}

template <typename... Visitor>
bool
Parser<Visitor...>::readCStyleComment()
{
    while (current_ != end_)
    {
        auto const c = getNextChar();

        if (c == '*' && current_ != end_ && *current_ == '/')
        {
            break;
        }
    }

    return getNextChar() == '/';
}

template <typename... Visitor>
bool
Parser<Visitor...>::readCppStyleComment()
{
    while (current_ != end_)
    {
        auto const c = getNextChar();

        if (c == '\r' || c == '\n')
        {
            break;
        }
    }

    return true;
}

template <typename... Visitor>
bool
Parser<Visitor...>::readComment()
{
    auto const c = getNextChar();

    if (c == '*')
    {
        return readCStyleComment();
    }

    if (c == '/')
    {
        return readCppStyleComment();
    }

    return false;
}

template <typename... Visitor>
Parser<Visitor...>::TokenType
Parser<Visitor...>::readNumber()
{
    static char const kExtendedTokens[] = {'.', 'e', 'E', '+', '-'};

    auto type = TokenType::Integer;

    if (current_ != end_)
    {
        if (*current_ == '-')
        {
            ++current_;
        }

        while (current_ != end_)
        {
            if (std::isdigit(static_cast<unsigned char>(*current_)) == 0)
            {
                auto ret = std::ranges::find(kExtendedTokens, *current_);

                if (ret == std::end(kExtendedTokens))
                {
                    break;
                }

                type = TokenType::Double;
            }

            ++current_;
        }
    }

    return type;
}

template <typename... Visitor>
bool
Parser<Visitor...>::match(Location pattern, std::int64_t patternLength)
{
    if (end_ - current_ < patternLength)
    {
        return false;
    }

    auto index = patternLength;

    while ((index--) != 0)
    {
        if (current_[index] != pattern[index])
        {
            return false;
        }
    }

    current_ += patternLength;
    return true;
}

template <typename... Visitor>
bool
Parser<Visitor...>::readToken(Token& token)
{
    skipSpaces();
    token.start = current_;
    auto const c = getNextChar();
    auto ok = true;

    switch (c)
    {
        case '{': {
            token.type = TokenType::ObjectBegin;
        }
        break;

        case '}': {
            token.type = TokenType::ObjectEnd;
        }
        break;

        case '[': {
            token.type = TokenType::ArrayBegin;
        }
        break;

        case ']': {
            token.type = TokenType::ArrayEnd;
        }
        break;

        case '"': {
            token.type = TokenType::String;
            ok = readString();
        }
        break;

        case '/': {
            token.type = TokenType::Comment;
            ok = readComment();
            DISPATCH_VISITORS(token, onComment(std::string(token.start, current_)));
        }
        break;

        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
        case '-': {
            token.type = readNumber();
        }
        break;

        case 't': {
            token.type = TokenType::True;
            ok = match("rue", 3);
        }
        break;

        case 'f': {
            token.type = TokenType::False;
            ok = match("alse", 4);  // cspell:disable-line
        }
        break;

        case 'n': {
            token.type = TokenType::Null;
            ok = match("ull", 3);
        }
        break;

        case ',': {
            token.type = TokenType::ArraySeparator;
        }
        break;

        case ':': {
            token.type = TokenType::MemberSeparator;
        }
        break;

        case 0: {
            token.type = TokenType::EndOfStream;
        }
        break;

        default: {
            ok = false;
        }
        break;
    }

    if (!ok)
    {
        token.type = TokenType::Error;
    }

    token.end = current_;
    return true;
}

template <typename... Visitor>
bool
Parser<Visitor...>::skipCommentTokens(Token& token)
{
    do
    {
        // A visitor rejecting onComment fails readToken while leaving the type
        // as Comment, so the result has to be checked rather than the type.
        if (!readToken(token))
        {
            return false;
        }
    } while (token.type == TokenType::Comment);

    return true;
}

template <typename... Visitor>
bool
Parser<Visitor...>::readObject(Token& tokenStart, std::size_t depth)
{
    DISPATCH_VISITORS(tokenStart, onObjectBegin());

    auto tokenName = Token{};
    auto name = std::string{};
    auto memberCount = std::size_t{0};

    while (readToken(tokenName))
    {
        bool initialTokenOk = true;

        while (tokenName.type == TokenType::Comment && initialTokenOk)
        {
            initialTokenOk = readToken(tokenName);
        }

        if (!initialTokenOk)
        {
            break;
        }

        if (tokenName.type == TokenType::ObjectEnd && name.empty())  // empty object
        {
            DISPATCH_VISITORS(tokenStart, onObjectEnd(memberCount));
            return true;
        }

        if (tokenName.type != TokenType::String)
        {
            break;
        }

        name.clear();

        if (!decodeString(tokenName, name))
        {
            return recoverFromError(TokenType::ObjectEnd);
        }

        auto colon = Token{};

        if (!readToken(colon) || colon.type != TokenType::MemberSeparator)
        {
            return addErrorAndRecover(
                "Missing ':' after object member name", colon, TokenType::ObjectEnd);
        }

        if (name.size() > keySizeLimit)
        {
            return addError(
                "Syntax error: key size exceeds the maximum allowed size of " +
                    std::to_string(keySizeLimit) + " bytes",
                tokenName);
        }

        DISPATCH_VISITORS(tokenName, onKey(name));

        bool const ok = readValue(depth + 1);

        if (!ok)  // error already set
        {
            return recoverFromError(TokenType::ObjectEnd);
        }

        if (++memberCount > objectMembersLimit)
        {
            return addError(
                "Syntax error: object member count exceeds the maximum allowed size of " +
                    std::to_string(objectMembersLimit) + " members",
                tokenName);
        }

        auto comma = Token{};

        if (!readToken(comma) ||
            (comma.type != TokenType::ObjectEnd && comma.type != TokenType::ArraySeparator &&
             comma.type != TokenType::Comment))
        {
            return addErrorAndRecover(
                "Missing ',' or '}' in object declaration", comma, TokenType::ObjectEnd);
        }

        auto finalizeTokenOk = true;

        while (comma.type == TokenType::Comment && finalizeTokenOk)
        {
            finalizeTokenOk = readToken(comma);
        }

        if (!finalizeTokenOk)
        {
            return recoverFromError(TokenType::ObjectEnd);
        }

        if (comma.type == TokenType::ObjectEnd)
        {
            DISPATCH_VISITORS(comma, onObjectEnd(memberCount));
            return true;
        }
    }

    return addErrorAndRecover("Missing '}' or object member name", tokenName, TokenType::ObjectEnd);
}

template <typename... Visitor>
bool
Parser<Visitor...>::readArray(Token& tokenStart, std::size_t depth)
{
    DISPATCH_VISITORS(tokenStart, onArrayBegin());

    auto elementCount = std::size_t{0};

    skipSpaces();

    if (current_ != end_ && *current_ == ']')  // empty array
    {
        auto endArray = Token{};
        readToken(endArray);
        DISPATCH_VISITORS(endArray, onArrayEnd(elementCount));
        return true;
    }

    while (true)
    {
        bool ok = readValue(depth + 1);

        if (!ok)  // error already set
        {
            return recoverFromError(TokenType::ArrayEnd);
        }

        if (++elementCount > arrayElementsLimit)
        {
            return addError(
                "Syntax error: array element count exceeds the maximum allowed size of " +
                    std::to_string(arrayElementsLimit) + " elements",
                tokenStart);
        }

        auto token = Token{};

        // Accept Comment after last item in the array.
        ok = readToken(token);

        while (token.type == TokenType::Comment && ok)
        {
            ok = readToken(token);
        }

        bool const badTokenType =
            (token.type != TokenType::ArraySeparator && token.type != TokenType::ArrayEnd);

        if (!ok || badTokenType)
        {
            return addErrorAndRecover(
                "Missing ',' or ']' in array declaration", token, TokenType::ArrayEnd);
        }

        if (token.type == TokenType::ArrayEnd)
        {
            DISPATCH_VISITORS(token, onArrayEnd(elementCount));
            break;
        }
    }

    return true;
}

template <typename... Visitor>
bool
Parser<Visitor...>::decodeNumber(Token& token)
{
    Location current = token.start;
    bool const isNegative = *current == '-';

    if (isNegative)
    {
        ++current;
    }

    if (current == token.end)
    {
        return addError(
            "'" + std::string(token.start, token.end) + "' is not a valid number.", token);
    }

    // The existing Json integers are 32-bit so using a 64-bit value here avoids
    // overflows in the conversion code below.
    auto value = std::int64_t{};

    static_assert(
        sizeof(value) > sizeof(Value::kMaxUInt),
        "The JSON integer overflow logic will need to be reworked.");

    while (current < token.end && (value <= Value::kMaxUInt))
    {
        Char const c = *current++;

        if (c < '0' || c > '9')
        {
            return addError(
                "'" + std::string(token.start, token.end) + "' is not a number.", token);
        }

        value = (value * 10) + (c - '0');
    }

    // More tokens left -> input is larger than largest possible return value
    if (current != token.end)
    {
        return addError(
            "'" + std::string(token.start, token.end) + "' exceeds the allowable range.", token);
    }

    if (isNegative)
    {
        value = -value;

        if (value < Value::kMinInt || value > Value::kMaxInt)
        {
            return addError(
                "'" + std::string(token.start, token.end) + "' exceeds the allowable range.",
                token);
        }

        DISPATCH_VISITORS(token, onInt(static_cast<Value::Int>(value)));
    }
    else
    {
        if (value > Value::kMaxUInt)
        {
            return addError(
                "'" + std::string(token.start, token.end) + "' exceeds the allowable range.",
                token);
        }

        // If it's representable as a signed integer, construct it as one.
        if (value <= Value::kMaxInt)
        {
            DISPATCH_VISITORS(token, onInt(static_cast<Value::Int>(value)));
        }
        else
        {
            DISPATCH_VISITORS(token, onUInt(static_cast<Value::UInt>(value)));
        }
    }

    return true;
}

template <typename... Visitor>
bool
Parser<Visitor...>::decodeDouble(Token& token)
{
    // Sanity check to avoid buffer overflow exploits.
    if (token.end < token.start)
    {
        return addError("Unable to parse token length", token);
    }

    auto value = double{};
    auto const [ptr, ec] = fast_float::from_chars(token.start, token.end, value);

    // Reject anything from_chars could not turn into a finite double:
    //   - ec != std::errc{}: no valid conversion, or an out-of-range magnitude
    //     (e.g. 1e400).
    //   - ptr != token.end: readNumber() is permissive about which characters
    //     it collects into a token (it will, for example, keep a '+' mid-token),
    //     but from_chars() will stop at the first character it cannot parse.
    if (ec != std::errc{} || ptr != token.end)
    {
        return addError("'" + std::string(token.start, token.end) + "' is not a number.", token);
    }

    DISPATCH_VISITORS(token, onDouble(value));

    return true;
}

template <typename... Visitor>
bool
Parser<Visitor...>::decodeString(Token& token)
{
    auto decoded = std::string{};
    if (!decodeString(token, decoded))
    {
        return false;
    }

    if (decoded.size() > stringSizeLimit)
    {
        return addError(
            "Syntax error: string size exceeds the maximum allowed size of " +
                std::to_string(stringSizeLimit) + " bytes",
            token);
    }

    DISPATCH_VISITORS(token, onString(decoded));

    return true;
}

template <typename... Visitor>
bool
Parser<Visitor...>::decodeString(Token& token, std::string& decoded)
{
    decoded.reserve(token.end - token.start - 2);
    Location current = token.start + 1;  // skip '"'
    Location end = token.end - 1;        // do not include '"'

    while (current != end)
    {
        Char const c = *current++;

        if (c == '"')
        {
            break;
        }
        if (c == '\\')
        {
            if (current == end)
                return addError("Empty escape sequence in string", token, current);

            auto const escape = *current++;

            switch (escape)
            {
                case '"':
                    decoded += '"';
                    break;

                case '/':
                    decoded += '/';
                    break;

                case '\\':
                    decoded += '\\';
                    break;

                case 'b':
                    decoded += '\b';
                    break;

                case 'f':
                    decoded += '\f';
                    break;

                case 'n':
                    decoded += '\n';
                    break;

                case 'r':
                    decoded += '\r';
                    break;

                case 't':
                    decoded += '\t';
                    break;

                case 'u': {
                    auto unicode = std::uint32_t{};

                    if (!decodeUnicodeCodePoint(token, current, end, unicode))
                        return false;

                    decoded += codePointToUTF8(unicode);
                }
                break;

                default:
                    return addError("Bad escape sequence in string", token, current);
            }
        }
        else
        {
            decoded += c;
        }
    }

    return true;
}

template <typename... Visitor>
bool
Parser<Visitor...>::readValue(std::size_t depth)
{
    auto token = Token{};

    if (!skipCommentTokens(token))
    {
        return false;  // error already recorded
    }

    if (depth > depthLimit)
    {
        return addError("Syntax error: maximum nesting depth exceeded", token);
    }
    auto successful = true;

    switch (token.type)
    {
        case TokenType::ObjectBegin:
            successful = readObject(token, depth);
            break;

        case TokenType::ArrayBegin:
            successful = readArray(token, depth);
            break;

        case TokenType::Integer:
            successful = decodeNumber(token);
            break;

        case TokenType::Double:
            successful = decodeDouble(token);
            break;

        case TokenType::String:
            successful = decodeString(token);
            break;

        case TokenType::True: {
            DISPATCH_VISITORS(token, onBool(true));
        }
        break;

        case TokenType::False: {
            DISPATCH_VISITORS(token, onBool(false));
        }
        break;

        case TokenType::Null: {
            DISPATCH_VISITORS(token, onNull());
        }
        break;

        default:
            return addError("Syntax error: value, object or array expected.", token);
    }

    return successful;
}

template <typename... Visitor>
bool
Parser<Visitor...>::decodeUnicodeCodePoint(
    Token& token,
    Location& current,
    Location end,
    std::uint32_t& unicode)
{
    if (!decodeUnicodeEscapeSequence(token, current, end, unicode))
    {
        return false;
    }

    // A trailing surrogate has no leading surrogate to pair with, and encoding
    // it verbatim would emit invalid UTF-8.
    if (unicode >= 0xDC00 && unicode <= 0xDFFF)
    {
        return addError("unpaired trailing surrogate in unicode escape sequence.", token, current);
    }

    if (unicode >= 0xD800 && unicode <= 0xDBFF)
    {
        // surrogate pairs
        if (end - current < 6)
        {
            return addError(
                "additional six characters expected to parse unicode surrogate "
                "pair.",
                token,
                current);
        }

        auto surrogatePair = std::uint32_t{};

        if (*current != '\\' || *(current + 1) != 'u')
        {
            return addError(
                "expecting another \\u token to begin the second half of a unicode surrogate pair",
                token,
                current);
        }

        current += 2;  // skip two characters checked above

        if (!decodeUnicodeEscapeSequence(token, current, end, surrogatePair))
        {
            return false;
        }

        // Only a trailing surrogate completes the pair; anything else would
        // silently compute the wrong code point.
        if (surrogatePair < 0xDC00 || surrogatePair > 0xDFFF)
        {
            return addError(
                "expecting a trailing surrogate to complete the unicode surrogate pair",
                token,
                current);
        }

        unicode = 0x10000 + ((unicode & 0x3FF) << 10) + (surrogatePair & 0x3FF);
    }

    return true;
}

template <typename... Visitor>
bool
Parser<Visitor...>::decodeUnicodeEscapeSequence(
    Token& token,
    Location& current,
    Location end,
    std::uint32_t& unicode)
{
    if (end - current < 4)
    {
        return addError(
            "Bad unicode escape sequence in string: four digits expected.", token, current);
    }

    unicode = 0;

    for (std::uint8_t index = 0; index < 4; ++index)
    {
        auto const c = *current++;
        unicode *= 16;

        if (c >= '0' && c <= '9')
        {
            unicode += c - '0';
        }
        else if (c >= 'a' && c <= 'f')
        {
            unicode += c - 'a' + 10;
        }
        else if (c >= 'A' && c <= 'F')
        {
            unicode += c - 'A' + 10;
        }
        else
        {
            return addError(
                "Bad unicode escape sequence in string: hexadecimal digit "
                "expected.",
                token,
                current);
        }
    }

    return true;
}

template <typename... Visitor>
bool
Parser<Visitor...>::addError(std::string const& message, Token const& token, Location extra)
{
    errors_.emplace_back();
    auto& info = errors_.back();
    info.token = token;
    info.message = message;
    info.extra = extra;
    return false;
}

template <typename... Visitor>
bool
Parser<Visitor...>::recoverFromError(TokenType skipUntilToken)
{
    auto const errorCount = errors_.size();
    auto skip = Token{};

    while (true)
    {
        if (!readToken(skip))
        {
            errors_.resize(errorCount);  // discard errors caused by recovery
        }

        if (skip.type == skipUntilToken || skip.type == TokenType::EndOfStream)
        {
            break;
        }
    }

    errors_.resize(errorCount);
    return false;
}

template <typename... Visitor>
bool
Parser<Visitor...>::addErrorAndRecover(
    std::string const& message,
    Token const& token,
    TokenType skipUntilToken)
{
    addError(message, token);
    return recoverFromError(skipUntilToken);
}

template <typename... Visitor>
void
Parser<Visitor...>::getLocationLineAndColumn(Location location, int64_t& line, int64_t& column)
    const
{
    if (begin_ == nullptr || location == nullptr)
    {
        line = 1;
        column = 1;
        return;
    }

    auto current = begin_;
    auto lastLineStart = current;
    line = 0;

    while (current < location && current != end_)
    {
        auto const c = *current++;

        if (c == '\r')
        {
            if (current != end_ && *current == '\n')
            {
                ++current;
            }

            lastLineStart = current;
            ++line;
        }
        else if (c == '\n')
        {
            lastLineStart = current;
            ++line;
        }
    }

    // column & line start at 1
    column = int64_t{location - lastLineStart} + 1;
    ++line;
}

template <typename... Visitor>
std::string
Parser<Visitor...>::getLocationLineAndColumn(Location location) const
{
    auto line = int64_t{};
    auto column = int64_t{};
    getLocationLineAndColumn(location, line, column);
    return "Line " + std::to_string(line) + ", Column " + std::to_string(column);
}

template <typename... Visitor>
std::string
Parser<Visitor...>::getFormattedErrorMessages() const
{
    auto formattedMessage = std::string{};

    for (auto const& error : errors_)
    {
        formattedMessage += "* " + getLocationLineAndColumn(error.token.start) + "\n";
        formattedMessage += "  " + error.message + "\n";

        if (error.extra != nullptr)
        {
            formattedMessage += "See " + getLocationLineAndColumn(error.extra) + " for detail.\n";
        }
    }

    return formattedMessage;
}

}  // namespace json

#undef DISPATCH_VISITORS
