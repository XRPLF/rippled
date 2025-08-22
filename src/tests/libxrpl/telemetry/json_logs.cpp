//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012 Ripple Labs Inc.

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

#include <xrpl/basics/Log.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/telemetry/JsonLogs.h>

#include <doctest/doctest.h>

using namespace ripple;

/**
 * @brief sink for writing all log messages to a stringstream
 */
class MockSink : public beast::Journal::Sink
{
    std::stringstream& strm_;

public:
    MockSink(beast::severities::Severity threshold, std::stringstream& strm)
        : beast::Journal::Sink(threshold, false), strm_(strm)
    {
    }

    void
    write(beast::severities::Severity level, std::string const& text) override
    {
        strm_ << text;
    }

    void
    writeAlways(beast::severities::Severity level, std::string const& text)
        override
    {
        strm_ << text;
    }
};

class JsonLogStreamFixture
{
public:
    JsonLogStreamFixture()
        : sink_(beast::severities::kAll, logStream_), j_(sink_)
    {
        static log::JsonStructuredJournal structuredJournal;
        beast::Journal::enableStructuredJournal(&structuredJournal);
    }

    ~JsonLogStreamFixture()
    {
        beast::Journal::disableStructuredJournal();
    }

    std::stringstream&
    stream()
    {
        return logStream_;
    }

    beast::Journal&
    journal()
    {
        return j_;
    }

private:
    MockSink sink_;
    std::stringstream logStream_;
    beast::Journal j_;
};

TEST_CASE_FIXTURE(JsonLogStreamFixture, "TestJsonLogFields")
{
    journal().debug() << std::boolalpha
                    << true
                    << std::noboolalpha
                    << " Test "
                    << std::boolalpha
                    << false;

    Json::Value logValue;
    Json::Reader reader;
    reader.parse(stream().str(), logValue);

    CHECK(logValue.isObject());
    CHECK(logValue.isMember("Function"));
    CHECK(logValue.isMember("File"));
    CHECK(logValue.isMember("Line"));
    CHECK(logValue.isMember("ThreadId"));
    CHECK(logValue.isMember("Params"));
    CHECK(logValue.isMember("Level"));
    CHECK(logValue.isMember("Message"));
    CHECK(logValue.isMember("Time"));

    CHECK(logValue["Function"].isString());
    CHECK(logValue["File"].isString());
    CHECK(logValue["Line"].isNumeric());
    CHECK(logValue["Params"].isNull());
    CHECK(logValue["Message"].isString());
    CHECK(logValue["Message"].asString() == "true Test false");
}

TEST_CASE_FIXTURE(JsonLogStreamFixture, "TestJsonLogLevels")
{
    {
        stream().str("");
        journal().trace() << "Test";
        Json::Value logValue;
        Json::Reader reader;
        reader.parse(stream().str(), logValue);

        CHECK(
            logValue["Level"].asString() ==
            beast::severities::to_string(beast::severities::kTrace));
    }

    {
        stream().str("");
        journal().debug() << "Test";
        Json::Value logValue;
        Json::Reader reader;
        reader.parse(stream().str(), logValue);

        CHECK(
            logValue["Level"].asString() ==
            beast::severities::to_string(beast::severities::kDebug));
    }

    {
        stream().str("");
        journal().info() << "Test";
        Json::Value logValue;
        Json::Reader reader;
        reader.parse(stream().str(), logValue);

        CHECK(
            logValue["Level"].asString() ==
            beast::severities::to_string(beast::severities::kInfo));
    }

    {
        stream().str("");
        journal().warn() << "Test";
        Json::Value logValue;
        Json::Reader reader;
        reader.parse(stream().str(), logValue);

        CHECK(
            logValue["Level"].asString() ==
            beast::severities::to_string(beast::severities::kWarning));
    }

    {
        stream().str("");
        journal().error() << "Test";
        Json::Value logValue;
        Json::Reader reader;
        reader.parse(stream().str(), logValue);

        CHECK(
            logValue["Level"].asString() ==
            beast::severities::to_string(beast::severities::kError));
    }

    {
        stream().str("");
        journal().fatal() << "Test";
        Json::Value logValue;
        Json::Reader reader;
        reader.parse(stream().str(), logValue);

        CHECK(
            logValue["Level"].asString() ==
            beast::severities::to_string(beast::severities::kFatal));
    }
}

TEST_CASE_FIXTURE(JsonLogStreamFixture, "TestJsonLogStream")
{
    journal().stream(beast::severities::kError) << "Test";

    Json::Value logValue;
    Json::Reader reader;
    reader.parse(stream().str(), logValue);

    CHECK(
        logValue["Level"].asString() ==
        beast::severities::to_string(beast::severities::kError));
}

TEST_CASE_FIXTURE(JsonLogStreamFixture, "TestJsonLogParams")
{
    journal().debug() << "Test: " << log::param("Field1", 1) << ", "
                      << log::param(
                             "Field2",
                             std::numeric_limits<std::uint64_t>::max());

    Json::Value logValue;
    Json::Reader reader;
    reader.parse(stream().str(), logValue);

    CHECK(logValue["Params"].isObject());
    CHECK(logValue["Params"]["Field1"].isNumeric());
    CHECK(logValue["Params"]["Field1"].asInt() == 1);
    // UInt64 doesn't fit in Json::Value so it should be converted to a string
    // NOTE: We should expect it to be an int64 after we make the json library
    // support in64 and uint64
    CHECK(logValue["Params"]["Field2"].isString());
    CHECK(logValue["Params"]["Field2"].asString() == "18446744073709551615");
    CHECK(logValue["Message"].isString());
    CHECK(logValue["Message"].asString() == "Test: 1, 18446744073709551615");
}

