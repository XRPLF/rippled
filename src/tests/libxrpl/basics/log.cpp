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

#include <doctest/doctest.h>
#include <rapidjson/document.h>

using namespace ripple;

class MockLogs : public Logs
{
private:
    class Sink : public beast::Journal::Sink
    {
    private:
        MockLogs& logs_;
        std::string partition_;

    public:
        Sink(
            std::string const& partition,
            beast::severities::Severity thresh,
            MockLogs& logs)
            : beast::Journal::Sink(thresh, false)
            , logs_(logs)
            , partition_(partition)
        {
        }

        Sink(Sink const&) = delete;
        Sink&
        operator=(Sink const&) = delete;

        void
        write(beast::severities::Severity level, std::string&& text)
            override
        {
            logs_.logStream_ << text;
        }

        void
        writeAlways(beast::severities::Severity level, std::string&& text)
            override
        {
            logs_.logStream_ << text;
        }
    };

    std::stringstream& logStream_;

public:
    MockLogs(std::stringstream& logStream, beast::severities::Severity level)
        : Logs(level), logStream_(logStream)
    {
    }

    std::unique_ptr<beast::Journal::Sink>
    makeSink(
        std::string const& partition,
        beast::severities::Severity startingLevel) override
    {
        return std::make_unique<Sink>(partition, startingLevel, *this);
    }
};

TEST_CASE("Text logs")
{
    std::stringstream logStream;

    MockLogs logs{logStream, beast::severities::kAll};

    logs.journal("Test").debug() << "Test";

    CHECK(logStream.str().find("Test") != std::string::npos);

    logStream.str("");

    logs.journal("Test").debug() << "\n";

    CHECK(logStream.str().find("\n") == std::string::npos);
}

TEST_CASE("Test format output")
{
    std::string output;
    Logs::format(output, "Message", beast::severities::kDebug, "Test");
    CHECK(output.find("Message") != std::string::npos);
    CHECK(output != "Message");
}

TEST_CASE("Test format output when structured logs are enabled")
{
    beast::Journal::enableStructuredJournal();

    std::string output;
    Logs::format(output, "Message", beast::severities::kDebug, "Test");

    CHECK(output == "Message");

    beast::Journal::disableStructuredJournal();
}

TEST_CASE("Enable json logs")
{
    std::stringstream logStream;

    MockLogs logs{logStream, beast::severities::kAll};

    logs.journal("Test").debug() << "Test";

    CHECK(logStream.str() == "Test");

    logStream.str("");

    beast::Journal::enableStructuredJournal();

    logs.journal("Test").debug() << "\n";

    rapidjson::Document doc;
    doc.Parse(logStream.str().c_str());

    CHECK(doc.GetParseError() == rapidjson::ParseErrorCode::kParseErrorNone);

    CHECK(doc.IsObject());
    CHECK(doc.HasMember("Message"));
    CHECK(doc["Message"].IsString());
    CHECK(doc["Message"].GetString() == std::string{""});
    beast::Journal::disableStructuredJournal();
}

TEST_CASE("Global attributes")
{
    std::stringstream logStream;

    MockLogs logs{logStream, beast::severities::kAll};

    beast::Journal::enableStructuredJournal();
    beast::Journal::addGlobalAttributes(
        log::attributes(log::attr("Field1", "Value1")));

    logs.journal("Test").debug() << "Test";

    rapidjson::Document jsonLog;
    jsonLog.Parse(logStream.str().c_str());

    CHECK(
        jsonLog.GetParseError() == rapidjson::ParseErrorCode::kParseErrorNone);

    CHECK(jsonLog.IsObject());
    CHECK(jsonLog.HasMember("Field1"));
    CHECK(jsonLog["Field1"].IsString());
    CHECK(jsonLog["Field1"].GetString() == std::string{"Value1"});
    beast::Journal::disableStructuredJournal();
}

