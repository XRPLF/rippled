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

#include <ripple/basics/ArraySize.h>
#include <ripple/rpc/impl/JsonWriter.h>
#include <ripple/rpc/impl/WriteJson.h>
#include <beast/unit_test/suite.h>

namespace ripple {
namespace RPC {

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

size_t lengthWithoutTrailingZeros (std::string const& s)
{
    if (s.find ('.') == std::string::npos)
        return s.size();

    return s.find_last_not_of ('0') + 1;
}

} // namespace

class Writer::Impl
{
public:
    Impl (Output const& output) : output_(output) {}

    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    bool empty() const { return stack_.empty (); }

    void start (CollectionType ct)
    {
        char ch = (ct == array) ? openBracket : openBrace;
        output (ch);
        stack_.push (Collection());
        stack_.top().type = ct;
    }

    void output (Bytes const& bytes)
    {
        markStarted ();
        output_ (bytes);
    }

    void stringOutput (Bytes const& bytes)
    {
        markStarted ();
        std::size_t position = 0, writtenUntil = 0;

        output_ (quote);
        auto data = bytes.data;
        for (; position < bytes.size; ++position)
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
        output_ (quote);
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
            output_ (comma);
    }

    void writeObjectTag (std::string const& tag)
    {
#ifdef DEBUG
        // Make sure we haven't already seen this tag.
        auto& tags = stack_.top ().tags;
        check (tags.find (tag) == tags.end (), "Already seen tag " + tag);
        tags.insert (tag);
#endif

        stringOutput (tag);
        output_ (colon);
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
        output_ (ch);
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
        /** What type of collection are we in? */
        Writer::CollectionType type;

        /** Is this the first entry in a collection?
         *  If false, we have to emit a , before we write the next entry. */
        bool isFirst = true;

#ifdef DEBUG
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
    impl_->finishAll ();
}

Writer::Writer(Writer&& w)
{
    impl_ = std::move (w.impl_);
}

Writer& Writer::operator=(Writer&& w)
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
    writeJson (value, impl_->getOutput());
}

template <>
void Writer::output (float f)
{
    auto s = to_string (f);
    impl_->output ({s.data (), lengthWithoutTrailingZeros (s)});
}

template <>
void Writer::output (double f)
{
    auto s = to_string (f);
    impl_->output ({s.data (), lengthWithoutTrailingZeros (s)});
}

template <>
void Writer::output (std::nullptr_t)
{
    impl_->output ("null");
}

void Writer::implOutput (std::string const& s)
{
    impl_->output (s);
}

void Writer::finishAll ()
{
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
    impl_->finish ();
}

} // RPC
} // ripple
