#pragma once

#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_parser.h>
#include <xrpl/json/json_value.h>

#include <cstddef>
#include <expected>
#include <istream>
#include <stack>
#include <string>
#include <string_view>

namespace json {

/**
 * clang-format off
 * @brief Unserialize a <a HREF="http://www.json.org">JSON</a> document into a
 * Value.
 * clang-format on
 */
class Reader
{
public:
    using Char = char;
    using Location = Char const*;

    /**
     * @brief Constructs a Reader allowing all features
     * for parsing.
     */
    Reader();

    // The parser holds its visitor by reference, so a copied Reader would
    // report into the original's builder.
    Reader(Reader const&) = delete;
    Reader&
    operator=(Reader const&) = delete;

    /**
     * clang-format off
     * @brief Read a Value from a <a HREF="http://www.json.org">JSON</a>
     * document.
     * @param document UTF-8 encoded string containing the document to
     * read.
     * @param root [out] Contains the root value of the document if it was
     * successfully parsed.
     * @return @c true if the document was successfully parsed, @c false if an
     * error occurred.
     * clang-format on
     */
    bool
    parse(std::string const& document, Value& root);

    /**
     * clang-format off
     * @brief Read a Value from a <a HREF="http://www.json.org">JSON</a>
     * document.
     * @param document UTF-8 encoded string containing the document to
     * read.
     * @param root [out] Contains the root value of the document if it was
     * successfully parsed.
     * @return @c true if the document was successfully parsed, @c false if an
     * error occurred.
     * clang-format on
     */
    bool
    parse(char const* beginDoc, char const* endDoc, Value& root);

    /**
     * clang-format off
     * @brief Parse from input stream.
     * @see json::operator>>(std::istream&, json::Value&).
     * clang-format on
     */
    bool
    parse(std::istream& is, Value& root);

    /**
     * clang-format off
     * @brief Read a Value from a <a HREF="http://www.json.org">JSON</a> buffer
     * sequence.
     * @param root [out] Contains the root value of the document if it
     * was successfully parsed.
     * @param UTF-8 encoded buffer sequence.
     * @return @c true if the buffer was successfully parsed, @c false if an error
     * occurred.
     * clang-format on
     */
    template <class BufferSequence>
    bool
    parse(Value& root, BufferSequence const& bs);

    /**
     * clang-format off
     * @brief Returns a user friendly string that list errors in the parsed
     * document.
     * @return Formatted error message with the list of errors with
     * their location in the parsed document. An empty string is returned if no
     * error occurred during parsing.
     * clang-format on
     */
    [[nodiscard]] std::string
    getFormattedErrorMessages() const;

    static constexpr unsigned kNestLimit{25};

private:
    /**
     * clang-format off
     * @brief A json::Parser visitor that builds a Value tree.
     *
     * Members are placed into a std::map, so the resulting tree iterates in
     * sorted key order regardless of the order they appeared in the document.
     * clang-format on
     */
    class ValueBuilder
    {
    public:
        using ReturnType = std::expected<void, std::string>;

        /**
         * Point the builder at the Value the next parse should populate.
         */
        void
        target(Value& root);

        ReturnType
        onDocumentBegin();
        ReturnType
        onDocumentEnd(std::size_t documentSize);
        ReturnType
        onObjectBegin();
        ReturnType
        onObjectEnd(std::size_t memberCount);
        ReturnType
        onArrayBegin();
        ReturnType
        onArrayEnd(std::size_t elementCount);
        ReturnType
        onKey(std::string_view key);
        ReturnType
        onString(std::string_view value);
        ReturnType
        onInt(Value::Int value);
        ReturnType
        onUInt(Value::UInt value);
        ReturnType
        onDouble(double value);
        ReturnType
        onBool(bool value);
        ReturnType
        onNull();

    private:
        /**
         * @brief Returns the slot the next value belongs in, creating it if
         * need be: the root, the member named by the pending key, or one past
         * the end of the enclosing array.
         */
        Value&
        place();

        Value* root_{nullptr};
        std::stack<Value*> nodes_;
        std::string pendingKey_;
    };

    ValueBuilder builder_;
    Parser<ValueBuilder> parser_{builder_};
};

template <class BufferSequence>
bool
Reader::parse(Value& root, BufferSequence const& bs)
{
    builder_.target(root);
    return parser_.parse(bs);
}

/**
 * clang-format off
 * @brief Read from 'sin' into 'root'.
 *
 * Always keep comments from the input JSON.
 *
 * This can be used to read a file into a particular sub-object.
 * For example:
 * @code
 * json::Value root;
 * cin >> root["dir"]["file"];
 * cout << root;
 * @endcode
 * Result:
 * @verbatim
 * {
 * "dir": {
 *    "file": {
 * // The input stream JSON would be nested here.
 *    }
 * }
 * }
 * @endverbatim
 * @throws std::exception on parse error.
 * @see json::operator<<()
 * clang-format on
 */
std::istream&
operator>>(std::istream&, Value&);

}  // namespace json
