#pragma once

#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>

#include <boost/asio/buffer.hpp>

#include <fast_float/fast_float.h>  // IWYU pragma: keep
#include <fast_float/parse_number.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <format>
#include <ios>
#include <istream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>

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
#define DISPATCH_VISITORS(TOKEN, CALL)                                                \
    {                                                                                 \
        auto dispatchVisitorsOk = true;                                               \
        auto dispatchVisitorsError = std::string{};                                   \
        std::apply(                                                                   \
            [&](auto&&... dispatchVisitorsVisitor) {                                  \
                (([&] {                                                               \
                     if (!dispatchVisitorsOk)                                         \
                     {                                                                \
                         return;                                                      \
                     }                                                                \
                     if constexpr (requires { dispatchVisitorsVisitor->CALL; })       \
                     {                                                                \
                         auto dispatchVisitorsResult = dispatchVisitorsVisitor->CALL; \
                         if (!dispatchVisitorsResult.has_value())                     \
                         {                                                            \
                             dispatchVisitorsOk = false;                              \
                             dispatchVisitorsError = dispatchVisitorsResult.error();  \
                         }                                                            \
                     }                                                                \
                 }()),                                                                \
                 ...);                                                                \
            },                                                                        \
            visitors_);                                                               \
        if (!dispatchVisitorsOk)                                                      \
        {                                                                             \
            return addError(dispatchVisitorsError, TOKEN);                            \
        }                                                                             \
    }

template <typename... Visitor>
class Parser
{
public:
    using Char = char;
    using Location = Char const*;

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

    Parser(Parser const&) = delete;
    Parser&
    operator=(Parser const&) = delete;
    Parser(Parser&& other) noexcept;
    Parser&
    operator=(Parser&& other) noexcept;

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
     * @param document UTF-8 encoded document. Taken by value and retained for
     *     the lifetime of the parse, so error locations point into it.
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
     *     which must outlive both the parse and any error messages read back
     *     from it.
     * @param endDoc One past the end of that document.
     * @return @c true if the document was parsed and every visitor accepted
     *     every event, @c false otherwise. See getFormattedErrorMessages().
     */
    // clang-format on
    bool
    parse(char const* beginDoc, char const* endDoc);

    // clang-format off
    /**
     * @brief Read @a is to end of stream and parse it.
     * @see parse(std::string).
     */
    // clang-format on
    bool
    parse(std::istream& is);

