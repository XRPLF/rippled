/** @file
 *  Implements the streaming JSON Writer.
 *
 *  All state and logic live in the private Writer::Impl class (Pimpl idiom).
 *  The public Writer shell holds only a std::unique_ptr<Impl>, so callers
 *  depend on the public header without exposure to the internal collection
 *  stack or string-escape map.
 */

#include <xrpl/json/Writer.h>

#include <xrpl/basics/ToString.h>
#include <xrpl/json/Output.h>

#include <cstddef>
#include <map>
#include <memory>
#include <set>  // IWYU pragma: keep
#include <stack>
#include <string>
#include <utility>
#include <vector>

namespace json {

namespace {

/** Lookup table mapping each JSON-special character to its two-character
 *  escape sequence.
 *
 *  Covers the eight characters required by RFC 8259 §7: `"`, `\`, `/`,
 *  backspace, form-feed, newline, carriage-return, and tab.
 */
std::map<char, char const*> gJsonSpecialCharacterEscape = {
    {'"', "\\\""},
    {'\\', "\\\\"},
    {'/', "\\/"},
    {'\b', "\\b"},
    {'\f', "\\f"},
    {'\n', "\\n"},
    {'\r', "\\r"},
    {'\t', "\\t"}};

/** Byte length of every escape sequence in gJsonSpecialCharacterEscape. */
size_t const kJSON_ESCAPE_LENGTH = 2;

// All other JSON punctuation.
char const kCLOSE_BRACE = '}';
char const kCLOSE_BRACKET = ']';
char const kCOLON = ':';
char const kCOMMA = ',';
char const kOPEN_BRACE = '{';
char const kOPEN_BRACKET = '[';
char const kQUOTE = '"';

/** Controls whether whole-number floats are serialized without a decimal point.
 *
 *  Hard-coded `false` so that `3.0` is emitted as `"3.0"` rather than `"3"`,
 *  preserving the float type hint for consumers. The named constant exists to
 *  document that this was a deliberate, considered choice.
 */
auto const kINTEGRAL_FLOATS_BECOME_INTS = false;

/** Return the used length of a float string after trimming trailing zeros.
 *
 *  If the string contains no decimal point it is returned unchanged.
 *  Otherwise trailing zeros are stripped; if that would leave only the decimal
 *  point (e.g. `"3."`) one zero is kept, producing `"3.0"`, unless
 *  kINTEGRAL_FLOATS_BECOME_INTS is true, in which case the point itself is
 *  also dropped.
 *
 *  @param s A float string produced by xrpl::to_string().
 *  @return The number of leading bytes of `s` that should be emitted.
 */
size_t
lengthWithoutTrailingZeros(std::string const& s)
{
    auto dotPos = s.find('.');
    if (dotPos == std::string::npos)
        return s.size();

    auto lastNonZero = s.find_last_not_of('0');
    auto hasDecimals = dotPos != lastNonZero;

    if (hasDecimals)
        return lastNonZero + 1;

    if (kINTEGRAL_FLOATS_BECOME_INTS || lastNonZero + 2 > s.size())
        return lastNonZero;

    return lastNonZero + 2;
}

}  // namespace

/** Private implementation of Writer (Pimpl idiom).
 *
 *  Maintains a stack of open JSON collections (arrays and objects), tracks
 *  comma-separator state, and forwards serialized bytes to the Output sink.
 *  All write operations enforce the write-once contract: once the root
 *  collection is closed, any further write attempt throws std::logic_error.
 *
 *  @note Not movable; ownership is managed exclusively by Writer via
 *      std::unique_ptr<Impl>.
 */
class Writer::Impl
{
public:
    /** Construct an Impl that forwards bytes to @p output. */
    explicit Impl(Output output) : output_(std::move(output))
    {
    }
    ~Impl() = default;

    Impl(Impl&&) = delete;
    Impl&
    operator=(Impl&&) = delete;