TEST_CASE("Global attributes inheritable")
{
    std::stringstream logStream;

    MockLogs logs{logStream, beast::severities::kAll};

    beast::Journal::enableStructuredJournal();
    beast::Journal::addGlobalAttributes(
        log::attributes(log::attr("Field1", "Value1")));

    logs.journal(
            "Test",
            log::attributes(
                log::attr("Field1", "Value3"), log::attr("Field2", "Value2")))
            .debug()
        << "Test";

    rapidjson::Document jsonLog;
    jsonLog.Parse(logStream.str().c_str());

    CHECK(
        jsonLog.GetParseError() == rapidjson::ParseErrorCode::kParseErrorNone);

    CHECK(jsonLog.IsObject());
    CHECK(jsonLog.HasMember("Field1"));
    CHECK(jsonLog["Field1"].IsString());
    // Field1 should be overwritten to Value3
    CHECK(jsonLog["Field1"].GetString() == std::string{"Value3"});
    CHECK(jsonLog["Field2"].IsString());
    CHECK(jsonLog["Field2"].GetString() == std::string{"Value2"});
    beast::Journal::disableStructuredJournal();
}

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
    write(beast::severities::Severity level, std::string&& text) override
    {
        strm_ << text;
    }

    void
    writeAlways(beast::severities::Severity level, std::string&& text)
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
        beast::Journal::enableStructuredJournal();
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
    journal().debug() << std::boolalpha << true << std::noboolalpha << " Test "
                      << std::boolalpha << false;

    rapidjson::Document logValue;
    logValue.Parse(stream().str().c_str());

    CHECK(
        logValue.GetParseError() == rapidjson::ParseErrorCode::kParseErrorNone);

    CHECK(logValue.IsObject());
    CHECK(logValue.HasMember("Function"));
    CHECK(logValue.HasMember("File"));
    CHECK(logValue.HasMember("Line"));
    CHECK(logValue.HasMember("ThreadId"));
    CHECK(logValue.HasMember("Params"));
    CHECK(logValue.HasMember("Level"));
    CHECK(logValue.HasMember("Message"));
    CHECK(logValue.HasMember("Time"));

    CHECK(logValue["Function"].IsString());
    CHECK(logValue["File"].IsString());
    CHECK(logValue["Line"].IsNumber());
    CHECK(logValue["Params"].IsObject());
    CHECK(logValue["Message"].IsString());
    CHECK(logValue["Message"].GetString() == std::string{"true Test false"});
}

TEST_CASE_FIXTURE(JsonLogStreamFixture, "TestJsonLogLevels")
{
    {
        stream().str("");
        journal().trace() << "Test";

        rapidjson::Document logValue;
        logValue.Parse(stream().str().c_str());

        CHECK(
            logValue.GetParseError() ==
            rapidjson::ParseErrorCode::kParseErrorNone);

        CHECK(
            logValue["Level"].GetString() ==
            beast::severities::to_string(beast::severities::kTrace));
    }

    {
        stream().str("");
        journal().debug() << "Test";

        rapidjson::Document logValue;
        logValue.Parse(stream().str().c_str());

        CHECK(
            logValue.GetParseError() ==
            rapidjson::ParseErrorCode::kParseErrorNone);

        CHECK(
            logValue["Level"].GetString() ==
            beast::severities::to_string(beast::severities::kDebug));
    }

    {
        stream().str("");
        journal().info() << "Test";

        rapidjson::Document logValue;
        logValue.Parse(stream().str().c_str());

        CHECK(
            logValue.GetParseError() ==
            rapidjson::ParseErrorCode::kParseErrorNone);

        CHECK(
            logValue["Level"].GetString() ==
            beast::severities::to_string(beast::severities::kInfo));
    }

    {
        stream().str("");
        journal().warn() << "Test";

        rapidjson::Document logValue;
        logValue.Parse(stream().str().c_str());

        CHECK(
            logValue.GetParseError() ==
            rapidjson::ParseErrorCode::kParseErrorNone);

        CHECK(
            logValue["Level"].GetString() ==
            beast::severities::to_string(beast::severities::kWarning));
    }

    {
        stream().str("");
        journal().error() << "Test";

        rapidjson::Document logValue;
        logValue.Parse(stream().str().c_str());

        CHECK(
            logValue.GetParseError() ==
            rapidjson::ParseErrorCode::kParseErrorNone);

        CHECK(
            logValue["Level"].GetString() ==
            beast::severities::to_string(beast::severities::kError));
    }

    {
        stream().str("");
        journal().fatal() << "Test";

        rapidjson::Document logValue;
        logValue.Parse(stream().str().c_str());

        CHECK(
            logValue.GetParseError() ==
            rapidjson::ParseErrorCode::kParseErrorNone);

        CHECK(
            logValue["Level"].GetString() ==
            beast::severities::to_string(beast::severities::kFatal));
    }
}

