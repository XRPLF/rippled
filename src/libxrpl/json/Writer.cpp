#include <xrpl/json/Writer.h>

#include <xrpl/basics/ToString.h>
#include <xrpl/json/Output.h>
#include <xrpl/basics/TraceLog.h>

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

std::map<char, char const*> gJsonSpecialCharacterEscape = {
    {'"', "\\\""},
    {'\\', "\\\\"},
    {'/', "\\/"},
    {'\b', "\\b"},
    {'\f', "\\f"},
    {'\n', "\\n"},
    {'\r', "\\r"},
    {'\t', "\\t"}};

size_t const kJSON_ESCAPE_LENGTH = 2;

// All other JSON punctuation.
char const kCLOSE_BRACE = '}';
char const kCLOSE_BRACKET = ']';
char const kCOLON = ':';
char const kCOMMA = ',';
char const kOPEN_BRACE = '{';
char const kOPEN_BRACKET = '[';
char const kQUOTE = '"';

auto const kINTEGRAL_FLOATS_BECOME_INTS = false;

size_t
lengthWithoutTrailingZeros(std::string const& s)
{
    TRACE_FUNC();
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

class Writer::Impl
{
public:
    explicit Impl(Output output) : output_(std::move(output))
    {
    }
    ~Impl() = default;

    Impl(Impl&&) = delete;
    Impl&
    operator=(Impl&&) = delete;

    [[nodiscard]] bool
    empty() const
    {
    TRACE_FUNC();
        return stack_.empty();
    }

    void
    start(CollectionType ct)
    {
    TRACE_FUNC();
        char const ch = (ct == CollectionType::Array) ? kOPEN_BRACKET : kOPEN_BRACE;
        output({&ch, 1});
        stack_.emplace(Collection{.type = ct});
    }

    void
    output(boost::beast::string_view const& bytes)
    {
    TRACE_FUNC();
        markStarted();
        output_(bytes);
    }

    void
    stringOutput(boost::beast::string_view const& bytes)
    {
    TRACE_FUNC();
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

    void
    markStarted()
    {
    TRACE_FUNC();
        check(!isFinished(), "isFinished() in output.");
        isStarted_ = true;
    }

    void
    nextCollectionEntry(CollectionType type, std::string const& message)
    {
    TRACE_FUNC();
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

    void
    writeObjectTag(std::string const& tag)
    {
    TRACE_FUNC();
#ifndef NDEBUG
        // Make sure we haven't already seen this tag.
        auto& tags = stack_.top().tags;
        check(!tags.contains(tag), "Already seen tag " + tag);
        tags.insert(tag);
#endif

        stringOutput(tag);
        output_({&kCOLON, 1});
    }

    [[nodiscard]] bool
    isFinished() const
    {
    TRACE_FUNC();
        return isStarted_ && empty();
    }

    void
    finish()
    {
    TRACE_FUNC();
        check(!empty(), "Empty stack in finish()");

        auto isArray = stack_.top().type == CollectionType::Array;
        auto ch = isArray ? kCLOSE_BRACKET : kCLOSE_BRACE;
        output_({&ch, 1});
        stack_.pop();
    }

    void
    finishAll()
    {
    TRACE_FUNC();
        if (isStarted_)
        {
            while (!isFinished())
                finish();
        }
    }

    [[nodiscard]] Output const&
    getOutput() const
    {
    TRACE_FUNC();
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

Writer::Writer(Output const& output) : impl_(std::make_unique<Impl>(output))
{
}

Writer::~Writer()
{
    TRACE_FUNC();
    if (impl_)
        impl_->finishAll();
}

Writer::Writer(Writer&& w) noexcept
{
    TRACE_FUNC();
    impl_ = std::move(w.impl_);
}

Writer&
Writer::operator=(Writer&& w) noexcept
{
    TRACE_FUNC();
    impl_ = std::move(w.impl_);
    return *this;
}

void
Writer::output(char const* s)
{
    TRACE_FUNC();
    impl_->stringOutput(s);
}

void
Writer::output(std::string const& s)
{
    TRACE_FUNC();
    impl_->stringOutput(s);
}

void
Writer::output(json::Value const& value)
{
    TRACE_FUNC();
    impl_->markStarted();
    outputJson(value, impl_->getOutput());
}

void
Writer::output(float f)
{
    TRACE_FUNC();
    auto s = xrpl::to_string(f);
    impl_->output({s.data(), lengthWithoutTrailingZeros(s)});
}

void
Writer::output(double f)
{
    TRACE_FUNC();
    auto s = xrpl::to_string(f);
    impl_->output({s.data(), lengthWithoutTrailingZeros(s)});
}

void
Writer::output(std::nullptr_t)
{
    TRACE_FUNC();
    impl_->output("null");
}

void
Writer::output(bool b)
{
    TRACE_FUNC();
    impl_->output(b ? "true" : "false");
}

void
Writer::implOutput(std::string const& s)
{
    TRACE_FUNC();
    impl_->output(s);
}

void
Writer::finishAll()
{
    TRACE_FUNC();
    if (impl_)
        impl_->finishAll();
}

void
Writer::rawAppend()
{
    TRACE_FUNC();
    impl_->nextCollectionEntry(CollectionType::Array, "append");
}

void
Writer::rawSet(std::string const& tag)
{
    TRACE_FUNC();
    check(!tag.empty(), "Tag can't be empty");

    impl_->nextCollectionEntry(CollectionType::Object, "set");
    impl_->writeObjectTag(tag);
}

void
Writer::startRoot(CollectionType type)
{
    TRACE_FUNC();
    impl_->start(type);
}

void
Writer::startAppend(CollectionType type)
{
    TRACE_FUNC();
    impl_->nextCollectionEntry(CollectionType::Array, "startAppend");
    impl_->start(type);
}

void
Writer::startSet(CollectionType type, std::string const& key)
{
    TRACE_FUNC();
    impl_->nextCollectionEntry(CollectionType::Object, "startSet");
    impl_->writeObjectTag(key);
    impl_->start(type);
}

void
Writer::finish()
{
    TRACE_FUNC();
    if (impl_)
        impl_->finish();
}

}  // namespace json