    /** Return true if no collection is currently open. */
    [[nodiscard]] bool
    empty() const
    {
        return stack_.empty();
    }

    /** Emit the opening delimiter for @p ct and push a new Collection entry.
     *
     *  @param ct Array emits `[`; Object emits `{`.
     */
    void
    start(CollectionType ct)
    {
        char const ch = (ct == CollectionType::Array) ? kOPEN_BRACKET : kOPEN_BRACE;
        output({&ch, 1});
        stack_.emplace(Collection{.type = ct});
    }

    /** Forward raw bytes to the output sink, marking the writer as started.
     *
     *  @param bytes Raw JSON fragment to emit verbatim.
     *  @throws std::logic_error if isFinished() is true.
     */
    void
    output(boost::beast::string_view const& bytes)
    {
        markStarted();
        output_(bytes);
    }

    /** Emit @p bytes as a JSON quoted string, escaping special characters.
     *
     *  Runs of clean ASCII are emitted in a single output call; only bytes
     *  present in gJsonSpecialCharacterEscape break the run.  A typical
     *  unescaped string therefore costs three output calls: `"`, body, `"`.
     *
     *  @param bytes The raw string content to quote and escape.
     *  @throws std::logic_error if isFinished() is true.
     */
    void
    stringOutput(boost::beast::string_view const& bytes)
    {
        markStarted();
        std::size_t position = 0, writtenUntil = 0;

        output_({&kQUOTE, 1});
        auto data = bytes.data();
        for (; position < bytes.size(); ++position)
        {
            auto i = gJsonSpecialCharacterEscape.find(data[position]);
            if (i != gJsonSpecialCharacterEscape.end())
            {
                if (writtenUntil < position)
                {
                    output_({data + writtenUntil, position - writtenUntil});
                }
                output_({i->second, kJSON_ESCAPE_LENGTH});
                writtenUntil = position + 1;
            };
        }
        if (writtenUntil < position)
            output_({data + writtenUntil, position - writtenUntil});
        output_({&kQUOTE, 1});
    }

    /** Assert the writer is not yet finished and set isStarted_ = true.
     *
     *  Called by every code path that emits bytes.  Once the root collection
     *  has been closed, this throws to enforce the write-once contract.
     *
     *  @throws std::logic_error if isFinished() is true.
     */
    void
    markStarted()
    {
        check(!isFinished(), "isFinished() in output.");
        isStarted_ = true;
    }

    /** Validate the current collection type and emit a comma if needed.
     *
     *  Checks that the stack is non-empty and that the innermost collection
     *  matches @p type.  On the first entry in a collection the comma is
     *  suppressed; on all subsequent entries a `,` is emitted.
     *
     *  @param type Expected collection type (Array or Object).
     *  @param message Context string included in the error if checks fail.
     *  @throws std::logic_error if the stack is empty or the types mismatch.
     */
    void
    nextCollectionEntry(CollectionType type, std::string const& message)
    {
        check(!empty(), "empty () in " + message);

        auto t = stack_.top().type;
        if (t != type)
        {
            check(
                false,
                "Not an " + ((type == CollectionType::Array ? "array: " : "object: ") + message));
        }
        if (stack_.top().isFirst)
        {
            stack_.top().isFirst = false;
        }
        else
        {
            output_({&kCOMMA, 1});
        }
    }

    /** Emit an object key followed by `:`, with duplicate-key detection in
     *  debug builds.
     *
     *  @param tag The object key to emit as a quoted, escaped string.
     *  @throws std::logic_error (debug builds only) if @p tag was already
     *      used in the current object.
     */
    void
    writeObjectTag(std::string const& tag)
    {
#ifndef NDEBUG
        // Make sure we haven't already seen this tag.
        auto& tags = stack_.top().tags;
        check(!tags.contains(tag), "Already seen tag " + tag);
        tags.insert(tag);
#endif

        stringOutput(tag);
        output_({&kCOLON, 1});
    }

