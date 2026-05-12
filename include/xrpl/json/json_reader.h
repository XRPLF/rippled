#pragma once

#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>

#include <boost/asio/buffer.hpp>

#include <stack>

namespace json {

/** \brief Unserialize a <a HREF="http://www.json.org">JSON</a> document into a
 * Value.
 *
 */
class Reader
{
public:
    using Char = char;
    using Location = Char const*;

    /** \brief Constructs a Reader allowing all features
     * for parsing.
     */
    Reader() = default;

    /** \brief Read a Value from a <a HREF="http://www.json.org">JSON</a>
     * document. \param document UTF-8 encoded string containing the document to
     * read. \param root [out] Contains the root value of the document if it was
     *             successfully parsed.
     * \return \c true if the document was successfully parsed, \c false if an
     * error occurred.
     */
    bool
    parse(std::string const& document, Value& root);

    /** \brief Read a Value from a <a HREF="http://www.json.org">JSON</a>
     * document. \param document UTF-8 encoded string containing the document to
     * read. \param root [out] Contains the root value of the document if it was
     *             successfully parsed.
     * \return \c true if the document was successfully parsed, \c false if an
     * error occurred.
     */
    bool
    parse(char const* beginDoc, char const* endDoc, Value& root);

    /// \brief Parse from input stream.
    /// \see json::operator>>(std::istream&, json::Value&).
    bool
    parse(std::istream& is, Value& root);

    /** \brief Read a Value from a <a HREF="http://www.json.org">JSON</a> buffer
     * sequence. \param root [out] Contains the root value of the document if it
     * was successfully parsed. \param UTF-8 encoded buffer sequence. \return \c
     * true if the buffer was successfully parsed, \c false if an error
     * occurred.
     */
    template <class BufferSequence>
    bool
    parse(Value& root, BufferSequence const& bs);

    /** \brief Returns a user friendly string that list errors in the parsed
     * document. \return Formatted error message with the list of errors with
     * their location in the parsed document. An empty string is returned if no
     * error occurred during parsing.
     */
    [[nodiscard]] std::string
    getFormattedErrorMessages() const;

    static constexpr unsigned kNestLimit{25};

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

        TokenType type;
        Location start;
        Location end;
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

    bool
    expectToken(TokenType type, Token& token, char const* message);
    bool
    readToken(Token& token);
    void
    skipSpaces();
    bool
    match(Location pattern, int patternLength);
    bool
    readComment();
    bool
    readCStyleComment();
    bool
    readCppStyleComment();
    bool
    readString();
    Reader::TokenType
    readNumber();
    bool
    readValue(unsigned depth);
    bool
    readObject(Token& token, unsigned depth);
    bool
    readArray(Token& token, unsigned depth);
    bool
    decodeNumber(Token& token);
    bool
    decodeString(Token& token);
    bool
    decodeString(Token& token, std::string& decoded);
    bool
    decodeDouble(Token& token);
    bool
    decodeUnicodeCodePoint(Token& token, Location& current, Location end, unsigned int& unicode);
    bool
    decodeUnicodeEscapeSequence(
        Token& token,
        Location& current,
        Location end,
        unsigned int& unicode);
    bool
    addError(std::string const& message, Token& token, Location extra = 0);
    bool
    recoverFromError(TokenType skipUntilToken);
    bool
    addErrorAndRecover(std::string const& message, Token& token, TokenType skipUntilToken);
    void
    skipUntilSpace();
    Value&
    currentValue();
    Char
    getNextChar();
    void
    getLocationLineAndColumn(Location location, int& line, int& column) const;
    std::string
    getLocationLineAndColumn(Location location) const;
    void
    skipCommentTokens(Token& token);

    using Nodes = std::stack<Value*>;
    Nodes nodes_;
    Errors errors_;
    std::string document_;
    Location begin_{};
    Location end_{};
    Location current_{};
    Location lastValueEnd_{};
    Value* lastValue_{};
};

template <class BufferSequence>
bool
Reader::parse(Value& root, BufferSequence const& bs)
{
    using namespace boost::asio;
    std::string s;
    s.reserve(buffer_size(bs));
    for (auto const& b : bs)
        s.append(static_cast<char const*>(b.data()), buffer_size(b));
    return parse(s, root);
}

/** \brief Read from 'sin' into 'root'.

 Always keep comments from the input JSON.

 This can be used to read a file into a particular sub-object.
 For example:
 \code
 json::Value root;
 cin >> root["dir"]["file"];
 cout << root;
 \endcode
 Result:
 \verbatim
 {
"dir": {
    "file": {
 // The input stream JSON would be nested here.
    }
}
 }
 \endverbatim
 \throw std::exception on parse error.
 \see json::operator<<()
*/
std::istream&
operator>>(std::istream&, Value&);

}  // namespace json
