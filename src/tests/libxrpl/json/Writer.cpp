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

#include <xrpl/json/Writer.h>

#include <doctest/doctest.h>

#include <memory>
#include <string>

using namespace ripple;
using namespace Json;

TEST_SUITE_BEGIN("JsonWriter");

struct WriterFixture
{
    std::string output;
    std::unique_ptr<Writer> writer;

    void
    setup()
    {
        output.clear();
        writer = std::make_unique<Writer>(stringOutput(output));
    }

    void
    expectOutput(std::string const& expected)
    {
        writer.reset();
        CHECK(output == expected);
    }
};

TEST_CASE_FIXTURE(WriterFixture, "trivial")
{
    setup();
    CHECK(output.empty());
    expectOutput("");
}

TEST_CASE_FIXTURE(WriterFixture, "near trivial")
{
    setup();
    CHECK(output.empty());
    writer->output(0);
    expectOutput("0");
}

TEST_CASE_FIXTURE(WriterFixture, "primitives")
{
    setup();
    writer->output(true);
    expectOutput("true");

    setup();
    writer->output(false);
    expectOutput("false");

    setup();
    writer->output(23);
    expectOutput("23");

    setup();
    writer->output(23.0);
    expectOutput("23.0");

    setup();
    writer->output(23.5);
    expectOutput("23.5");

    setup();
    writer->output("a string");
    expectOutput("\"a string\"");

    setup();
    writer->output(nullptr);
    expectOutput("null");
}

TEST_CASE_FIXTURE(WriterFixture, "empty")
{
    setup();
    writer->startRoot(Writer::array);
    writer->finish();
    expectOutput("[]");

    setup();
    writer->startRoot(Writer::object);
    writer->finish();
    expectOutput("{}");
}

TEST_CASE_FIXTURE(WriterFixture, "escaping")
{
    setup();
    writer->output("\\");
    expectOutput("\"\\\\\"");

    setup();
    writer->output("\"");
    expectOutput("\"\\\"\"");

    setup();
    writer->output("\\\"");
    expectOutput("\"\\\\\\\"\"");

    setup();
    writer->output("this contains a \\ in the middle of it.");
    expectOutput("\"this contains a \\\\ in the middle of it.\"");

    setup();
    writer->output("\b\f\n\r\t");
    expectOutput("\"\\b\\f\\n\\r\\t\"");
}

TEST_CASE_FIXTURE(WriterFixture, "array")
{
    setup();
    writer->startRoot(Writer::array);
    writer->append(12);
    writer->finish();
    expectOutput("[12]");
}

TEST_CASE_FIXTURE(WriterFixture, "long array")
{
    setup();
    writer->startRoot(Writer::array);
    writer->append(12);
    writer->append(true);
    writer->append("hello");
    writer->finish();
    expectOutput("[12,true,\"hello\"]");
}

TEST_CASE_FIXTURE(WriterFixture, "embedded array simple")
{
    setup();
    writer->startRoot(Writer::array);
    writer->startAppend(Writer::array);
    writer->finish();
    writer->finish();
    expectOutput("[[]]");
}

TEST_CASE_FIXTURE(WriterFixture, "object")
{
    setup();
    writer->startRoot(Writer::object);
    writer->set("hello", "world");
    writer->finish();
    expectOutput("{\"hello\":\"world\"}");
}

TEST_CASE_FIXTURE(WriterFixture, "complex object")
{
    setup();
    writer->startRoot(Writer::object);
    writer->set("hello", "world");
    writer->startSet(Writer::array, "array");
    writer->append(true);
    writer->append(12);
    writer->startAppend(Writer::array);
    writer->startAppend(Writer::object);
    writer->set("goodbye", "cruel world.");
    writer->startSet(Writer::array, "subarray");
    writer->append(23.5);
    writer->finishAll();
    expectOutput(
        "{\"hello\":\"world\",\"array\":[true,12,[{\"goodbye\":\"cruel "
        "world.\",\"subarray\":[23.5]}]]}");
}

TEST_CASE_FIXTURE(WriterFixture, "json value")
{
    setup();
    Json::Value value(Json::objectValue);
    value["foo"] = 23;
    writer->startRoot(Writer::object);
    writer->set("hello", value);
    writer->finish();
    expectOutput("{\"hello\":{\"foo\":23}}");
}

TEST_SUITE_END();
