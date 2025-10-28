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
#include <google/protobuf/stubs/port.h>

#include <memory>
#include <string>

using namespace ripple;
using namespace Json;

TEST_SUITE_BEGIN("JsonWriter");

struct WriterFixture
{
    std::string output;
    std::unique_ptr<Writer> writer;

    WriterFixture()
    {
        writer = std::make_unique<Writer>(stringOutput(output));
    }

    void
    reset()
    {
        output.clear();
        writer = std::make_unique<Writer>(stringOutput(output));
    }

    void
    expectOutput(std::string const& expected) const
    {
        CHECK(output == expected);
    }

    void
    checkOutputAndReset(std::string const& expected)
    {
        expectOutput(expected);
        reset();
    }
};

TEST_CASE_FIXTURE(WriterFixture, "trivial")
{
    CHECK(output.empty());
    checkOutputAndReset("");
}

TEST_CASE_FIXTURE(WriterFixture, "near trivial")
{
    CHECK(output.empty());
    writer->output(0);
    checkOutputAndReset("0");
}

TEST_CASE_FIXTURE(WriterFixture, "primitives")
{
    writer->output(true);
    checkOutputAndReset("true");

    writer->output(false);
    checkOutputAndReset("false");

    writer->output(23);
    checkOutputAndReset("23");

    writer->output(23.0);
    checkOutputAndReset("23.0");

    writer->output(23.5);
    checkOutputAndReset("23.5");

    writer->output("a string");
    checkOutputAndReset(R"("a string")");

    writer->output(nullptr);
    checkOutputAndReset("null");
}

TEST_CASE_FIXTURE(WriterFixture, "empty")
{
    writer->startRoot(Writer::array);
    writer->finish();
    checkOutputAndReset("[]");

    writer->startRoot(Writer::object);
    writer->finish();
    checkOutputAndReset("{}");
}

TEST_CASE_FIXTURE(WriterFixture, "escaping")
{
    writer->output("\\");
    checkOutputAndReset(R"("\\")");

    writer->output("\"");
    checkOutputAndReset(R"("\"")");

    writer->output("\\\"");
    checkOutputAndReset(R"("\\\"")");

    writer->output("this contains a \\ in the middle of it.");
    checkOutputAndReset(R"("this contains a \\ in the middle of it.")");

    writer->output("\b\f\n\r\t");
    checkOutputAndReset(R"("\b\f\n\r\t")");
}

TEST_CASE_FIXTURE(WriterFixture, "array")
{
    writer->startRoot(Writer::array);
    writer->append(12);
    writer->finish();
    checkOutputAndReset("[12]");
}

TEST_CASE_FIXTURE(WriterFixture, "long array")
{
    writer->startRoot(Writer::array);
    writer->append(12);
    writer->append(true);
    writer->append("hello");
    writer->finish();
    checkOutputAndReset(R"([12,true,"hello"])");
}

TEST_CASE_FIXTURE(WriterFixture, "embedded array simple")
{
    writer->startRoot(Writer::array);
    writer->startAppend(Writer::array);
    writer->finish();
    writer->finish();
    checkOutputAndReset("[[]]");
}

TEST_CASE_FIXTURE(WriterFixture, "object")
{
    writer->startRoot(Writer::object);
    writer->set("hello", "world");
    writer->finish();
    checkOutputAndReset(R"({"hello":"world"})");
}

TEST_CASE_FIXTURE(WriterFixture, "complex object")
{
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
    checkOutputAndReset(
        R"({"hello":"world","array":[true,12,[{"goodbye":"cruel world.","subarray":[23.5]}]]})");
}

TEST_CASE_FIXTURE(WriterFixture, "json value")
{
    Json::Value value(Json::objectValue);
    value["foo"] = 23;
    writer->startRoot(Writer::object);
    writer->set("hello", value);
    writer->finish();
    checkOutputAndReset(R"({"hello":{"foo":23}})");
}

TEST_SUITE_END();
