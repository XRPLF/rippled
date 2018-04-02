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

#include <ripple/json/Output.h>
#include <ripple/json/Writer.h>
#include <stack>
#include <set>

namespace Json {

namespace {

std::map <char, const char*> jsonSpecialCharacterEscape = {
    {'"',  "\\\""},
    {'\\', "\\\\"},
    {'/',  "\\/"},
    {'\b', "\\b"},
    {'\f', "\\f"},
    {'\n', "\\n"},
    {'\r', "\\r"},
    {'\t', "\\t"}
};

static size_t const jsonEscapeLength = 2;

// All other JSON punctuation.
const char closeBrace = '}';
const char closeBracket = ']';
const char colon = ':';
const char comma = ',';
const char openBrace = '{';
const char openBracket = '[';
const char quote = '"';

const std::string none;

static auto const integralFloatsBecomeInts = false;

size_t lengthWithoutTrailingZeros (std::string const& s)
{
    auto dotPos = s.find ('.');
    if (dotPos == std::string::npos)
        return s.size();

    auto lastNonZero = s.find_last_not_of ('0');
    auto hasDecimals = dotPos != lastNonZero;

    if (hasDecimals)
        return lastNonZero + 1;

    if (integralFloatsBecomeInts || lastNonZero + 2 > s.size())
        return lastNonZero;

    return lastNonZero + 2;
}

} // namespace

class Writer::Impl
{
public:
    explicit
    Impl (Output const& output) : output_(output) {}
    ~Impl() = default;

    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    bool empty() const { return stack_.empty (); }

    void start (CollectionType ct)
    {
        char ch = (ct == array) ? openBracket : openBrace;
        output ({&ch, 1});
        stack_.push (Collection());
        stack_.top().type = ct;
    }

    void output (beast::string_view const& bytes)
    {
        markStarted ();
        output_ (bytes);
    }

    void stringOutput (beast::string_view const& bytes)
    {
        markStarted ();
        std::size_t position = 0, writtenUntil = 0;

        output_ ({&quote, 1});
        auto data = bytes.data();
        for (; position < bytes.size(); ++position)
        {
            auto i = jsonSpecialCharacterEscape.find (data[position]);
            if (i != jsonSpecialCharacterEscape.end ())
            {
                if (writtenUntil < position)
                {
                    output_ ({data + writtenUntil, position - writtenUntil});
                }
                output_ ({i->second, jsonEscapeLength});
                writtenUntil = position + 1;
            };
        }
        if (writtenUntil < position)
            output_ ({data + writtenUntil, position - writtenUntil});
        output_ ({&quote, 1});
    }

    void markStarted ()
    {
        check (!isFinished(), "isFinished() in output.");
        isStarted_ = true;
    }

    void nextCollectionEntry (CollectionType type, std::string const& message)
    {
        check (!empty() , "empty () in " + message);

        auto t = stack_.top ().type;
        if (t != type)
        {
            check (false, "Not an " +
                   ((type == array ? "array: " : "object: ") + message));
        }
        if (stack_.top ().isFirst)
            stack_.top ().isFirst = false;
        else
            output_ ({&comma, 1});
    }

    void writeObjectTag (std::string const& tag)
    {
#ifndef NDEBUG
        // Make sure we haven't already seen this tag.
        auto& tags = stack_.top ().tags;
        check (tags.find (tag) == tags.end (), "Already seen tag " + tag);
        tags.insert (tag);
#endif

        stringOutput (tag);
        output_ ({&colon, 1});
    }

    bool isFinished() const
    {
        return isStarted_ && empty();
    }

    void finish ()
    {
        check (!empty(), "Empty stack in finish()");

        auto isArray = stack_.top().type == array;
        auto ch = isArray ? closeBracket : closeBrace;
        output_ ({&ch, 1});
        stack_.pop();
    }

    void finishAll ()
    {
        if (isStarted_)
        {
            while (!isFinished())
                finish();
        }
    }

    Output const& getOutput() const { return output_; }

private:
    // JSON collections are either arrrays, or objects.
    struct Collection
    {
        explicit Collection() = default;

        /** What type of collection are we in? */
        Writer::CollectionType type;

        /** Is this the first entry in a collection?
         *  If false, we have to emit a , before we write the next entry. */
        bool isFirst = true;

#ifndef NDEBUG
        /** What tags have we already seen in this collection? */
        std::set <std::string> tags;
#endif
    };

    using Stack = std::stack <Collection, std::vector<Collection>>;

    Output output_;
    Stack stack_;

    bool isStarted_ = false;
};

Writer::Writer (Output const &output)
        : impl_(std::make_unique <Impl> (output))
{
}

Writer::~Writer()
{
    if (impl_)
        impl_->finishAll ();
}

Writer::Writer(Writer&& w) noexcept
{
    impl_ = std::move (w.impl_);
}

Writer& Writer::operator=(Writer&& w) noexcept
{
    impl_ = std::move (w.impl_);
    return *this;
}

void Writer::output (char const* s)
{
    impl_->stringOutput (s);
}

void Writer::output (std::string const& s)
{
    impl_->stringOutput (s);
}

void Writer::output (Json::Value const& value)
{
    impl_->markStarted();
    outputJson (value, impl_->getOutput());
}

void Writer::output (float f)
{
    auto s = ripple::to_string (f);
    impl_->output ({s.data (), lengthWithoutTrailingZeros (s)});
}

void Writer::output (double f)
{
    auto s = ripple::to_string (f);
    impl_->output ({s.data (), lengthWithoutTrailingZeros (s)});
}

void Writer::output (std::nullptr_t)
{
    impl_->output ("null");
}

void Writer::output (bool b)
{
    impl_->output (b ? "true" : "false");
}

void Writer::implOutput (std::string const& s)
{
    impl_->output (s);
}

void Writer::finishAll ()
{
    if (impl_)
        impl_->finishAll ();
}

void Writer::rawAppend()
{
    impl_->nextCollectionEntry (array, "append");
}

void Writer::rawSet (std::string const& tag)
{
    check (!tag.empty(), "Tag can't be empty");

    impl_->nextCollectionEntry (object, "set");
    impl_->writeObjectTag (tag);
}

void Writer::startRoot (CollectionType type)
{
    impl_->start (type);
}

void Writer::startAppend (CollectionType type)
{
    impl_->nextCollectionEntry (array, "startAppend");
    impl_->start (type);
}

void Writer::startSet (CollectionType type, std::string const& key)
{
    impl_->nextCollectionEntry (object, "startSet");
    impl_->writeObjectTag (key);
    impl_->start (type);
}

void Writer::finish ()
{
    if (impl_)
        impl_->finish ();
}

} // Json
