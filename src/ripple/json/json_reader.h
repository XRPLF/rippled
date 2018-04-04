//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_JSON_JSON_READER_H_INCLUDED
#define RIPPLE_JSON_JSON_READER_H_INCLUDED

# define CPPTL_JSON_READER_H_INCLUDED

#include <ripple/json/json_forwards.h>
#include <ripple/json/json_value.h>
#include <boost/asio/buffer.hpp>
#include <stack>

namespace Json
{

/** \brief Unserialize a <a HREF="http://www.json.org">JSON</a> document into a Value.
 *
 */
class Reader
{
public:
    using Char = char;
    using Location = const Char*;

    /** \brief Constructs a Reader allowing all features
     * for parsing.
     */
    Reader ();

    /** \brief Read a Value from a <a HREF="http://www.json.org">JSON</a> document.
     * \param document UTF-8 encoded string containing the document to read.
     * \param root [out] Contains the root value of the document if it was
     *             successfully parsed.
     * \return \c true if the document was successfully parsed, \c false if an error occurred.
     */
    bool parse ( std::string const& document, Value& root);

    /** \brief Read a Value from a <a HREF="http://www.json.org">JSON</a> document.
     * \param document UTF-8 encoded string containing the document to read.
     * \param root [out] Contains the root value of the document if it was
     *             successfully parsed.
     * \return \c true if the document was successfully parsed, \c false if an error occurred.
     */
    bool parse ( const char* beginDoc, const char* endDoc, Value& root);

    /// \brief Parse from input stream.
    /// \see Json::operator>>(std::istream&, Json::Value&).
    bool parse ( std::istream& is, Value& root);

    /** \brief Read a Value from a <a HREF="http://www.json.org">JSON</a> buffer sequence.
     * \param root [out] Contains the root value of the document if it was
     *             successfully parsed.
     * \param UTF-8 encoded buffer sequence.
     * \return \c true if the buffer was successfully parsed, \c false if an error occurred.
     */
    template<class BufferSequence>
    bool
    parse(Value& root, BufferSequence const& bs);

    /** \brief Returns a user friendly string that list errors in the parsed document.
     * \return Formatted error message with the list of errors with their location in
     *         the parsed document. An empty string is returned if no error occurred
     *         during parsing.
     */
    std::string getFormatedErrorMessages () const;

    static constexpr unsigned nest_limit {25};

private:
    enum TokenType
    {
        tokenEndOfStream = 0,
        tokenObjectBegin,
        tokenObjectEnd,
        tokenArrayBegin,
        tokenArrayEnd,
        tokenString,
        tokenInteger,
        tokenDouble,
        tokenTrue,
        tokenFalse,
        tokenNull,
        tokenArraySeparator,
        tokenMemberSeparator,
        tokenComment,
        tokenError
    };

    class Token
    {
    public:
        explicit Token() = default;

        TokenType type_;
        Location start_;
        Location end_;
    };

    class ErrorInfo
    {
    public:
        explicit ErrorInfo() = default;

        Token token_;
        std::string message_;
        Location extra_;
    };

    using Errors = std::deque<ErrorInfo>;

    bool expectToken ( TokenType type, Token& token, const char* message );
    bool readToken ( Token& token );
    void skipSpaces ();
    bool match ( Location pattern,
                 int patternLength );
    bool readComment ();
    bool readCStyleComment ();
    bool readCppStyleComment ();
    bool readString ();
    Reader::TokenType readNumber ();
    bool readValue(unsigned depth);
    bool readObject(Token& token, unsigned depth);
    bool readArray (Token& token, unsigned depth);
    bool decodeNumber ( Token& token );
    bool decodeString ( Token& token );
    bool decodeString ( Token& token, std::string& decoded );
    bool decodeDouble ( Token& token );
    bool decodeUnicodeCodePoint ( Token& token,
                                  Location& current,
                                  Location end,
                                  unsigned int& unicode );
    bool decodeUnicodeEscapeSequence ( Token& token,
                                       Location& current,
                                       Location end,
                                       unsigned int& unicode );
    bool addError ( std::string const& message,
                    Token& token,
                    Location extra = 0 );
    bool recoverFromError ( TokenType skipUntilToken );
    bool addErrorAndRecover ( std::string const& message,
                              Token& token,
                              TokenType skipUntilToken );
    void skipUntilSpace ();
    Value& currentValue ();
    Char getNextChar ();
    void getLocationLineAndColumn ( Location location,
                                    int& line,
                                    int& column ) const;
    std::string getLocationLineAndColumn ( Location location ) const;
    void skipCommentTokens ( Token& token );

    using Nodes = std::stack<Value*>;
    Nodes nodes_;
    Errors errors_;
    std::string document_;
    Location begin_;
    Location end_;
    Location current_;
    Location lastValueEnd_;
    Value* lastValue_;
};

template<class BufferSequence>
bool
Reader::parse(Value& root, BufferSequence const& bs)
{
    using namespace boost::asio;
    std::string s;
    s.reserve (buffer_size(bs));
    for (auto const& b : bs)
        s.append(buffer_cast<char const*>(b), buffer_size(b));
    return parse(s, root);
}

/** \brief Read from 'sin' into 'root'.

 Always keep comments from the input JSON.

 This can be used to read a file into a particular sub-object.
 For example:
 \code
 Json::Value root;
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
 \see Json::operator<<()
*/
std::istream& operator>> ( std::istream&, Value& );

} // namespace Json

#endif // CPPTL_JSON_READER_H_INCLUDED