TEST_CASE_FIXTURE(JsonLogStreamFixture, "TestJsonLogStream")
{
    journal().stream(beast::severities::kError) << "Test";

    rapidjson::Document logValue;
    logValue.Parse(stream().str().c_str());

    CHECK(
        logValue.GetParseError() == rapidjson::ParseErrorCode::kParseErrorNone);

    CHECK(
        logValue["Level"].GetString() ==
        beast::severities::to_string(beast::severities::kError));
}

TEST_CASE_FIXTURE(JsonLogStreamFixture, "TestJsonLogParams")
{
    journal().debug() << "Test: " << log::param("Field1", 1) << ", "
                      << log::param(
                             "Field2",
                             std::numeric_limits<std::uint64_t>::max());

    rapidjson::Document logValue;
    logValue.Parse(stream().str().c_str());

    CHECK(
        logValue.GetParseError() == rapidjson::ParseErrorCode::kParseErrorNone);

    CHECK(logValue["Params"].IsObject());
    CHECK(logValue["Params"]["Field1"].IsNumber());
    CHECK(logValue["Params"]["Field1"].GetInt() == 1);
    // UInt64 doesn't fit in Json::Value so it should be converted to a string
    // NOTE: We should expect it to be an int64 after we make the json library
    // support in64 and uint64
    CHECK(logValue["Params"]["Field2"].IsNumber());
    CHECK(
        logValue["Params"]["Field2"].GetUint64() ==
        std::numeric_limits<std::uint64_t>::max());
    CHECK(logValue["Message"].IsString());
    CHECK(
        logValue["Message"].GetString() ==
        std::string{"Test: 1, 18446744073709551615"});
}

TEST_CASE_FIXTURE(JsonLogStreamFixture, "TestJsonLogFields")
{
    journal().debug() << "Test" << log::field("Field1", 1)
                      << log::field(
                             "Field2",
                             std::numeric_limits<std::uint64_t>::max());

    rapidjson::Document logValue;
    logValue.Parse(stream().str().c_str());

    CHECK(
        logValue.GetParseError() == rapidjson::ParseErrorCode::kParseErrorNone);

    CHECK(logValue["Params"].IsObject());
    CHECK(logValue["Params"]["Field1"].IsNumber());
    CHECK(logValue["Params"]["Field1"].GetInt() == 1);
    // UInt64 doesn't fit in Json::Value so it should be converted to a string
    // NOTE: We should expect it to be an int64 after we make the json library
    // support in64 and uint64
    CHECK(logValue["Params"]["Field2"].IsNumber());
    CHECK(
        logValue["Params"]["Field2"].GetUint64() ==
        std::numeric_limits<std::uint64_t>::max());
    CHECK(logValue["Message"].IsString());
    CHECK(logValue["Message"].GetString() == std::string{"Test"});
}

TEST_CASE_FIXTURE(JsonLogStreamFixture, "TestJournalAttributes")
{
    beast::Journal j{
        journal(),
        log::attributes(log::attr("Field1", "Value1"), log::attr("Field2", 2))};

    j.debug() << "Test";

    rapidjson::Document logValue;
    logValue.Parse(stream().str().c_str());

    CHECK(
        logValue.GetParseError() == rapidjson::ParseErrorCode::kParseErrorNone);

    CHECK(logValue["Field1"].IsString());
    CHECK(logValue["Field1"].GetString() == std::string{"Value1"});
    CHECK(logValue["Field2"].IsNumber());
    CHECK(logValue["Field2"].GetInt() == 2);
}

