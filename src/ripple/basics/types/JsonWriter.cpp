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

#include <ripple/basics/types/JsonWriter.h>
#include <ripple/basics/utility/PlatformMacros.h>

namespace ripple {
namespace json {

namespace {

// JSON characters which need escaping.
char const charactersToEscape[] = "\"\\/\b\f\n\r\t";

// The escaped versions - all two characters.
char const* const escapings[] = {
    "\\\"",
    "\\\\",
    "\\/",
    "\\b",
    "\\f",
    "\\n",
    "\\r",
    "\\t",
};

static_assert (strlen (charactersToEscape) == RIPPLE_ARRAYSIZE (escapings),
               "Different number of characters and escapes.");

inline void checkNotFinished (Writer& w)
{
#ifdef CHECK_JSON_WRITER
    assert (!w.isFinished());
#endif
}

int lengthWithoutTrailingZeros (std::string const& s)
{
    int size = s.size();
    int i = size - 1;
    for (; i >= 0 && s[i] == '0'; --i);
    if (i == (size - 1))
        return size;  // No trailing zeroes.

    int lastNonZero = i;
    for (; i >= 0 && s[i] != '.'; --i);
    if (i < 0)
        return size;  // No decimal place.
    return lastNonZero  + 1;
}

char startSymbol (Writer::CollectionType t)
{
    return t == Writer::CollectionType::array ? '[' : '{';
}

char finishSymbol (Writer::CollectionType t)
{
    return t == Writer::CollectionType::array ? ']' : '}';
}

} // namespace

Writer::~Writer()
{
#ifdef CHECK_JSON_WRITER
    assert (isFinished ());
#endif
}

bool Writer::isFinished() const
{
    return isStarted_ && stack_.empty();
}

void Writer::rawWrite (char const* data, size_t length)
{
    checkNotFinished (*this);
    isStarted_ = true;

    writable_.write (data, length);
}

void Writer::write (std::string const& s)
{
    checkNotFinished (*this);
    isStarted_ = true;

    char const* data = s.data();
    size_t size = s.size();
    size_t position = 0, writtenUntil = 0;
    static const char quote = '"';

    writable_.write (&quote, 1);
    for (; position < size; ++position)
    {
        if (auto pos = strchr (charactersToEscape, s[position]))
        {
            if (writtenUntil < position)
                writable_.write (data + writtenUntil, position - writtenUntil);
            writable_.write (escapings[pos - charactersToEscape], 2);
            writtenUntil = position + 1;
        };
    }
    if (writtenUntil < position)
        writable_.write (data + writtenUntil, position - writtenUntil);
    writable_.write (&quote, 1);
}

void Writer::write (float f)
{
    auto s = to_string (f);
    rawWrite (s.data (), lengthWithoutTrailingZeros (s));
}

void Writer::write (double f)
{
    auto s = to_string (f);
    rawWrite (s.data (), lengthWithoutTrailingZeros (s));
}

void Writer::write (nullptr_t)
{
    rawWrite ("null", strlen("null"));
}

void Writer::rawStart (CollectionType type)
{
    char ch = startSymbol (type);
    rawWrite (&ch, 1);
    stack_.push (Collection());
    stack_.top().type = type;
}

void Writer::start (CollectionType type)
{
#ifdef CHECK_JSON_WRITER
    assert (stack_.empty() || stack_.top().type == CollectionType::array);
#endif
    writeCommaBeforeEntry ();
    rawStart (type);
}

void Writer::start (std::string const& tag, CollectionType type)
{
    writeCommaBeforeEntry ();
    writeObjectTag (tag);
    rawStart (type);
}

void Writer::finish ()
{
#ifdef CHECK_JSON_WRITER
    assert (!stack_.empty());
#endif
    if (!stack_.empty ()) {
        char ch = finishSymbol (stack_.top().type);
        rawWrite (&ch, 1);
        stack_.pop();
    }
}

void Writer::finishAll ()
{
    if (isStarted_)
    {
        while (!isFinished())
            finish();
    }
}

void Writer::writeCommaBeforeEntry ()
{
    if (!stack_.empty())
    {
        static const char comma = ',';
        if (stack_.top ().isFirst)
            stack_.top ().isFirst = false;
        else
            rawWrite (&comma, 1);
        return;
    }
}

void Writer::writeObjectTag(std::string const& tag)
{
#ifdef CHECK_JSON_WRITER
    // Make sure we haven't already seen this tag.
    assert (!stack_.empty());
    auto tags = stack_.top ().tags;
    assert (tags.find (tag) == tags.end ());
#endif
    write (tag);

    static const char colon = ':';
    rawWrite (&colon, 1);
}

} // json


struct StringWritable : public Writable
{
    void write (char const* data, size_t length) override
    {
        output.append (data, length);
    }

