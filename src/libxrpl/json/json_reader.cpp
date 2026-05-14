/** @file
 *  Recursive-descent JSON parser implementation for the XRP Ledger.
 *
 *  Implements `Json::Reader`, the sole JSON deserialization component in
 *  libxrpl. Every inbound JSON document — RPC requests, configuration files,
 *  and test fixtures — passes through this parser. The implementation follows
 *  a classic lexer + recursive-descent design: `readToken()` classifies source
 *  bytes into `TokenType` values, and `readValue()` / `readObject()` /
 *  `readArray()` drive the lexer and populate a `Json::Value` tree.
 *
 *  Two deliberate deviations from the JSON specification are worth noting:
 *  - The document root must be an object or array (RFC 4627 semantics).
 *  - Duplicate object keys are rejected outright rather than shadowed.
 *
 *  Both constraints are intentional hardening for a network-facing service.
 */

#include <xrpl/json/json_reader.h>

#include <xrpl/basics/contract.h>
#include <xrpl/json/json_value.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <istream>
#include <stdexcept>
#include <string>

namespace json {

/** Encode a Unicode scalar value as a UTF-8 byte sequence.
 *
 *  Covers all four byte-length cases defined by RFC 3629:
 *  U+0000–U+007F (1 byte), U+0080–U+07FF (2 bytes),
 *  U+0800–U+FFFF (3 bytes), and U+10000–U+10FFFF (4 bytes).
 *  Values above U+10FFFF produce an empty string.
 *
 *  @param cp Unicode code point to encode.
 *  @return UTF-8 encoded string of 1–4 bytes, or an empty string if
 *      `cp` exceeds U+10FFFF.
 */
static std::string
codePointToUTF8(unsigned int cp)
{
    std::string result;

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

// --- Reader implementation ---

/** Parse a UTF-8 JSON document from a `std::string`.
 *
 *  Copies `document` into `document_` so the source lifetime is managed
 *  internally, then delegates to the `char const*` overload.
 *
 *  @param document UTF-8 encoded JSON text to parse.
 *  @param root     Output value; populated on success, state unspecified
 *      on failure.
 *  @return `true` if the document was parsed without errors.
 */
bool
Reader::parse(std::string const& document, Value& root)
{
    document_ = document;
    char const* begin = document_.c_str();
    char const* end = begin + document_.length();
    return parse(begin, end, root);
}

/** Parse a JSON document read from an input stream.
 *
 *  Slurps the entire stream into a `std::string` via `std::getline` and
 *  delegates to the string overload. The whole document is buffered before
 *  any token is processed — streaming is not supported.
 *
 *  @param sin  Input stream positioned at the start of the JSON text.
 *  @param root Output value; populated on success.
 *  @return `true` if the document was parsed without errors.
 */
bool
Reader::parse(std::istream& sin, Value& root)
{
    std::string doc;
    std::getline(sin, doc, (char)EOF);
    return parse(doc, root);
}

/** Parse a JSON document from a raw byte range.
 *
 *  This is the core parse entry point; both other overloads delegate here.
 *  Initialises parser state, pushes `root` onto `nodes_`, then calls
 *  `readValue(0)`. After parsing, enforces the RFC 4627 constraint that the
 *  document root must be an object or array — bare scalars are rejected even
 *  if lexically valid JSON.
 *
 *  @param beginDoc Pointer to the first byte of the JSON document.
 *  @param endDoc   One-past-the-end pointer.
 *  @param root     Output value; populated on success.
 *  @return `true` if the document was parsed without errors and the root
 *      value is an object or array.
 *  @note Errors are accumulated in `errors_` rather than reported immediately;
 *      call `getFormattedErrorMessages()` after a `false` return.
 */
bool
Reader::parse(char const* beginDoc, char const* endDoc, Value& root)
{
    begin_ = beginDoc;
    end_ = endDoc;
    current_ = begin_;
    lastValueEnd_ = 0;
    lastValue_ = 0;
    errors_.clear();

    while (!nodes_.empty())
        nodes_.pop();

    nodes_.push(&root);
    bool const successful = readValue(0);
    Token token{};
    skipCommentTokens(token);

    if (!root.isNull() && !root.isArray() && !root.isObject())
    {
        token.type = TokenType::Error;
        token.start = beginDoc;
        token.end = endDoc;
        addError("A valid JSON document must be either an array or an object value.", token);
        return false;
    }

    return successful;
}

/** Dispatch the next JSON value into the current node.
 *
 *  Reads one token via `skipCommentTokens()`, then branches on its type to
 *  populate `currentValue()`. Recursion through `readObject()` and
 *  `readArray()` is bounded by `kNEST_LIMIT` (25 levels), preventing stack
 *  exhaustion from adversarially deep documents.
 *
 *  @param depth Current nesting depth (0 at the document root).
 *  @return `true` if the value was read successfully.
 */
bool
Reader::readValue(unsigned depth)
{
    Token token{};
    skipCommentTokens(token);
    if (depth > kNEST_LIMIT)
        return addError("Syntax error: maximum nesting depth exceeded", token);
    bool successful = true;

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

        case TokenType::True:
            currentValue() = true;
            break;

        case TokenType::False:
            currentValue() = false;
            break;

        case TokenType::Null:
            currentValue() = Value();
            break;

        default:
            return addError("Syntax error: value, object or array expected.", token);
    }

    return successful;
}

/** Advance `token` past any comment tokens to the next meaningful token.
 *
 *  Both C-style (`/* ... *\/`) and C++-style (`// ...`) comments are treated
 *  as transparent to the parser.
 *
 *  @param token Receives the first non-comment token found.
 */
void
Reader::skipCommentTokens(Token& token)
{
    do
    {
        readToken(token);
    } while (token.type == TokenType::Comment);
}

/** Read the next token and verify it matches the expected type.
 *
 *  @param type    The required `TokenType`.
 *  @param token   Receives the token that was read.
 *  @param message Error message recorded if the token type does not match.
 *  @return `true` if the token type matched; `false` (with an error recorded)
 *      otherwise.
 */
bool
Reader::expectToken(TokenType type, Token& token, char const* message)
{
    readToken(token);

    if (token.type != type)
        return addError(message, token);

    return true;
}

/** Lexer: classify the next token and advance `current_`.
 *
 *  Skips leading whitespace, records `token.start`, consumes one or more
 *  bytes, and sets `token.type` and `token.end`. Single-character punctuation
 *  is classified directly; multi-character tokens (`true`, `false`, `null`,
 *  strings, numbers, comments) are dispatched to specialised helpers.
 *
 *  @param token Receives the classified token with start/end pointers into
 *      the source buffer.
 *  @return Always `true`; a failed classification sets `token.type` to
 *      `TokenType::Error` rather than returning `false`.
 */
bool
Reader::readToken(Token& token)
{
    skipSpaces();
    token.start = current_;
    Char const c = getNextChar();
    bool ok = true;

    switch (c)
    {
        case '{':
            token.type = TokenType::ObjectBegin;
            break;

        case '}':
            token.type = TokenType::ObjectEnd;
            break;

        case '[':
            token.type = TokenType::ArrayBegin;
            break;

        case ']':
            token.type = TokenType::ArrayEnd;
            break;

        case '"':
            token.type = TokenType::String;
            ok = readString();
            break;

        case '/':
            token.type = TokenType::Comment;
            ok = readComment();
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
        case '-':
            token.type = readNumber();
            break;

        case 't':
            token.type = TokenType::True;
            ok = match("rue", 3);
            break;

        case 'f':
            token.type = TokenType::False;
            ok = match("alse", 4);  // cspell:disable-line
            break;

        case 'n':
            token.type = TokenType::Null;
            ok = match("ull", 3);
            break;

        case ',':
            token.type = TokenType::ArraySeparator;
            break;

        case ':':
            token.type = TokenType::MemberSeparator;
            break;

        case 0:
            token.type = TokenType::EndOfStream;
            break;

        default:
            ok = false;
            break;
    }

    if (!ok)
        token.type = TokenType::Error;

    token.end = current_;
    return true;
}

/** Advance `current_` past any ASCII whitespace (space, tab, CR, LF). */
void
Reader::skipSpaces()
{
    while (current_ != end_)
    {
        Char const c = *current_;

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

/** Attempt to match `patternLength` bytes at `current_` against `pattern`.
 *
 *  Used after the first character of a keyword (`t`, `f`, `n`) has already
 *  been consumed by `readToken()`. Advances `current_` only on a full match.
 *
 *  @param pattern       Pointer to the remaining expected bytes.
 *  @param patternLength Number of bytes to compare.
 *  @return `true` if all bytes matched and `current_` was advanced.
 */
bool
Reader::match(Location pattern, int patternLength)
{
    if (end_ - current_ < patternLength)
        return false;

    int index = patternLength;

    while ((index--) != 0)
    {
        if (current_[index] != pattern[index])
            return false;
    }

    current_ += patternLength;
    return true;
}

/** Dispatch a comment after the opening `/` has been consumed.
 *
 *  Peeks at the next character: `*` routes to `readCStyleComment()`,
 *  `/` routes to `readCppStyleComment()`, anything else is an error.
 *
 *  @return `true` if a valid comment was consumed.
 */
bool
Reader::readComment()
{
    Char const c = getNextChar();

    if (c == '*')
        return readCStyleComment();

    if (c == '/')
        return readCppStyleComment();

    return false;
}

/** Consume a C-style block comment after `/*` has been consumed.
 *
 *  @return `true` if the closing `*\/` was found before end of input.
 */
bool
Reader::readCStyleComment()
{
    while (current_ != end_)
    {
        Char const c = getNextChar();

        if (c == '*' && *current_ == '/')
            break;
    }

    return getNextChar() == '/';
}

/** Consume a C++-style line comment after `//` has been consumed.
 *
 *  Advances `current_` to the end of the line (CR or LF) or end of input.
 *  Always returns `true` — a missing newline (end of input) is not an error.
 *
 *  @return Always `true`.
 */
bool
Reader::readCppStyleComment()
{
    while (current_ != end_)
    {
        Char const c = getNextChar();

        if (c == '\r' || c == '\n')
            break;
    }

    return true;
}

/** Consume a number token and classify it as integer or double.
 *
 *  Assumes the first digit (or `-`) has already been consumed by `readToken()`.
 *  Scans the remaining digits; if any of `.`, `e`, `E`, `+`, or `-` is
 *  encountered the token is classified as `TokenType::Double`, otherwise
 *  `TokenType::Integer`. Actual conversion is deferred to `decodeNumber()`
 *  or `decodeDouble()`.
 *
 *  @return `TokenType::Integer` or `TokenType::Double`.
 */
Reader::TokenType
Reader::readNumber()
{
    static char const kEXTENDED_TOKENS[] = {'.', 'e', 'E', '+', '-'};

    TokenType type = TokenType::Integer;

    if (current_ != end_)
    {
        if (*current_ == '-')
            ++current_;

        while (current_ != end_)
        {
            if (std::isdigit(static_cast<unsigned char>(*current_)) == 0)
            {
                auto ret = std::ranges::find(kEXTENDED_TOKENS, *current_);

                if (ret == std::end(kEXTENDED_TOKENS))
                    break;

                type = TokenType::Double;
            }

            ++current_;
        }
    }

    return type;
}

/** Advance `current_` past a quoted string, consuming the closing `"`.
 *
 *  Only validates structure (balanced quotes, escape prefix handling).
 *  Actual escape decoding is performed later by `decodeString()`.
 *
 *  @return `true` if the closing `"` was found before end of input.
 */
bool
Reader::readString()
{
    Char c = 0;

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

/** Parse a JSON object body after `{` has been consumed.
 *
 *  Iterates over key-value pairs, decoding each string key and recursing
 *  into `readValue()` for each value. Duplicate keys are rejected — unlike
 *  the JSON specification, which permits them with implementation-defined
 *  winner semantics. This is intentional: in transaction parsing a silent
 *  duplicate (e.g. a second `"Amount"` field) could shadow the first.
 *
 *  @param tokenStart Token for the opening `{`; used for error reporting.
 *  @param depth      Current nesting depth, passed through to `readValue()`.
 *  @return `true` if the object was well-formed and closed with `}`.
 */
bool
Reader::readObject(Token& tokenStart, unsigned depth)
{
    Token tokenName{};
    std::string name;
    currentValue() = Value(ValueType::Object);

    while (readToken(tokenName))
    {
        bool initialTokenOk = true;

        while (tokenName.type == TokenType::Comment && initialTokenOk)
            initialTokenOk = readToken(tokenName);

        if (!initialTokenOk)
            break;

        if (tokenName.type == TokenType::ObjectEnd && name.empty())  // empty object
            return true;

        if (tokenName.type != TokenType::String)
            break;

        name = "";

        if (!decodeString(tokenName, name))
            return recoverFromError(TokenType::ObjectEnd);

        Token colon{};

        if (!readToken(colon) || colon.type != TokenType::MemberSeparator)
        {
            return addErrorAndRecover(
                "Missing ':' after object member name", colon, TokenType::ObjectEnd);
        }

        if (currentValue().isMember(name))
            return addError("Key '" + name + "' appears twice.", tokenName);

        Value& value = currentValue()[name];
        nodes_.push(&value);
        bool const ok = readValue(depth + 1);
        nodes_.pop();

        if (!ok)  // error already set
            return recoverFromError(TokenType::ObjectEnd);

        Token comma{};

        if (!readToken(comma) ||
            (comma.type != TokenType::ObjectEnd && comma.type != TokenType::ArraySeparator &&
             comma.type != TokenType::Comment))
        {
            return addErrorAndRecover(
                "Missing ',' or '}' in object declaration", comma, TokenType::ObjectEnd);
        }

        bool finalizeTokenOk = true;

        while (comma.type == TokenType::Comment && finalizeTokenOk)
            finalizeTokenOk = readToken(comma);

        if (comma.type == TokenType::ObjectEnd)
            return true;
    }

    return addErrorAndRecover("Missing '}' or object member name", tokenName, TokenType::ObjectEnd);
}

/** Parse a JSON array body after `[` has been consumed.
 *
 *  Iterates over elements, assigning each to a sequentially-indexed child of
 *  `currentValue()` and recursing into `readValue()`. An empty array (`[]`)
 *  is detected by peeking at the next non-whitespace character before entering
 *  the loop, avoiding an unnecessary `readValue()` call on `]`.
 *
 *  @param tokenStart Token for the opening `[`; used for error reporting.
 *  @param depth      Current nesting depth, passed through to `readValue()`.
 *  @return `true` if the array was well-formed and closed with `]`.
 */
bool
Reader::readArray(Token& tokenStart, unsigned depth)
{
    currentValue() = Value(ValueType::Array);
    skipSpaces();

    if (*current_ == ']')  // empty array
    {
        Token endArray{};
        readToken(endArray);
        return true;
    }

    int index = 0;

    while (true)
    {
        Value& value = currentValue()[index++];
        nodes_.push(&value);
        bool ok = readValue(depth + 1);
        nodes_.pop();

        if (!ok)  // error already set
            return recoverFromError(TokenType::ArrayEnd);

        Token token{};
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
            break;
    }

    return true;
}

/** Decode an integer token and assign it to `currentValue()`.
 *
 *  Uses a `std::int64_t` accumulator (wider than `Value::maxUInt`) to detect
 *  overflow before committing to a `Value::Int` or `Value::UInt`. When the
 *  magnitude fits in `Value::kMAX_INT`, the result is stored as a signed
 *  `Value::Int` to reduce surprises in downstream comparisons; larger
 *  non-negative values use `Value::UInt`.
 *
 *  @param token Token with `start`/`end` pointing into the source buffer.
 *  @return `true` on success; `false` (with an error recorded) if the token
 *      is not a valid integer or exceeds the representable range.
 */
bool
Reader::decodeNumber(Token& token)
{
    Location current = token.start;
    bool const isNegative = *current == '-';

    if (isNegative)
        ++current;

    if (current == token.end)
    {
        return addError(
            "'" + std::string(token.start, token.end) + "' is not a valid number.", token);
    }

    std::int64_t value = 0;

    static_assert(
        sizeof(value) > sizeof(Value::kMAX_UINT),
        "The JSON integer overflow logic will need to be reworked.");

    while (current < token.end && (value <= Value::kMAX_UINT))
    {
        Char const c = *current++;

        if (c < '0' || c > '9')
        {
            return addError(
                "'" + std::string(token.start, token.end) + "' is not a number.", token);
        }

        value = (value * 10) + (c - '0');
    }

    if (current != token.end)
    {
        return addError(
            "'" + std::string(token.start, token.end) + "' exceeds the allowable range.", token);
    }

    if (isNegative)
    {
        value = -value;

        if (value < Value::kMIN_INT || value > Value::kMAX_INT)
        {
            return addError(
                "'" + std::string(token.start, token.end) + "' exceeds the allowable range.",
                token);
        }

        currentValue() = static_cast<Value::Int>(value);
    }
    else
    {
        if (value > Value::kMAX_UINT)
        {
            return addError(
                "'" + std::string(token.start, token.end) + "' exceeds the allowable range.",
                token);
        }

        if (value <= Value::kMAX_INT)
        {
            currentValue() = static_cast<Value::Int>(value);
        }
        else
        {
            currentValue() = static_cast<Value::UInt>(value);
        }
    }

    return true;
}

/** Decode a floating-point token and assign it to `currentValue()`.
 *
 *  Uses `sscanf` rather than `std::stod` to work around a crash on some OS X
 *  versions when a string-constant format argument is used with certain
 *  compiler flags. The format string is stored in a `char[]` array rather
 *  than passed as a literal to avoid the issue. Tokens up to 32 characters
 *  use a stack buffer; longer tokens fall back to a `std::string`.
 *
 *  @param token Token with `start`/`end` pointing into the source buffer.
 *  @return `true` on success; `false` if `sscanf` does not consume exactly
 *      one value.
 */
bool
Reader::decodeDouble(Token& token)
{
    double value = 0;
    int const bufferSize = 32;
    int count = 0;
    int const length = int(token.end - token.start);
    if (length < 0)
    {
        return addError("Unable to parse token length", token);
    }
    char format[] = "%lf";
    if (length <= bufferSize)
    {
        Char buffer[bufferSize + 1];
        memcpy(buffer, token.start, length);
        buffer[length] = 0;
        count = sscanf(buffer, format, &value);
    }
    else
    {
        std::string const buffer(token.start, token.end);
        count = sscanf(buffer.c_str(), format, &value);
    }
    if (count != 1)
        return addError("'" + std::string(token.start, token.end) + "' is not a number.", token);
    currentValue() = value;
    return true;
}

/** Decode a string token into `currentValue()`.
 *
 *  Convenience wrapper that calls the two-argument overload and assigns the
 *  result into the current output node.
 *
 *  @param token String token with `start`/`end` including the surrounding
 *      quotes.
 *  @return `true` on success.
 */
bool
Reader::decodeString(Token& token)
{
    std::string decoded;

    if (!decodeString(token, decoded))
        return false;

    currentValue() = decoded;
    return true;
}

/** Decode a string token into `decoded`, processing all escape sequences.
 *
 *  Handles the standard JSON escape sequences (`\"`, `\\`, `\/`, `\b`, `\f`,
 *  `\n`, `\r`, `\t`) and `\uXXXX` Unicode escapes via
 *  `decodeUnicodeCodePoint()`, which handles UTF-16 surrogate pairs and
 *  encodes the result as UTF-8.
 *
 *  @param token   String token with `start`/`end` including surrounding quotes.
 *  @param decoded Accumulator for the decoded output; not cleared on entry.
 *  @return `true` on success; `false` if an invalid escape sequence is found.
 */
bool
Reader::decodeString(Token& token, std::string& decoded)
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

            Char const escape = *current++;

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
                    unsigned int unicode = 0;

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

/** Decode a `\uXXXX` escape and, if necessary, a following surrogate pair.
 *
 *  Calls `decodeUnicodeEscapeSequence()` for the first four hex digits. If
 *  the result is a UTF-16 high surrogate (U+D800–U+DBFF), a second
 *  `\uXXXX` sequence must immediately follow; the pair is combined into a
 *  supplementary-plane scalar using:
 *    `0x10000 + ((high & 0x3FF) << 10) + (low & 0x3FF)`.
 *
 *  @param token   Enclosing string token, used for error location.
 *  @param current In/out: pointer into the source buffer, positioned after
 *      the `u` of the first escape; advanced past all consumed characters.
 *  @param end     End of the string token's content region.
 *  @param unicode Output: decoded Unicode scalar value.
 *  @return `true` on success; `false` if the escape is malformed or an
 *      expected low surrogate is missing.
 */
bool
Reader::decodeUnicodeCodePoint(Token& token, Location& current, Location end, unsigned int& unicode)
{
    if (!decodeUnicodeEscapeSequence(token, current, end, unicode))
        return false;

    if (unicode >= 0xD800 && unicode <= 0xDBFF)
    {
        if (end - current < 6)
        {
            return addError(
                "additional six characters expected to parse unicode surrogate "
                "pair.",
                token,
                current);
        }

        unsigned int surrogatePair = 0;

        if (*current != '\\' || *(current + 1) != 'u')
        {
            return addError(
                "expecting another \\u token to begin the second half of a unicode surrogate pair",
                token,
                current);
        }

        current += 2;

        if (!decodeUnicodeEscapeSequence(token, current, end, surrogatePair))
            return false;

        unicode = 0x10000 + ((unicode & 0x3FF) << 10) + (surrogatePair & 0x3FF);
    }

    return true;
}

/** Decode exactly four hex digits from a `\uXXXX` escape sequence.
 *
 *  @param token   Enclosing string token, used for error location.
 *  @param current In/out: pointer into source positioned at the first hex
 *      digit; advanced by four on success.
 *  @param end     End of the content region.
 *  @param unicode Output: the decoded 16-bit value (0–0xFFFF).
 *  @return `true` on success; `false` if fewer than four characters remain
 *      or any character is not a valid hex digit.
 */
bool
Reader::decodeUnicodeEscapeSequence(
    Token& token,
    Location& current,
    Location end,
    unsigned int& unicode)
{
    if (end - current < 4)
    {
        return addError(
            "Bad unicode escape sequence in string: four digits expected.", token, current);
    }

    unicode = 0;

    for (int index = 0; index < 4; ++index)
    {
        Char const c = *current++;
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

/** Record a parse error and return `false`.
 *
 *  Appends an `ErrorInfo` to `errors_`. Always returns `false` so callers
 *  can write `return addError(...)` as a one-liner.
 *
 *  @param message Human-readable description of the error.
 *  @param token   Token at which the error occurred.
 *  @param extra   Optional secondary source location for additional context.
 *  @return Always `false`.
 */
bool
Reader::addError(std::string const& message, Token& token, Location extra)
{
    ErrorInfo info;
    info.token = token;
    info.message = message;
    info.extra = extra;
    errors_.push_back(info);
    return false;
}

/** Skip tokens until `skipUntilToken` or end of stream is found.
 *
 *  Called after a structural error (missing colon, bad value, etc.) to allow
 *  the parser to continue and report additional errors from the same document.
 *  Any secondary errors produced during the skip are discarded so they do not
 *  pollute the error list with noise from recovery.
 *
 *  @param skipUntilToken The terminator to seek (`TokenType::ObjectEnd` or
 *      `TokenType::ArrayEnd`).
 *  @return Always `false` (the error that triggered recovery was already
 *      recorded by the caller).
 */
bool
Reader::recoverFromError(TokenType skipUntilToken)
{
    int const errorCount = int(errors_.size());
    Token skip{};

    while (true)
    {
        if (!readToken(skip))
            errors_.resize(errorCount);  // discard errors caused by recovery

        if (skip.type == skipUntilToken || skip.type == TokenType::EndOfStream)
            break;
    }

    errors_.resize(errorCount);
    return false;
}

/** Record an error and attempt to recover by skipping to `skipUntilToken`.
 *
 *  Convenience composition of `addError()` and `recoverFromError()`.
 *
 *  @param message        Error message to record.
 *  @param token          Token at which the error occurred.
 *  @param skipUntilToken Terminator to seek during recovery.
 *  @return Always `false`.
 */
bool
Reader::addErrorAndRecover(std::string const& message, Token& token, TokenType skipUntilToken)
{
    addError(message, token);
    return recoverFromError(skipUntilToken);
}

/** Return a reference to the top-of-stack output node.
 *
 *  The caller is responsible for ensuring `nodes_` is non-empty.
 *
 *  @return Reference to the `Value` currently being populated.
 */
Value&
Reader::currentValue()
{
    return *(nodes_.top());
}

/** Return the byte at `current_` and advance it, or return 0 at end of input.
 *
 *  @return The consumed byte, or `0` if `current_ == end_`.
 */
Reader::Char
Reader::getNextChar()
{
    if (current_ == end_)
        return 0;

    return *current_++;
}

/** Compute the 1-based line and column numbers for a source location.
 *
 *  Walks the source buffer from `begin_` to `location`, counting CR, LF, and
 *  CR+LF line endings. This is O(n) per call, acceptable because it is only
 *  invoked when formatting error messages.
 *
 *  @param location Pointer into the source buffer to locate.
 *  @param line     Output: 1-based line number.
 *  @param column   Output: 1-based column number.
 */
void
Reader::getLocationLineAndColumn(Location location, int& line, int& column) const
{
    Location current = begin_;
    Location lastLineStart = current;
    line = 0;

    while (current < location && current != end_)
    {
        Char const c = *current++;

        if (c == '\r')
        {
            if (*current == '\n')
                ++current;

            lastLineStart = current;
            ++line;
        }
        else if (c == '\n')
        {
            lastLineStart = current;
            ++line;
        }
    }

    column = int(location - lastLineStart) + 1;
    ++line;
}

/** Format a source location as a human-readable string.
 *
 *  @param location Pointer into the source buffer.
 *  @return A string of the form `"Line N, Column M"` (1-based).
 */
std::string
Reader::getLocationLineAndColumn(Location location) const
{
    int line = 0, column = 0;
    getLocationLineAndColumn(location, line, column);
    return "Line " + std::to_string(line) + ", Column " + std::to_string(column);
}

std::string
Reader::getFormattedErrorMessages() const
{
    std::string formattedMessage;

    for (Errors::const_iterator itError = errors_.begin(); itError != errors_.end(); ++itError)
    {
        ErrorInfo const& error = *itError;
        formattedMessage += "* " + getLocationLineAndColumn(error.token.start) + "\n";
        formattedMessage += "  " + error.message + "\n";

        if (error.extra != nullptr)
            formattedMessage += "See " + getLocationLineAndColumn(error.extra) + " for detail.\n";
    }

    return formattedMessage;
}

/** Extract a JSON value from an input stream, throwing on parse failure.
 *
 *  Unlike `Reader::parse()`, which returns `false` on error, this overload
 *  throws `std::runtime_error` (via `xrpl::Throw<>`) with the formatted error
 *  messages. Use this on code paths where parse failure is truly exceptional
 *  and propagation via return value would be burdensome.
 *
 *  @param sin  Input stream positioned at the start of a JSON document.
 *  @param root Output value populated on success.
 *  @return `sin` (to support chaining).
 *  @throws std::runtime_error if the stream does not contain valid JSON.
 *  @see Reader::parse(std::istream&, Value&)
 */
std::istream&
operator>>(std::istream& sin, Value& root)
{
    json::Reader reader;
    bool const ok = reader.parse(sin, root);

    // XRPL_ASSERT(ok, "json::operator>>() : parse succeeded");
    if (!ok)
        xrpl::Throw<std::runtime_error>(reader.getFormattedErrorMessages());

    return sin;
}

}  // namespace json
