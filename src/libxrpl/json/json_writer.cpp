/** @file
 *  Serialization engine for XRPL's embedded JSON library.
 *
 *  Implements three output strategies over `Json::Value` trees:
 *  - `FastWriter`          — compact single-line output, no heap cost per node.
 *  - `StyledWriter`        — human-readable indented output to a `std::string`.
 *  - `StyledStreamWriter`  — human-readable indented output directly to a
 *                            `std::ostream`, avoiding intermediate string copies.
 *
 *  The file also provides the primitive helpers (`valueToString` overloads,
 *  `valueToQuotedString`) used by all three writers and by the header-only
 *  `detail::write_value` template.
 */
#include <xrpl/json/json_writer.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>

#include <cstdio>
#include <cstring>
#include <iomanip>
#include <ios>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>

namespace json {

/** Returns true if @a ch is a JSON control character (U+0001–U+001F).
 *
 *  U+0000 (NUL) is excluded; it terminates C-strings and cannot appear
 *  in a valid JSON string literal without escaping via `\u0000`.
 *
 *  @param ch The character to test.
 *  @return True if @a ch falls in the range [0x01, 0x1F].
 */
static bool
isControlCharacter(char ch)
{
    return ch > 0 && ch <= 0x1F;
}

/** Returns true if @a str contains at least one JSON control character.
 *
 *  Scans the NUL-terminated string for any byte in U+0001–U+001F.
 *  Used by `valueToQuotedString` as part of its fast-path check before
 *  committing to a character-by-character escape walk.
 *
 *  @param str NUL-terminated input string.
 *  @return True if any control character is present.
 */
static bool
containsControlCharacter(char const* str)
{
    while (*str != 0)
    {
        if (isControlCharacter(*(str++)))
            return true;
    }

    return false;
}

/** Write the decimal representation of @a value into a stack buffer.
 *
 *  Digits are written in reverse order — from the end of the buffer toward
 *  the front — so no string reversal is needed. On return, @a current
 *  points to the first character of the number string and a NUL terminator
 *  has been placed one position past the last digit.
 *
 *  The 32-byte buffer supplied by the caller is far larger than the maximum
 *  decimal representation of any 64-bit integer (20 digits), so the pointer
 *  can never escape the buffer.
 *
 *  @param value   The unsigned integer to convert.
 *  @param current In/out pointer into the caller's buffer; advanced backward
 *      past each digit and the trailing NUL on each call.
 */
static void
uintToString(unsigned int value, char*& current)
{
    *--current = 0;

    do
    {
        *--current = (value % 10) + '0';
        value /= 10;
    } while (value != 0);
}

/** Serialize a signed integer to its decimal string representation.
 *
 *  Uses a stack-allocated 32-byte buffer and `uintToString` to avoid heap
 *  allocation. A leading '-' is prepended for negative values.
 *
 *  @param value The signed integer to convert.
 *  @return Decimal string representation of @a value.
 */
std::string
valueToString(Int value)
{
    char buffer[32];
    char* current = buffer + sizeof(buffer);  // NOLINT(misc-const-correctness)
    bool const isNegative = value < 0;

    if (isNegative)
        value = -value;

    uintToString(UInt(value), current);

    if (isNegative)
        *--current = '-';

    XRPL_ASSERT(current >= buffer, "json::valueToString(Int) : buffer check");
    return current;
}

/** Serialize an unsigned integer to its decimal string representation.
 *
 *  Uses a stack-allocated 32-byte buffer and `uintToString` to avoid heap
 *  allocation.
 *
 *  @param value The unsigned integer to convert.
 *  @return Decimal string representation of @a value.
 */
std::string
valueToString(UInt value)
{
    char buffer[32];
    char* current = buffer + sizeof(buffer);  // NOLINT(misc-const-correctness)
    uintToString(value, current);
    XRPL_ASSERT(current >= buffer, "json::valueToString(UInt) : buffer check");
    return current;
}

/** Serialize a double to its JSON string representation at full precision.
 *
 *  Formats with `%.16g` to preserve 16 significant digits. The `%g`
 *  specifier is used rather than `%#g` because JSON does not distinguish
 *  reals from integers in its grammar, so a mandatory trailing decimal
 *  point is unnecessary.
 *
 *  @param value The double to convert.
 *  @return String containing the formatted number.
 */
std::string
valueToString(double value)
{
    char buffer[32];
#if defined(_MSC_VER) && defined(__STDC_SECURE_LIB__)  // Use secure version with visual studio 2005
                                                       // to avoid warning.
    sprintf_s(buffer, sizeof(buffer), "%.16g", value);
#else
    snprintf(buffer, sizeof(buffer), "%.16g", value);
#endif
    return buffer;
}

/** Serialize a boolean to its JSON literal representation.
 *
 *  @param value The boolean to convert.
 *  @return `"true"` or `"false"`.
 */
std::string
valueToString(bool value)
{
    return value ? "true" : "false";
}

/** Produce a properly JSON-escaped, double-quote-delimited string.
 *
 *  Fast path: if the input contains none of the named special characters
 *  (`"`, `\`, `\b`, `\f`, `\n`, `\r`, `\t`) and no control characters
 *  (U+0001–U+001F), the string is wrapped in quotes and returned directly.
 *
 *  Slow path: each character is inspected and emitted as the appropriate
 *  two-character JSON escape sequence. Control characters outside the
 *  named set are emitted as `\uXXXX`. The result buffer is pre-reserved
 *  to `2 * strlen + 3` bytes to avoid repeated reallocation.
 *
 *  @note Forward slashes are intentionally *not* escaped. They are legal
 *      unescaped in JSON. Escaping them (`\/`) would help avoid the
 *      JavaScript `</` sequence but is not required by the JSON spec.
 *
 *  @param value NUL-terminated input string.
 *  @return JSON-escaped string enclosed in double quotes.
 */
std::string
valueToQuotedString(char const* value)
{
    if (strpbrk(value, "\"\\\b\f\n\r\t") == nullptr && !containsControlCharacter(value))
        return std::string("\"") + value + "\"";

    // We have to walk value and escape any special characters.
    // Appending to std::string is not efficient, but this should be rare.
    unsigned const maxsize = (strlen(value) * 2) + 3;  // all-escaped+quotes+NULL
    std::string result;
    result.reserve(maxsize);
    result += "\"";

    for (char const* c = value; *c != 0; ++c)
    {
        switch (*c)
        {
            case '\"':
                result += "\\\"";
                break;

            case '\\':
                result += "\\\\";
                break;

            case '\b':
                result += "\\b";
                break;

            case '\f':
                result += "\\f";
                break;

            case '\n':
                result += "\\n";
                break;

            case '\r':
                result += "\\r";
                break;

            case '\t':
                result += "\\t";
                break;

            default:
                if (isControlCharacter(*c))
                {
                    std::ostringstream oss;
                    oss << "\\u" << std::hex << std::uppercase << std::setfill('0') << std::setw(4)
                        << static_cast<int>(*c);
                    result += oss.str();
                }
                else
                {
                    result += *c;
                }

                break;
        }
    }

    result += "\"";
    return result;
}

// --- FastWriter ---

/** Serialize @a root to a compact, single-line JSON string.
 *
 *  Resets the internal accumulation buffer, performs a recursive write,
 *  then returns the result via move. The output contains no whitespace
 *  beyond what appears inside string values.
 *
 *  @param root The value tree to serialize.
 *  @return Compact JSON string with no trailing newline.
 */
std::string
FastWriter::write(Value const& root)
{
    document_ = "";
    writeValue(root);
    return std::move(document_);
}

/** Recursively append the JSON representation of @a value to `document_`.
 *
 *  Arrays are rendered as `[elem,elem,...]` and objects as
 *  `{"key":value,...}` with no extra whitespace.
 *
 *  @param value The node to serialize.
 */
void
FastWriter::writeValue(Value const& value)
{
    switch (value.type())
    {
        case ValueType::Null:
            document_ += "null";
            break;

        case ValueType::Int:
            document_ += valueToString(value.asInt());
            break;

        case ValueType::UInt:
            document_ += valueToString(value.asUInt());
            break;

        case ValueType::Real:
            document_ += valueToString(value.asDouble());
            break;

        case ValueType::String:
            document_ += valueToQuotedString(value.asCString());
            break;

        case ValueType::Boolean:
            document_ += valueToString(value.asBool());
            break;

        case ValueType::Array: {
            document_ += "[";
            int const size = value.size();

            for (int index = 0; index < size; ++index)
            {
                if (index > 0)
                    document_ += ",";

                writeValue(value[index]);
            }

            document_ += "]";
        }
        break;

        case ValueType::Object: {
            Value::Members members(value.getMemberNames());
            document_ += "{";

            for (Value::Members::iterator it = members.begin(); it != members.end(); ++it)
            {
                std::string const& name = *it;

                if (it != members.begin())
                    document_ += ",";

                document_ += valueToQuotedString(name.c_str());
                document_ += ":";
                writeValue(value[name]);
            }

            document_ += "}";
        }
        break;
    }
}

// --- StyledWriter ---

StyledWriter::StyledWriter() = default;

/** Serialize @a root to a human-readable, indented JSON string.
 *
 *  Resets all internal state (accumulation buffer, indent level, and the
 *  `addChildValues_` measurement flag) before each call so the writer is
 *  safe to reuse across multiple documents. A trailing newline is appended.
 *
 *  @param root The value tree to serialize.
 *  @return Indented JSON string terminated with `'\n'`.
 */
std::string
StyledWriter::write(Value const& root)
{
    document_ = "";
    addChildValues_ = false;
    indentString_ = "";
    writeValue(root);
    document_ += "\n";
    return document_;
}

/** Recursively write the styled JSON representation of @a value.
 *
 *  Scalars are forwarded to `pushValue`. Arrays delegate to
 *  `writeArrayValue` for the single-line/multi-line heuristic. Objects
 *  always expand to one member per indented line; an empty object emits
 *  `{}` inline.
 *
 *  @param value The node to serialize.
 */
void
StyledWriter::writeValue(Value const& value)
{
    switch (value.type())
    {
        case ValueType::Null:
            pushValue("null");
            break;

        case ValueType::Int:
            pushValue(valueToString(value.asInt()));
            break;

        case ValueType::UInt:
            pushValue(valueToString(value.asUInt()));
            break;

        case ValueType::Real:
            pushValue(valueToString(value.asDouble()));
            break;

        case ValueType::String:
            pushValue(valueToQuotedString(value.asCString()));
            break;

        case ValueType::Boolean:
            pushValue(valueToString(value.asBool()));
            break;

        case ValueType::Array:
            writeArrayValue(value);
            break;

        case ValueType::Object: {
            Value::Members members(value.getMemberNames());

            if (members.empty())
            {
                pushValue("{}");
            }
            else
            {
                writeWithIndent("{");
                indent();
                Value::Members::iterator it = members.begin();

                while (true)
                {
                    std::string const& name = *it;
                    Value const& childValue = value[name];
                    writeWithIndent(valueToQuotedString(name.c_str()));
                    document_ += " : ";
                    writeValue(childValue);

                    if (++it; it == members.end())
                        break;

                    document_ += ",";
                }

                unindent();
                writeWithIndent("}");
            }
        }
        break;
    }
}

/** Write an array value, choosing single-line or multi-line layout.
 *
 *  An empty array emits `[]`. Non-empty arrays consult `isMultilineArray`
 *  to decide layout: multi-line places each element on its own indented
 *  line; single-line renders `[ e1, e2, ... ]` using the pre-rendered
 *  strings in `childValues_` populated during the heuristic's dry run.
 *
 *  @param value An array-typed `Value` node.
 */
void
StyledWriter::writeArrayValue(Value const& value)
{
    unsigned const size = value.size();

    if (size == 0)
    {
        pushValue("[]");
    }
    else
    {
        bool const isArrayMultiLine = isMultilineArray(value);

        if (isArrayMultiLine)
        {
            writeWithIndent("[");
            indent();
            bool const hasChildValue = !childValues_.empty();
            unsigned index = 0;

            while (true)
            {
                Value const& childValue = value[index];

                if (hasChildValue)
                {
                    writeWithIndent(childValues_[index]);
                }
                else
                {
                    writeIndent();
                    writeValue(childValue);
                }

                if (++index == size)
                    break;

                document_ += ",";
            }

            unindent();
            writeWithIndent("]");
        }
        else  // output on a single line
        {
            XRPL_ASSERT(
                childValues_.size() == size,
                "json::StyledWriter::writeArrayValue : child size match");
            document_ += "[ ";

            for (unsigned index = 0; index < size; ++index)
            {
                if (index > 0)
                    document_ += ", ";

                document_ += childValues_[index];
            }

            document_ += " ]";
        }
    }
}

/** Determine whether @a value should be rendered as a multi-line array.
 *
 *  Applies a two-stage heuristic:
 *
 *  1. **Quick check** — if `size * 3 >= rightMargin_` (three characters per
 *     element saturates the line), or if any element is a non-empty object
 *     or array, the array is immediately deemed multi-line.
 *
 *  2. **Dry-run check** — sets `addChildValues_` so that `pushValue` diverts
 *     rendered strings into `childValues_` instead of `document_`, then
 *     calls `writeValue` on each element to measure actual widths. If the
 *     total computed line length reaches `rightMargin_`, the array becomes
 *     multi-line. The populated `childValues_` is reused by
 *     `writeArrayValue` during the real render pass, avoiding a second
 *     serialization of each element.
 *
 *  @note Each element of a borderline array may be serialized twice: once
 *      during measurement and once during final output. This is negligible
 *      for the short arrays common in ledger data.
 *
 *  @param value An array-typed `Value` node.
 *  @return True if the array should be rendered with one element per line.
 */
bool
StyledWriter::isMultilineArray(Value const& value)
{
    int const size = value.size();
    bool isMultiLine = size * 3 >= rightMargin_;
    childValues_.clear();

    for (int index = 0; index < size && !isMultiLine; ++index)
    {
        Value const& childValue = value[index];
        isMultiLine = isMultiLine ||
            ((childValue.isArray() || childValue.isObject()) && childValue.size() > 0);
    }

    if (!isMultiLine)  // check if line length > max line length
    {
        childValues_.reserve(size);
        addChildValues_ = true;
        int lineLength = 4 + ((size - 1) * 2);  // '[ ' + ', '*n + ' ]'

        for (int index = 0; index < size; ++index)
        {
            writeValue(value[index]);
            lineLength += int(childValues_[index].length());
        }

        addChildValues_ = false;
        isMultiLine = isMultiLine || lineLength >= rightMargin_;
    }

    return isMultiLine;
}

/** Append @a value to the output, or capture it for measurement.
 *
 *  When `addChildValues_` is set (during the `isMultilineArray` dry run),
 *  the string is pushed onto `childValues_` instead of being written to
 *  `document_`. This lets the measurement pass share the same write path
 *  as the final render pass.
 *
 *  @param value The serialized scalar or composite string to emit.
 */
void
StyledWriter::pushValue(std::string const& value)
{
    if (addChildValues_)
    {
        childValues_.push_back(value);
    }
    else
    {
        document_ += value;
    }
}

/** Emit a newline and the current indent string, avoiding double-indentation.
 *
 *  If the last character in `document_` is already a space, the call is a
 *  no-op (the line is considered already indented). If the last character
 *  is not a newline, one is appended before the indent string.
 */
void
StyledWriter::writeIndent()
{
    if (!document_.empty())
    {
        char const last = document_[document_.length() - 1];

        if (last == ' ')  // already indented
            return;

        if (last != '\n')  // Comments may add new-line
            document_ += '\n';
    }

    document_ += indentString_;
}

/** Emit a newline+indent then append @a value to `document_`.
 *
 *  @param value The string to write after the indent.
 */
void
StyledWriter::writeWithIndent(std::string const& value)
{
    writeIndent();
    document_ += value;
}

/** Increase the current indentation level by `indentSize_` spaces. */
void
StyledWriter::indent()
{
    indentString_ += std::string(indentSize_, ' ');
}

/** Decrease the current indentation level by `indentSize_` spaces. */
void
StyledWriter::unindent()
{
    XRPL_ASSERT(
        int(indentString_.size()) >= indentSize_,
        "json::StyledWriter::unindent : maximum indent size");
    indentString_.resize(indentString_.size() - indentSize_);
}

// --- StyledStreamWriter ---

/** Construct a `StyledStreamWriter` with the given per-level indentation unit.
 *
 *  Unlike `StyledWriter`, which hard-codes 3-space indentation, the stream
 *  variant accepts any string so callers can choose tabs, 2-space, or
 *  4-space indent without subclassing.
 *
 *  @param indentation String appended once per nesting level. Defaults to `"\t"`.
 */
StyledStreamWriter::StyledStreamWriter(std::string indentation)
    : indentation_(std::move(indentation))
{
}

/** Write the styled JSON representation of @a root directly to @a out.
 *
 *  Resets internal state, writes the full value tree, emits a trailing
 *  newline, then sets `document_` to `nullptr` as a defensive measure
 *  against use-after-write if the caller holds a reference to this writer
 *  and accidentally invokes methods outside a `write()` session.
 *
 *  @param out  The output stream to write to.
 *  @param root The value tree to serialize.
 */
void
StyledStreamWriter::write(std::ostream& out, Value const& root)
{
    document_ = &out;
    addChildValues_ = false;
    indentString_ = "";
    writeValue(root);
    *document_ << "\n";
    document_ = nullptr;  // Forget the stream, for safety.
}

/** Recursively write the styled JSON representation of @a value to the stream.
 *
 *  Mirrors `StyledWriter::writeValue` but emits directly to `*document_`
 *  rather than accumulating into a string. Arrays delegate to
 *  `writeArrayValue`; objects always expand to one member per indented line.
 *
 *  @param value The node to serialize.
 */
void
StyledStreamWriter::writeValue(Value const& value)
{
    switch (value.type())
    {
        case ValueType::Null:
            pushValue("null");
            break;

        case ValueType::Int:
            pushValue(valueToString(value.asInt()));
            break;

        case ValueType::UInt:
            pushValue(valueToString(value.asUInt()));
            break;

        case ValueType::Real:
            pushValue(valueToString(value.asDouble()));
            break;

        case ValueType::String:
            pushValue(valueToQuotedString(value.asCString()));
            break;

        case ValueType::Boolean:
            pushValue(valueToString(value.asBool()));
            break;

        case ValueType::Array:
            writeArrayValue(value);
            break;

        case ValueType::Object: {
            Value::Members members(value.getMemberNames());

            if (members.empty())
            {
                pushValue("{}");
            }
            else
            {
                writeWithIndent("{");
                indent();
                Value::Members::iterator it = members.begin();

                while (true)
                {
                    std::string const& name = *it;
                    Value const& childValue = value[name];
                    writeWithIndent(valueToQuotedString(name.c_str()));
                    *document_ << " : ";
                    writeValue(childValue);

                    if (++it == members.end())
                        break;

                    *document_ << ",";
                }

                unindent();
                writeWithIndent("}");
            }
        }
        break;
    }
}

/** Write an array value to the stream, choosing single-line or multi-line layout.
 *
 *  Mirrors `StyledWriter::writeArrayValue`. An empty array emits `[]`.
 *  Non-empty arrays consult `isMultilineArray` and either expand one
 *  element per line or emit `[ e1, e2, ... ]` using the pre-rendered
 *  strings in `childValues_`.
 *
 *  @param value An array-typed `Value` node.
 */
void
StyledStreamWriter::writeArrayValue(Value const& value)
{
    unsigned const size = value.size();

    if (size == 0)
    {
        pushValue("[]");
    }
    else
    {
        bool const isArrayMultiLine = isMultilineArray(value);

        if (isArrayMultiLine)
        {
            writeWithIndent("[");
            indent();
            bool const hasChildValue = !childValues_.empty();
            unsigned index = 0;

            while (true)
            {
                Value const& childValue = value[index];

                if (hasChildValue)
                {
                    writeWithIndent(childValues_[index]);
                }
                else
                {
                    writeIndent();
                    writeValue(childValue);
                }

                if (++index == size)
                    break;

                *document_ << ",";
            }

            unindent();
            writeWithIndent("]");
        }
        else  // output on a single line
        {
            XRPL_ASSERT(
                childValues_.size() == size,
                "json::StyledStreamWriter::writeArrayValue : child size match");
            *document_ << "[ ";

            for (unsigned index = 0; index < size; ++index)
            {
                if (index > 0)
                    *document_ << ", ";

                *document_ << childValues_[index];
            }

            *document_ << " ]";
        }
    }
}

/** Determine whether @a value should be rendered as a multi-line array.
 *
 *  Identical heuristic to `StyledWriter::isMultilineArray`: quick size
 *  check first, then a dry-run pass using `addChildValues_` to capture
 *  rendered element widths in `childValues_` for reuse during final output.
 *
 *  @param value An array-typed `Value` node.
 *  @return True if the array should be rendered with one element per line.
 */
bool
StyledStreamWriter::isMultilineArray(Value const& value)
{
    int const size = value.size();
    bool isMultiLine = size * 3 >= rightMargin_;
    childValues_.clear();

    for (int index = 0; index < size && !isMultiLine; ++index)
    {
        Value const& childValue = value[index];
        isMultiLine = isMultiLine ||
            ((childValue.isArray() || childValue.isObject()) && childValue.size() > 0);
    }

    if (!isMultiLine)  // check if line length > max line length
    {
        childValues_.reserve(size);
        addChildValues_ = true;
        int lineLength = 4 + ((size - 1) * 2);  // '[ ' + ', '*n + ' ]'

        for (int index = 0; index < size; ++index)
        {
            writeValue(value[index]);
            lineLength += int(childValues_[index].length());
        }

        addChildValues_ = false;
        isMultiLine = isMultiLine || lineLength >= rightMargin_;
    }

    return isMultiLine;
}

/** Write @a value to the stream, or capture it for measurement.
 *
 *  When `addChildValues_` is set (during the `isMultilineArray` dry run),
 *  the string is pushed onto `childValues_`. Otherwise it is written
 *  directly to `*document_`.
 *
 *  @param value The serialized scalar or composite string to emit.
 */
void
StyledStreamWriter::pushValue(std::string const& value)
{
    if (addChildValues_)
    {
        childValues_.push_back(value);
    }
    else
    {
        *document_ << value;
    }
}

/** Emit `'\n'` followed by the current indent string to the stream.
 *
 *  Unlike `StyledWriter::writeIndent`, this implementation unconditionally
 *  emits the newline without checking whether the last character was
 *  already a space or newline. This simplification is acceptable because
 *  the stream variant is used for final human-readable output rather than
 *  intermediate string composition.
 */
void
StyledStreamWriter::writeIndent()
{
    *document_ << '\n' << indentString_;
}

/** Emit a newline+indent then write @a value to the stream.
 *
 *  @param value The string to write after the indent.
 */
void
StyledStreamWriter::writeWithIndent(std::string const& value)
{
    writeIndent();
    *document_ << value;
}

/** Increase the current indentation level by one `indentation_` unit. */
void
StyledStreamWriter::indent()
{
    indentString_ += indentation_;
}

/** Decrease the current indentation level by one `indentation_` unit. */
void
StyledStreamWriter::unindent()
{
    XRPL_ASSERT(
        indentString_.size() >= indentation_.size(),
        "json::StyledStreamWriter::unindent : maximum indent size");
    indentString_.resize(indentString_.size() - indentation_.size());
}

/** Stream @a root as styled, human-readable JSON using `StyledStreamWriter`.
 *
 *  This is the format produced when a `json::Value` appears in log output
 *  or anywhere an undecorated stream insertion operator is used. For
 *  compact single-line output, use `json::Compact{std::move(jv)}` instead.
 *
 *  @param sout The output stream.
 *  @param root The value tree to serialize.
 *  @return @a sout, to allow chaining.
 */
std::ostream&
operator<<(std::ostream& sout, Value const& root)
{
    json::StyledStreamWriter writer;
    writer.write(sout, root);
    return sout;
}

}  // namespace json