    // clang-format off
    /**
     * @brief Flatten a buffer sequence into one document and parse it.
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
    // The number of hex digits in a \uXXXX escape.
    static constexpr std::ptrdiff_t kUnicodeEscapeDigits = 4;

    enum class TokenType {
        EndOfStream = 0,
        ObjectBegin = 1,
        ObjectEnd = 2,
        ArrayBegin = 3,
        ArrayEnd = 4,
        String = 5,
        Integer = 6,
        Double = 7,
        True = 8,
        False = 9,
        Null = 10,
        ArraySeparator = 11,
        MemberSeparator = 12,
        Comment = 13,
        Error = 14,
    };

    class Token
    {
    public:
        explicit Token() = default;

        TokenType type{};
        Location start{};
        Location end{};

        // The characters of the token as written in the document.
        [[nodiscard]] std::string_view
        text() const
        {
            return {start, static_cast<std::size_t>(end - start)};
        }
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
    match(std::string_view pattern);
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
    decodeString(Token& token, std::string& decoded, std::size_t sizeLimit, std::string_view what);
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
    getLocationLineAndColumn(Location location, std::int64_t& line, std::int64_t& column) const;
    std::string
    getLocationLineAndColumn(Location location) const;
    bool
    skipCommentTokens(Token& token);

    std::tuple<Visitor*...> visitors_;
    Errors errors_;
    // Held by pointer so that moving the parser never relocates the characters
    // the Locations below (and those in errors_) may point into.
    std::unique_ptr<std::string> document_;
    Location begin_{};
    Location end_{};
    Location current_{};
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
{
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
    document_ = std::move(other.document_);

    return *this;
}

template <typename... Visitor>
bool
Parser<Visitor...>::parse(std::string document)
{
    if (document_)
    {
        *document_ = std::move(document);
    }
    else
    {
        document_ = std::make_unique<std::string>(std::move(document));
    }

    return parse(document_->data(), document_->data() + document_->size());
}

template <typename... Visitor>
bool
Parser<Visitor...>::parse(std::istream& sin)
{
    // Bytes read from the stream per read() call.
    static constexpr std::size_t kReadChunkSize = 4096;

    // Reads at most one byte past documentSizeLimit, which is enough for the
    // size check to reject the document without buffering the rest of the
    // stream.
    auto doc = std::string{};
    auto chunk = std::array<char, kReadChunkSize>{};

    while (sin && doc.size() <= documentSizeLimit)
    {
        // Arranged so that a limit of max() cannot overflow.
        auto const count = std::min(chunk.size() - 1, documentSizeLimit - doc.size()) + 1;
        sin.read(chunk.data(), static_cast<std::streamsize>(count));
        doc.append(chunk.data(), static_cast<std::size_t>(sin.gcount()));
    }

    if (!doc.empty() && sin.eof() && !sin.bad())
    {
        sin.clear(std::ios_base::eofbit);
    }

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

    auto documentSize = static_cast<std::size_t>(end_ - begin_);

    auto token = Token{};
    token.start = begin_;
    token.end = begin_;

    if (documentSize > documentSizeLimit)
    {
        return addError(
            std::format(
                "Syntax error: document size exceeds the maximum allowed size of {} bytes",
                documentSizeLimit),
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
    requires boost::asio::is_const_buffer_sequence<BufferSequence>::value
bool
Parser<Visitor...>::parse(BufferSequence const& bs)
{
    using namespace boost::asio;
    auto const size = buffer_size(bs);
    if (size > documentSizeLimit)
    {
        errors_.clear();
        auto const token = Token{};
        return addError(
            std::format(
                "Syntax error: document size exceeds the maximum allowed size of {} bytes",
                documentSizeLimit),
            token);
    }
    auto s = std::string{};
    s.reserve(size);
    for (auto const& b : bs)
    {
        s.append(static_cast<char const*>(b.data()), buffer_size(b));
    }
    return parse(std::move(s));
}

template <typename... Visitor>
std::string
Parser<Visitor...>::codePointToUTF8(std::uint32_t cp)
{
    // The largest code point each sequence length can carry, the lead byte
    // marker and payload mask for each length, and the marker and payload of a
    // continuation byte.
    static constexpr std::uint32_t kMaxOneByteCodePoint = 0x7F;
    static constexpr std::uint32_t kMaxTwoByteCodePoint = 0x7FF;
    static constexpr std::uint32_t kMaxThreeByteCodePoint = 0xFFFF;
    static constexpr std::uint32_t kMaxCodePoint = 0x10FFFF;
    static constexpr std::uint32_t kTwoByteLead = 0xC0;
    static constexpr std::uint32_t kTwoByteLeadMask = 0x1F;
    static constexpr std::uint32_t kThreeByteLead = 0xE0;
    static constexpr std::uint32_t kThreeByteLeadMask = 0x0F;
    static constexpr std::uint32_t kFourByteLead = 0xF0;
    static constexpr std::uint32_t kFourByteLeadMask = 0x07;
    static constexpr std::uint32_t kContinuationByte = 0x80;
    static constexpr std::uint32_t kContinuationMask = 0x3F;
    static constexpr std::uint32_t kContinuationBits = 6;

    auto result = std::string{};

    // based on description from http://en.wikipedia.org/wiki/UTF-8

    // The payload of the continuation byte carrying bits [shift, shift + 6).
    auto const continuation = [cp](std::uint32_t shift) {
        return static_cast<char>(kContinuationByte | (kContinuationMask & (cp >> shift)));
    };

    if (cp <= kMaxOneByteCodePoint)
    {
        result.resize(1);
        result[0] = static_cast<char>(cp);
    }
    else if (cp <= kMaxTwoByteCodePoint)
    {
        result.resize(2);
        result[1] = continuation(0);
        result[0] =
            static_cast<char>(kTwoByteLead | (kTwoByteLeadMask & (cp >> kContinuationBits)));
    }
    else if (cp <= kMaxThreeByteCodePoint)
    {
        result.resize(3);
        result[2] = continuation(0);
        result[1] = continuation(kContinuationBits);
        result[0] = static_cast<char>(
            kThreeByteLead | (kThreeByteLeadMask & (cp >> (2 * kContinuationBits))));
    }
    else if (cp <= kMaxCodePoint)
    {
        result.resize(4);
        result[3] = continuation(0);
        result[2] = continuation(kContinuationBits);
        result[1] = continuation(2 * kContinuationBits);
        result[0] = static_cast<char>(
            kFourByteLead | (kFourByteLeadMask & (cp >> (3 * kContinuationBits))));
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
                if (!std::ranges::contains(kExtendedTokens, *current_))
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
Parser<Visitor...>::match(std::string_view pattern)
{
    auto const patternLength = static_cast<std::ptrdiff_t>(pattern.size());

    if (end_ - current_ < patternLength)
    {
        return false;
    }

    if (std::string_view(current_, pattern.size()) != pattern)
    {
        return false;
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
            if (ok)
            {
                DISPATCH_VISITORS(token, onComment(std::string(token.start, current_)));
            }
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
            ok = match("rue");
        }
        break;

        case 'f': {
            token.type = TokenType::False;
            ok = match("alse");  // cspell:disable-line
        }
        break;

        case 'n': {
            token.type = TokenType::Null;
            ok = match("ull");
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

        if (tokenName.type == TokenType::ObjectEnd && memberCount == 0)  // empty object
        {
            DISPATCH_VISITORS(tokenName, onObjectEnd(memberCount));
            return true;
        }

        if (tokenName.type != TokenType::String)
        {
            break;
        }

        // Checked before the member is decoded or dispatched, so an object at
        // its limit costs no more work and visitors never see a member that
        // cannot be accepted.
        if (memberCount >= objectMembersLimit)
        {
            return addError(
                std::format(
                    "Syntax error: object member count exceeds the maximum allowed size of {} "
                    "members",
                    objectMembersLimit),
                tokenName);
        }

        name.clear();

        if (!decodeString(tokenName, name, keySizeLimit, "key"))
        {
            return recoverFromError(TokenType::ObjectEnd);
        }

        auto colon = Token{};

        if (!readToken(colon) || colon.type != TokenType::MemberSeparator)
        {
            return addErrorAndRecover(
                "Missing ':' after object member name", colon, TokenType::ObjectEnd);
        }

        DISPATCH_VISITORS(tokenName, onKey(name));

        bool const ok = readValue(depth + 1);

        if (!ok)  // error already set
        {
            return recoverFromError(TokenType::ObjectEnd);
        }

        ++memberCount;

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
        // Checked before the element is parsed, for the same reason as the
        // object member limit in readObject.
        if (elementCount >= arrayElementsLimit)
        {
            return addError(
                std::format(
                    "Syntax error: array element count exceeds the maximum allowed size of {} "
                    "elements",
                    arrayElementsLimit),
                tokenStart);
        }

        bool ok = readValue(depth + 1);

        if (!ok)  // error already set
        {
            return recoverFromError(TokenType::ArrayEnd);
        }

        ++elementCount;

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
    static constexpr std::int64_t kDecimalBase = 10;

    Location current = token.start;
    bool const isNegative = *current == '-';

    if (isNegative)
    {
        ++current;
    }

    if (current == token.end)
    {
        return addError(std::format("'{}' is not a valid number.", token.text()), token);
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
            return addError(std::format("'{}' is not a number.", token.text()), token);
        }

        value = (value * kDecimalBase) + (c - '0');
    }

    // More tokens left -> input is larger than largest possible return value
    if (current != token.end)
    {
        return addError(std::format("'{}' exceeds the allowable range.", token.text()), token);
    }

    if (isNegative)
    {
        value = -value;

        if (value < Value::kMinInt || value > Value::kMaxInt)
        {
            return addError(std::format("'{}' exceeds the allowable range.", token.text()), token);
        }

        DISPATCH_VISITORS(token, onInt(static_cast<Value::Int>(value)));
    }
    else
    {
        if (value > Value::kMaxUInt)
        {
            return addError(std::format("'{}' exceeds the allowable range.", token.text()), token);
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
        return addError(std::format("'{}' is not a number.", token.text()), token);
    }

    DISPATCH_VISITORS(token, onDouble(value));

    return true;
}

template <typename... Visitor>
bool
Parser<Visitor...>::decodeString(Token& token)
{
    auto decoded = std::string{};
    if (!decodeString(token, decoded, stringSizeLimit, "string"))
    {
        return false;
    }

    DISPATCH_VISITORS(token, onString(decoded));

    return true;
}

template <typename... Visitor>
bool
Parser<Visitor...>::decodeString(
    Token& token,
    std::string& decoded,
    std::size_t sizeLimit,
    std::string_view what)
{
    decoded.reserve(std::min(static_cast<std::size_t>(token.end - token.start - 2), sizeLimit));
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

        if (decoded.size() > sizeLimit)
        {
            return addError(
                std::format(
                    "Syntax error: {} size exceeds the maximum allowed size of {} bytes",
                    what,
                    sizeLimit),
                token);
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
    static constexpr std::uint32_t kLeadingSurrogateMin = 0xD800;
    static constexpr std::uint32_t kLeadingSurrogateMax = 0xDBFF;
    static constexpr std::uint32_t kTrailingSurrogateMin = 0xDC00;
    static constexpr std::uint32_t kTrailingSurrogateMax = 0xDFFF;
    static constexpr std::uint32_t kSurrogateMask = 0x3FF;
    static constexpr std::uint32_t kSurrogateBits = 10;
    static constexpr std::uint32_t kSupplementaryPlaneBase = 0x10000;
    // A second escape: the "\u" prefix followed by the hex digits.
    static constexpr std::ptrdiff_t kUnicodeEscapePrefixLength = 2;
    static constexpr std::ptrdiff_t kUnicodeEscapeLength =
        kUnicodeEscapePrefixLength + kUnicodeEscapeDigits;

    if (!decodeUnicodeEscapeSequence(token, current, end, unicode))
    {
        return false;
    }

    // A trailing surrogate has no leading surrogate to pair with, and encoding
    // it verbatim would emit invalid UTF-8.
    if (unicode >= kTrailingSurrogateMin && unicode <= kTrailingSurrogateMax)
    {
        return addError("unpaired trailing surrogate in unicode escape sequence.", token, current);
    }

    if (unicode >= kLeadingSurrogateMin && unicode <= kLeadingSurrogateMax)
    {
        // surrogate pairs
        if (end - current < kUnicodeEscapeLength)
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

        current += kUnicodeEscapePrefixLength;  // skip the "\u" checked above

        if (!decodeUnicodeEscapeSequence(token, current, end, surrogatePair))
        {
            return false;
        }

        // Only a trailing surrogate completes the pair; anything else would
        // silently compute the wrong code point.
        if (surrogatePair < kTrailingSurrogateMin || surrogatePair > kTrailingSurrogateMax)
        {
            return addError(
                "expecting a trailing surrogate to complete the unicode surrogate pair",
                token,
                current);
        }

        unicode = kSupplementaryPlaneBase + ((unicode & kSurrogateMask) << kSurrogateBits) +
            (surrogatePair & kSurrogateMask);
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
    static constexpr std::uint32_t kHexBase = 16;
    // The value of the hex digit 'a' / 'A'.
    static constexpr std::uint32_t kHexLetterOffset = 10;

    if (end - current < kUnicodeEscapeDigits)
    {
        return addError(
            "Bad unicode escape sequence in string: four digits expected.", token, current);
    }

    unicode = 0;

    for (std::ptrdiff_t index = 0; index < kUnicodeEscapeDigits; ++index)
    {
        auto const c = *current++;
        unicode *= kHexBase;

        if (c >= '0' && c <= '9')
        {
            unicode += c - '0';
        }
        else if (c >= 'a' && c <= 'f')
        {
            unicode += c - 'a' + kHexLetterOffset;
        }
        else if (c >= 'A' && c <= 'F')
        {
            unicode += c - 'A' + kHexLetterOffset;
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
Parser<Visitor...>::getLocationLineAndColumn(
    Location location,
    std::int64_t& line,
    std::int64_t& column) const
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
    column = std::int64_t{location - lastLineStart} + 1;
    ++line;
}

template <typename... Visitor>
std::string
Parser<Visitor...>::getLocationLineAndColumn(Location location) const
{
    auto line = std::int64_t{};
    auto column = std::int64_t{};
    getLocationLineAndColumn(location, line, column);
    return std::format("Line {}, Column {}", line, column);
}

template <typename... Visitor>
std::string
Parser<Visitor...>::getFormattedErrorMessages() const
{
    auto formattedMessage = std::string{};

    for (auto const& error : errors_)
    {
        formattedMessage +=
            std::format("* {}\n  {}\n", getLocationLineAndColumn(error.token.start), error.message);

        if (error.extra != nullptr)
        {
            formattedMessage +=
                std::format("See {} for detail.\n", getLocationLineAndColumn(error.extra));
        }
    }

    return formattedMessage;
}

}  // namespace json

#undef DISPATCH_VISITORS