    /** Return true when the writer has started and all collections are closed.
     *
     *  Once true, any further write attempt will throw via markStarted().
     */
    [[nodiscard]] bool
    isFinished() const
    {
        return isStarted_ && empty();
    }

    /** Emit the closing delimiter of the innermost collection and pop the stack.
     *
     *  @throws std::logic_error if the collection stack is empty.
     */
    void
    finish()
    {
        check(!empty(), "Empty stack in finish()");

        auto isArray = stack_.top().type == CollectionType::Array;
        auto ch = isArray ? kCLOSE_BRACKET : kCLOSE_BRACE;
        output_({&ch, 1});
        stack_.pop();
    }

    /** Close all open collections in innermost-first order.
     *
     *  A no-op if the writer was never started.  Called by ~Writer() to
     *  guarantee a syntactically complete JSON document even when an exception
     *  or early return interrupts the caller's serialization loop.
     */
    void
    finishAll()
    {
        if (isStarted_)
        {
            while (!isFinished())
                finish();
        }
    }

    /** Return the underlying Output sink for use by the public Writer layer. */
    [[nodiscard]] Output const&
    getOutput() const
    {
        return output_;
    }

private:
    // JSON collections are either arrays, or objects.
    struct Collection
    {
        /** What type of collection are we in? */
        Writer::CollectionType type = Writer::CollectionType::Array;

        /** Is this the first entry in a collection?
         *  If false, we have to emit a , before we write the next entry. */
        bool isFirst = true;

#ifndef NDEBUG
        /** What tags have we already seen in this collection? */
        std::set<std::string> tags{};  // NOLINT(readability-redundant-member-init)
#endif
    };

    using Stack = std::stack<Collection, std::vector<Collection>>;

    Output output_;
    Stack stack_;