TEST_CASE_FIXTURE(JsonLogStreamFixture, "TestJsonLogFields")
{
    journal().debug() << "Test" << log::field("Field1", 1)
                      << log::field(
                             "Field2",
                             std::numeric_limits<std::uint64_t>::max());

    Json::Value logValue;
    Json::Reader reader;
    reader.parse(stream().str(), logValue);

    CHECK(logValue["Params"].isObject());
    CHECK(logValue["Params"]["Field1"].isNumeric());
    CHECK(logValue["Params"]["Field1"].asInt() == 1);
    // UInt64 doesn't fit in Json::Value so it should be converted to a string
    // NOTE: We should expect it to be an int64 after we make the json library
    // support in64 and uint64
    CHECK(logValue["Params"]["Field2"].isString());
    CHECK(logValue["Params"]["Field2"].asString() == "18446744073709551615");
    CHECK(logValue["Message"].isString());
    CHECK(logValue["Message"].asString() == "Test");
}

TEST_CASE_FIXTURE(JsonLogStreamFixture, "TestJournalAttributes")
{
    beast::Journal j{
        journal(), log::attributes({{"Field1", "Value1"}, {"Field2", 2}})};

    j.debug() << "Test";

    Json::Value logValue;
    Json::Reader reader;
    reader.parse(stream().str(), logValue);

    CHECK(logValue["Field1"].isString());
    CHECK(logValue["Field1"].asString() == "Value1");
    CHECK(logValue["Field2"].isNumeric());
    CHECK(logValue["Field2"].asInt() == 2);
}

TEST_CASE_FIXTURE(JsonLogStreamFixture, "TestJournalAttributesInheritable")
{
    beast::Journal j{
        journal(), log::attributes({{"Field1", "Value1"}, {"Field2", 2}})};
    beast::Journal j2{
        j, log::attributes({{"Field3", "Value3"}, {"Field2", 0}})};

    j2.debug() << "Test";

    Json::Value logValue;
    Json::Reader reader;
    reader.parse(stream().str(), logValue);

    CHECK(logValue["Field1"].isString());
    CHECK(logValue["Field1"].asString() == "Value1");
    CHECK(logValue["Field3"].isString());
    CHECK(logValue["Field3"].asString() == "Value3");
    // Field2 should be overwritten to 0
    CHECK(logValue["Field2"].isNumeric());
    CHECK(logValue["Field2"].asInt() == 0);
}

TEST_CASE_FIXTURE(
    JsonLogStreamFixture,
    "TestJournalAttributesInheritableAfterMoving")
{
    beast::Journal j{
        std::move(journal()),
        log::attributes({{"Field1", "Value1"}, {"Field2", 2}})};
    beast::Journal j2{
        std::move(j), log::attributes({{"Field3", "Value3"}, {"Field2", 0}})};

    j2.debug() << "Test";

    Json::Value logValue;
    Json::Reader reader;
    reader.parse(stream().str(), logValue);

    CHECK(logValue["Field1"].isString());
    CHECK(logValue["Field1"].asString() == "Value1");
    CHECK(logValue["Field3"].isString());
    CHECK(logValue["Field3"].asString() == "Value3");
    // Field2 should be overwritten to 0
    CHECK(logValue["Field2"].isNumeric());
    CHECK(logValue["Field2"].asInt() == 0);
}

TEST_CASE_FIXTURE(
    JsonLogStreamFixture,
    "TestJournalAttributesInheritableAfterCopyAssignment")
{
    beast::Journal j{
        std::move(journal()),
        log::attributes({{"Field1", "Value1"}, {"Field2", 2}})};

    beast::Journal j2{beast::Journal::getNullSink()};

    j2 = j;

    j2.debug() << "Test";

    Json::Value logValue;
    Json::Reader reader;
    reader.parse(stream().str(), logValue);

    CHECK(logValue["Field1"].isString());
    CHECK(logValue["Field1"].asString() == "Value1");
    CHECK(logValue["Field2"].isNumeric());
    CHECK(logValue["Field2"].asInt() == 2);
}

TEST_CASE_FIXTURE(
    JsonLogStreamFixture,
    "TestJournalAttributesInheritableAfterMoveAssignment")
{
    beast::Journal j{
        std::move(journal()),
        log::attributes({{"Field1", "Value1"}, {"Field2", 2}})};

    beast::Journal j2{beast::Journal::getNullSink()};

    j2 = std::move(j);

    j2.debug() << "Test";

    Json::Value logValue;
    Json::Reader reader;
    reader.parse(stream().str(), logValue);

    CHECK(logValue["Field1"].isString());
    CHECK(logValue["Field1"].asString() == "Value1");
    CHECK(logValue["Field2"].isNumeric());
    CHECK(logValue["Field2"].asInt() == 2);
}