    std::string output;
};

class JsonWriter_test : public beast::unit_test::suite
{
public:
    StringWritable writable_;
    std::unique_ptr <json::Writer> writer_;

    void setup (std::string const& testName)
    {
        testcase (testName);
        writable_.output.clear();
        writer_ = std::make_unique <json::Writer>(writable_);
    }

    void expectFinished (bool finished = true)
    {
        expect (writer_->isFinished() == finished,
                std::string ("isFinished=") + (!finished ? "true" : "false"));
    }

    // Test the result and report values.
    void expectResult (std::string const& result)
    {
        if (result != "")
            expectFinished();
        expect (writable_.output == result,
                writable_.output + " != " + result);
    }

    void testTrivial ()
    {
        setup ("trivial");
        expectFinished (false);
        expect (writable_.output.empty());
        writer_->write(0);
        expectFinished (true);
        expectResult("0");
    }

    void testPrimitives ()
    {
        setup ("true");
        writer_->write (true);
        expectResult ("true");

        setup ("false");
        writer_->write (false);
        expectResult ("false");

        setup ("23");
        writer_->write (23);
        expectResult ("23");

        setup ("23.5");
        writer_->write (23.5);
        expectResult ("23.5");

        setup ("a string");
        writer_->write ("a string");
        expectResult ("\"a string\"");

        setup ("nullptr");
        writer_->write (nullptr);
        expectResult ("null");
    }

    void testEmpty ()
    {
        setup ("empty array");
        writer_->startArray ();
        writer_->finish ();
        expectResult ("[]");

        setup ("empty object");
        writer_->startObject ();
        writer_->finish ();
        expectResult ("{}");
    }

    void testEscaping ()
    {
        setup ("backslash");
        writer_->write ("\\");
        expectResult ("\"\\\\\"");

        setup ("quote");
        writer_->write ("\"");
        expectResult ("\"\\\"\"");

        setup ("backslash and quote");
        writer_->write ("\\\"");
        expectResult ("\"\\\\\\\"\"");

        setup ("escape embedded");
        writer_->write ("this contains a \\ in the middle of it.");
        expectResult ("\"this contains a \\\\ in the middle of it.\"");

        setup ("remaining escapes");
        writer_->write ("\b\f\n\r\t");
        expectResult ("\"\\b\\f\\n\\r\\t\"");
    }

    void testArray ()
    {
        setup ("empty array");
        writer_->startArray ();
        writer_->append (12);
        writer_->finish ();
        expectResult ("[12]");
    }

    void testLongArray ()
    {
        setup ("long array");
        writer_->startArray ();
        writer_->append (12);
        writer_->append (true);
        writer_->append ("hello");
        writer_->finish ();
        expectResult ("[12,true,\"hello\"]");
    }

    void testEmbeddedArraySimple ()
    {
        setup ("embedded array simple");
        writer_->startArray ();
        writer_->startArray ();
        writer_->finishAll ();
        expectResult("[[]]");
    }

    void testObject ()
    {
        setup ("object");
        writer_->startObject ();
        writer_->set ("hello", "world");
        writer_->finish ();
        expectResult ("{\"hello\":\"world\"}");
    }

    void testComplexObject ()
    {
        setup ("complex object");
        writer_->startObject ();
        writer_->set ("hello", "world");
        writer_->startArray ("array");
        writer_->append (true);
        writer_->append (12);
        writer_->startArray ();
        writer_->startObject();
        writer_->set ("goodbye", "cruel world.");
        writer_->startArray("subarray");
        writer_->append(23.5);
        writer_->finishAll ();
        expectResult ("{\"hello\":\"world\",\"array\":[true,12,"
                      "[{\"goodbye\":\"cruel world.\",\"subarray\":[23.5]}]]}");
    }

    void run () override
    {
        testTrivial ();
        testPrimitives ();
        testEmpty ();
        testEscaping ();
        testArray ();
        testLongArray ();
        testEmbeddedArraySimple ();
        testObject ();
        testComplexObject ();
    }
};

BEAST_DEFINE_TESTSUITE(JsonWriter, ripple_basics, ripple);

} // ripple