    bool isStarted_ = false;
};

/** Construct a Writer that sends serialized bytes to @p output. */
Writer::Writer(Output const& output) : impl_(std::make_unique<Impl>(output))
{
}

/** Close all open collections and destroy the writer.
 *
 *  Calls finishAll() so the output stream always ends with a syntactically
 *  valid JSON document, even if the caller threw or returned early.
 */
Writer::~Writer()
{
    if (impl_)
        impl_->finishAll();
}

/** Transfer ownership of the implementation from @p w, leaving it empty. */
Writer::Writer(Writer&& w) noexcept
{
    impl_ = std::move(w.impl_);
}

/** Transfer ownership of the implementation from @p w, leaving it empty.
 *
 *  @return *this
 */
Writer&
Writer::operator=(Writer&& w) noexcept
{
    impl_ = std::move(w.impl_);
    return *this;
}

/** Emit @p s as a JSON quoted, escaped string.
 *
 *  @param s Null-terminated C string to serialize.
 */
void
Writer::output(char const* s)
{
    impl_->stringOutput(s);
}

/** Emit @p s as a JSON quoted, escaped string.
 *
 *  @param s String to serialize.
 */
void
Writer::output(std::string const& s)
{
    impl_->stringOutput(s);
}

/** Serialize @p value by streaming its minimal JSON representation.
 *
 *  Delegates to outputJson() so the entire json::Value tree is written
 *  directly to the output sink without an intermediate string allocation.
 *
 *  @param value The json::Value to serialize.
 */
void
Writer::output(json::Value const& value)
{
    impl_->markStarted();
    outputJson(value, impl_->getOutput());
}

/** Emit @p f as a decimal string with trailing zeros removed.
 *
 *  Integral values such as `3.0` are kept as `"3.0"` (not `"3"`) because
 *  kINTEGRAL_FLOATS_BECOME_INTS is false.
 *
 *  @param f Float value to serialize.
 */
void
Writer::output(float f)
{
    auto s = xrpl::to_string(f);
    impl_->output({s.data(), lengthWithoutTrailingZeros(s)});
}

/** Emit @p f as a decimal string with trailing zeros removed.
 *
 *  Integral values such as `3.0` are kept as `"3.0"` (not `"3"`) because
 *  kINTEGRAL_FLOATS_BECOME_INTS is false.
 *
 *  @param f Double value to serialize.
 */
void
Writer::output(double f)
{
    auto s = xrpl::to_string(f);
    impl_->output({s.data(), lengthWithoutTrailingZeros(s)});
}

/** Emit the JSON literal `null`. */
void
Writer::output(std::nullptr_t)
{
    impl_->output("null");
}

/** Emit the JSON literal `true` or `false`. */
void
Writer::output(bool b)
{
    impl_->output(b ? "true" : "false");
}

/** Emit a pre-formatted value string produced by the template output().
 *
 *  Called by the template overload after converting the value to a string
 *  via std::to_string().  Not intended for direct use by callers.
 *
 *  @param s String representation of the value, emitted verbatim (unquoted).
 */
void
Writer::implOutput(std::string const& s)
{
    impl_->output(s);
}

/** Close all open collections in innermost-first order.
 *
 *  Safe to call on a moved-from Writer (impl_ may be null).
 *  @see ~Writer(), which calls this automatically.
 */
void
Writer::finishAll()
{
    if (impl_)
        impl_->finishAll();
}

/** Prepare to append a value to the current array.
 *
 *  Emits a comma separator if this is not the first element.
 *  Use this when you will emit the value yourself rather than through
 *  append().
 *
 *  @throws std::logic_error if the innermost open collection is not an array.
 */
void
Writer::rawAppend()
{
    impl_->nextCollectionEntry(CollectionType::Array, "append");
}

/** Prepare to set a key-value pair in the current object.
 *
 *  Emits a comma separator if needed, then emits `"key":`.  Use this when
 *  you will emit the value yourself rather than through set().
 *
 *  @param tag The object key; must be non-empty.
 *  @throws std::logic_error if @p tag is empty, the innermost collection is
 *      not an object, or (debug builds) @p tag was already used in this
 *      object.
 */
void
Writer::rawSet(std::string const& tag)
{
    check(!tag.empty(), "Tag can't be empty");

    impl_->nextCollectionEntry(CollectionType::Object, "set");
    impl_->writeObjectTag(tag);
}

/** Open a new top-level collection, emitting `[` or `{`.
 *
 *  Must be the first output call on this writer.
 *
 *  @param type Array or Object.
 */
void
Writer::startRoot(CollectionType type)
{
    impl_->start(type);
}

/** Open a nested collection as the next element of the current array.
 *
 *  Emits a comma if needed, then emits `[` or `{`.
 *
 *  @param type Array or Object.
 *  @throws std::logic_error if the innermost open collection is not an array.
 */
void
Writer::startAppend(CollectionType type)
{
    impl_->nextCollectionEntry(CollectionType::Array, "startAppend");
    impl_->start(type);
}

/** Open a nested collection as the value of a key in the current object.
 *
 *  Emits a comma if needed, then emits `"key":[` or `"key":{`.
 *
 *  @param type Array or Object.
 *  @param key  The object key for this nested collection.
 *  @throws std::logic_error if the innermost open collection is not an object,
 *      or (debug builds) if @p key was already used in this object.
 */
void
Writer::startSet(CollectionType type, std::string const& key)
{
    impl_->nextCollectionEntry(CollectionType::Object, "startSet");
    impl_->writeObjectTag(key);
    impl_->start(type);
}

/** Close the innermost open collection, emitting `]` or `}`.
 *
 *  Safe to call on a moved-from Writer (impl_ may be null).
 *
 *  @throws std::logic_error if the collection stack is empty.
 */
void
Writer::finish()
{
    if (impl_)
        impl_->finish();
}

}  // namespace json