TEST_CASE_FIXTURE(JsonLogStreamFixture, "TestJournalAttributesInheritable")
{
    beast::Journal j{
        journal(),
        log::attributes(log::attr("Field1", "Value1"), log::attr("Field2", 2))};
    beast::Journal j2{
        j,
        log::attributes(log::attr("Field3", "Value3"), log::attr("Field2", 0))};

    j2.debug() << "Test";

    rapidjson::Document logValue;
    logValue.Parse(stream().str().c_str());

    CHECK(
        logValue.GetParseError() == rapidjson::ParseErrorCode::kParseErrorNone);

    CHECK(logValue["Field1"].IsString());
    CHECK(logValue["Field1"].GetString() == std::string{"Value1"});
    CHECK(logValue["Field3"].IsString());
    CHECK(logValue["Field3"].GetString() == std::string{"Value3"});
    // Field2 should be overwritten to 0
    CHECK(logValue["Field2"].IsNumber());
    CHECK(logValue["Field2"].GetInt() == 0);
}

TEST_CASE_FIXTURE(
    JsonLogStreamFixture,
    "TestJournalAttributesInheritableAfterMoving")
{
    beast::Journal j{
        journal(),
        log::attributes(log::attr("Field1", "Value1"), log::attr("Field2", 2))};
    beast::Journal j2{
        j,
        log::attributes(log::attr("Field3", "Value3"), log::attr("Field2", 0))};

    j2.debug() << "Test";

    rapidjson::Document logValue;
    logValue.Parse(stream().str().c_str());

    CHECK(
        logValue.GetParseError() == rapidjson::ParseErrorCode::kParseErrorNone);

    CHECK(logValue["Field1"].IsString());
    CHECK(logValue["Field1"].GetString() == std::string{"Value1"});
    CHECK(logValue["Field3"].IsString());
    CHECK(logValue["Field3"].GetString() == std::string{"Value3"});
    // Field2 should be overwritten to 0
    CHECK(logValue["Field2"].IsNumber());
    CHECK(logValue["Field2"].GetInt() == 0);
}

TEST_CASE_FIXTURE(
    JsonLogStreamFixture,
    "TestJournalAttributesInheritableAfterCopyAssignment")
{
    beast::Journal j{
        std::move(journal()),
        log::attributes(log::attr("Field1", "Value1"), log::attr("Field2", 2))};

    beast::Journal j2{beast::Journal::getNullSink()};

    j2 = j;

    j2.debug() << "Test";

    rapidjson::Document logValue;
    logValue.Parse(stream().str().c_str());

    CHECK(
        logValue.GetParseError() == rapidjson::ParseErrorCode::kParseErrorNone);

    CHECK(logValue["Field1"].IsString());
    CHECK(logValue["Field1"].GetString() == std::string{"Value1"});
    CHECK(logValue["Field2"].IsNumber());
    CHECK(logValue["Field2"].GetInt() == 2);
}

TEST_CASE_FIXTURE(
    JsonLogStreamFixture,
    "TestJournalAttributesInheritableAfterMoveAssignment")
{
    beast::Journal j{
        journal(),
        log::attributes(log::attr("Field1", "Value1"), log::attr("Field2", 2))};

    beast::Journal j2{beast::Journal::getNullSink()};

    j2 = std::move(j);

    j2.debug() << "Test";

    rapidjson::Document logValue;
    logValue.Parse(stream().str().c_str());

    CHECK(
        logValue.GetParseError() == rapidjson::ParseErrorCode::kParseErrorNone);

    CHECK(logValue["Field1"].IsString());
    CHECK(logValue["Field1"].GetString() == std::string{"Value1"});
    CHECK(logValue["Field2"].IsNumber());
    CHECK(logValue["Field2"].GetInt() == 2);
}