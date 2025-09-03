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

#include <boost/json.hpp>

#include <doctest/doctest.h>

#include <iostream>
#include <numbers>

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
        write(beast::severities::Severity level, std::string&& text) override
        {
            logs_.write(level, partition_, text, false);
        }

        void
        writeAlways(beast::severities::Severity level, std::string&& text)
            override
        {
            logs_.write(level, partition_, text, false);
        }
    };

    std::string& logStream_;

public:
    MockLogs(std::string& logStream, beast::severities::Severity level)
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

    void
    write(
        beast::severities::Severity level,
        std::string const& partition,
        std::string const& text,
        bool console)
    {
        std::string s;
        format(s, text, level, partition);
        logStream_.append(s);
    }
};

TEST_CASE("Text logs")
{
    std::string logStream;

    MockLogs logs{logStream, beast::severities::kAll};

    logs.journal("Test").debug() << "Test";

    CHECK(logStream.find("Test") != std::string::npos);

    logStream.clear();

    logs.journal("Test").debug() << "\n";

    CHECK(logStream.find("\n") == std::string::npos);
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
    std::string logStream;

    MockLogs logs{logStream, beast::severities::kAll};

    logs.journal("Test ").debug() << "Test123";

    CHECK(logStream.find("Test123") != std::string::npos);

    logStream.clear();

    beast::Journal::enableStructuredJournal();

    logs.journal("Test").debug() << "\n";

    boost::system::error_code ec;
    auto doc = boost::json::parse(logStream, ec);
    CHECK(ec == boost::system::errc::success);

    CHECK(doc.is_object());
    CHECK(doc.as_object().contains("Message"));
    CHECK(doc.as_object()["Message"].is_string());
    CHECK(doc.as_object()["Message"].get_string() == "");
    beast::Journal::disableStructuredJournal();
}

TEST_CASE("Global attributes")
{
    std::string logStream;

    MockLogs logs{logStream, beast::severities::kAll};

    beast::Journal::enableStructuredJournal();
    beast::Journal::addGlobalAttributes(
        log::attributes(log::attr("Field1", "Value1")));

    logs.journal("Test").debug() << "Test";

    boost::system::error_code ec;
    auto jsonLog = boost::json::parse(logStream, ec);
    CHECK(ec == boost::system::errc::success);

    CHECK(jsonLog.is_object());
    CHECK(jsonLog.as_object().contains("GlobalParams"));
    CHECK(jsonLog.as_object()["GlobalParams"].is_object());
    CHECK(jsonLog.as_object()["GlobalParams"].as_object().contains("Field1"));
    CHECK(
        jsonLog.as_object()["GlobalParams"].as_object()["Field1"].is_string());
    CHECK(
        jsonLog.as_object()["GlobalParams"]
            .as_object()["Field1"]
            .get_string() == "Value1");
    beast::Journal::disableStructuredJournal();
}

TEST_CASE("Global attributes inheritable")
{
    std::string logStream;

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

    boost::system::error_code ec;
    auto jsonLog = boost::json::parse(logStream, ec);
    CHECK(ec == boost::system::errc::success);

    CHECK(jsonLog.is_object());
    CHECK(jsonLog.as_object()["GlobalParams"].as_object().contains("Field1"));
    CHECK(
        jsonLog.as_object()["GlobalParams"].as_object()["Field1"].is_string());
    CHECK(
        jsonLog.as_object()["GlobalParams"]
            .as_object()["Field1"]
            .get_string() == "Value1");
    CHECK(
        jsonLog.as_object()["JournalParams"]
            .as_object()["Field1"]
            .get_string() == "Value3");
    CHECK(
        jsonLog.as_object()["JournalParams"]
            .as_object()["Field2"]
            .get_string() == "Value2");
    beast::Journal::disableStructuredJournal();
}

TEST_CASE("Test JsonWriter")
{
    {
        std::string buffer;
        beast::detail::SimpleJsonWriter writer{buffer};

        writer.writeString("\n\r\t123\b\f123");
        CHECK(writer.finish() == "\"\\n\\r\\t123\\b\\f123\"");
    }

    {
        std::string buffer;
        beast::detail::SimpleJsonWriter writer{buffer};

        writer.writeString("\t");
        CHECK(writer.finish() == "\"\\t\"");
    }

    {
        std::string buffer;
        beast::detail::SimpleJsonWriter writer{buffer};

        writer.writeString(std::string_view{"\0", 1});
        CHECK(writer.finish() == "\"\\u0000\"");
    }

    {
        std::string buffer;
        beast::detail::SimpleJsonWriter writer{buffer};

        writer.writeString("\"\\");
        CHECK(writer.finish() == "\"\\\"\\\\\"");
    }

    {
        std::string buffer;
        beast::detail::SimpleJsonWriter writer{buffer};

        writer.startArray();
        writer.writeBool(true);
        writer.writeBool(false);
        writer.writeNull();
        writer.endArray();
        CHECK(writer.finish() == "[true,false,null]");
    }
}

namespace test_detail {
struct ToCharsStruct
{
};

std::to_chars_result
to_chars(char* first, char* last, ToCharsStruct)
{
    *first = '0';
    return std::to_chars_result{first + 1, std::errc{}};
}

struct StreamStruct
{
};

std::ostream&
operator<<(std::ostream& os, StreamStruct)
{
    os << "0";
    return os;
}

}  // namespace test_detail

TEST_CASE("Test setJsonValue")
{
    std::ostringstream stringBuf;
    std::string buffer;
    beast::detail::SimpleJsonWriter writer{buffer};
    writer.startObject();

    log::detail::setJsonValue<bool>(writer, "testBool", true, &stringBuf);
    log::detail::setJsonValue<std::int32_t>(writer, "testInt32", -1, &stringBuf);
    log::detail::setJsonValue<std::uint32_t>(
        writer, "testUInt32", 1, &stringBuf);
    log::detail::setJsonValue<std::int64_t>(writer, "testInt64", -1, &stringBuf);
    log::detail::setJsonValue<std::uint64_t>(
        writer, "testUInt64", 1, &stringBuf);
    log::detail::setJsonValue<double>(writer, "testDouble", 1.1, &stringBuf);
    log::detail::setJsonValue<char const*>(
        writer, "testCharStar", "Char*", &stringBuf);
    log::detail::setJsonValue<std::string>(
        writer, "testStdString", "StdString", &stringBuf);
    log::detail::setJsonValue<std::string_view>(
        writer, "testStdStringView", "StdStringView", &stringBuf);
    log::detail::setJsonValue<test_detail::ToCharsStruct>(
        writer, "testToChars", {}, &stringBuf);
    log::detail::setJsonValue<test_detail::StreamStruct>(
        writer, "testStream", {}, &stringBuf);
    writer.endObject();

    CHECK(writer.finish() == R"AAA({"testBool":true,"testInt32":-1,"testUInt32":1,"testInt64":-1,"testUInt64":1,"testDouble":1.1,"testCharStar":"Char*","testStdString":"StdString","testStdStringView":"StdStringView","testToChars":"0","testStream":"0"})AAA");
}

TEST_CASE("Test json logging not enabled")
{
    std::string logStream;

    MockLogs logs{logStream, beast::severities::kAll};

    beast::Journal::disableStructuredJournal();
    beast::Journal::addGlobalAttributes(
        log::attributes(log::attr("Field1", "Value1")));

    logs.journal("Test123").debug()
        << "Test " << log::param(" Field1", "Value1")
        << log::field("Field2", "Value2");

    CHECK(logStream.find("Test Value1") != std::string::npos);
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
    writeAlways(beast::severities::Severity level, std::string&& text) override
    {
        strm_ << text;
    }
};

class JsonLogStreamFixture
{
public:
    JsonLogStreamFixture()
        : sink_(beast::severities::kAll, logStream_)
        , j_(sink_, "Test", log::attributes(log::attr("Field1", "Value1")))
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

TEST_CASE_FIXTURE(JsonLogStreamFixture, "Test json log fields")
{
    beast::Journal::addGlobalAttributes(
        log::attributes(log::attr("Field2", "Value2")));
    journal().debug() << std::boolalpha << true << std::noboolalpha << " Test "
                      << std::boolalpha << false
                      << log::field("Field3", "Value3");

    boost::system::error_code ec;
    auto logValue = boost::json::parse(stream().str(), ec);
    CHECK(ec == boost::system::errc::success);

    CHECK(logValue.is_object());
    CHECK(logValue.as_object().contains("GlobalParams"));
    CHECK(logValue.as_object().contains("JournalParams"));
    CHECK(logValue.as_object().contains("MessageParams"));
    CHECK(logValue.as_object().contains("Message"));

    CHECK(logValue.as_object()["GlobalParams"].is_object());
    CHECK(logValue.as_object()["JournalParams"].is_object());
    CHECK(logValue.as_object()["MessageParams"].is_object());
    CHECK(logValue.as_object()["Message"].is_string());

    CHECK(
        logValue.as_object()["MessageParams"].as_object().contains("Function"));
    CHECK(logValue.as_object()["MessageParams"].as_object().contains("File"));
    CHECK(logValue.as_object()["MessageParams"].as_object().contains("Line"));
    CHECK(
        logValue.as_object()["MessageParams"].as_object().contains("ThreadId"));
    CHECK(logValue.as_object()["MessageParams"].as_object().contains("Level"));
    CHECK(logValue.as_object()["MessageParams"].as_object().contains("Time"));

    CHECK(logValue.as_object()["MessageParams"]
              .as_object()["Function"]
              .is_string());
    CHECK(
        logValue.as_object()["MessageParams"].as_object()["File"].is_string());
    CHECK(
        logValue.as_object()["MessageParams"].as_object()["Line"].is_number());

    CHECK(
        logValue.as_object()["Message"].get_string() ==
        std::string{"true Test false"});
}

TEST_CASE_FIXTURE(JsonLogStreamFixture, "Test json log levels")
{
    {
        stream().str("");
        journal().trace() << "Test";

        boost::system::error_code ec;
        auto logValue = boost::json::parse(stream().str(), ec);
        CHECK(ec == boost::system::errc::success);

        CHECK(
            logValue.as_object()["MessageParams"]
                .as_object()["Level"]
                .get_string() ==
            beast::severities::to_string(beast::severities::kTrace));
    }

    {
        stream().str("");
        journal().debug() << "Test";

        boost::system::error_code ec;
        auto logValue = boost::json::parse(stream().str(), ec);
        CHECK(ec == boost::system::errc::success);

        CHECK(
            logValue.as_object()["MessageParams"]
                .as_object()["Level"]
                .get_string() ==
            beast::severities::to_string(beast::severities::kDebug));
    }

    {
        stream().str("");
        journal().info() << "Test";

        boost::system::error_code ec;
        auto logValue = boost::json::parse(stream().str(), ec);
        CHECK(ec == boost::system::errc::success);

        CHECK(
            logValue.as_object()["MessageParams"]
                .as_object()["Level"]
                .get_string() ==
            beast::severities::to_string(beast::severities::kInfo));
    }

    {
        stream().str("");
        journal().warn() << "Test";

        boost::system::error_code ec;
        auto logValue = boost::json::parse(stream().str(), ec);
        CHECK(ec == boost::system::errc::success);

        CHECK(
            logValue.as_object()["MessageParams"]
                .as_object()["Level"]
                .get_string() ==
            beast::severities::to_string(beast::severities::kWarning));
    }

    {
        stream().str("");
        journal().error() << "Test";

        boost::system::error_code ec;
        auto logValue = boost::json::parse(stream().str(), ec);
        CHECK(ec == boost::system::errc::success);

        CHECK(
            logValue.as_object()["MessageParams"]
                .as_object()["Level"]
                .get_string() ==
            beast::severities::to_string(beast::severities::kError));
    }

    {
        stream().str("");
        journal().fatal() << "Test";

        boost::system::error_code ec;
        auto logValue = boost::json::parse(stream().str(), ec);
        CHECK(ec == boost::system::errc::success);

        CHECK(
            logValue.as_object()["MessageParams"]
                .as_object()["Level"]
                .get_string() ==
            beast::severities::to_string(beast::severities::kFatal));
    }
}

TEST_CASE_FIXTURE(JsonLogStreamFixture, "Test json log stream")
{
    journal().stream(beast::severities::kError) << "Test";

    boost::system::error_code ec;
    auto logValue = boost::json::parse(stream().str(), ec);
    CHECK(ec == boost::system::errc::success);

    CHECK(
        logValue.as_object()["MessageParams"]
            .as_object()["Level"]
            .get_string() ==
        beast::severities::to_string(beast::severities::kError));
}

TEST_CASE_FIXTURE(JsonLogStreamFixture, "Test json log params")
{
    journal().debug() << "Test: " << log::param("Field1", 1) << ", "
                      << log::param(
                             "Field2",
                             std::numeric_limits<std::uint64_t>::max())
                      << ", " << log::param("Field3", std::numbers::pi);

    boost::system::error_code ec;
    auto logValue = boost::json::parse(stream().str(), ec);
    CHECK(ec == boost::system::errc::success);

    CHECK(logValue.as_object()["MessageParams"].is_object());
    CHECK(logValue.as_object()["MessageParams"]
              .as_object()["Field1"]
              .is_number());
    CHECK(
        logValue.as_object()["MessageParams"]
            .as_object()["Field1"]
            .get_int64() == 1);
    CHECK(logValue.as_object()["MessageParams"]
              .as_object()["Field2"]
              .is_number());
    CHECK(
        logValue.as_object()["MessageParams"]
            .as_object()["Field2"]
            .get_uint64() == std::numeric_limits<std::uint64_t>::max());
    auto field3Val = logValue.as_object()["MessageParams"]
                         .as_object()["Field3"]
                         .get_double();
    auto difference = std::abs(field3Val - std::numbers::pi);
    CHECK(difference < 1e-4);
    CHECK(logValue.as_object()["Message"].is_string());
    CHECK(
        logValue.as_object()["Message"].get_string() ==
        std::string{"Test: 1, 18446744073709551615, 3.141592653589793"});
}

TEST_CASE_FIXTURE(JsonLogStreamFixture, "Test json log fields")
{
    journal().debug() << "Test" << log::field("Field1", 1)
                      << log::field(
                             "Field2",
                             std::numeric_limits<std::uint64_t>::max());

    boost::system::error_code ec;
    auto logValue = boost::json::parse(stream().str(), ec);
    CHECK(ec == boost::system::errc::success);

    CHECK(logValue.as_object()["MessageParams"].is_object());
    CHECK(logValue.as_object()["MessageParams"]
              .as_object()["Field1"]
              .is_number());
    CHECK(
        logValue.as_object()["MessageParams"]
            .as_object()["Field1"]
            .get_int64() == 1);
    // UInt64 doesn't fit in Json::Value so it should be converted to a string
    // NOTE: We should expect it to be an int64 after we make the json library
    // support in64 and uint64
    CHECK(logValue.as_object()["MessageParams"]
              .as_object()["Field2"]
              .is_number());
    CHECK(
        logValue.as_object()["MessageParams"]
            .as_object()["Field2"]
            .get_uint64() == std::numeric_limits<std::uint64_t>::max());
    CHECK(logValue.as_object()["Message"].is_string());
    CHECK(logValue.as_object()["Message"].get_string() == "Test");
}

TEST_CASE_FIXTURE(JsonLogStreamFixture, "Test journal attributes")
{
    beast::Journal j{
        journal(),
        log::attributes(log::attr("Field1", "Value1"), log::attr("Field2", 2))};

    j.debug() << "Test";

    boost::system::error_code ec;
    auto logValue = boost::json::parse(stream().str(), ec);
    CHECK(ec == boost::system::errc::success);

    CHECK(logValue.as_object()["JournalParams"]
              .as_object()["Field1"]
              .is_string());
    CHECK(
        logValue.as_object()["JournalParams"]
            .as_object()["Field1"]
            .get_string() == std::string{"Value1"});
    CHECK(logValue.as_object()["JournalParams"]
              .as_object()["Field2"]
              .is_number());
    CHECK(
        logValue.as_object()["JournalParams"]
            .as_object()["Field2"]
            .get_int64() == 2);
}

TEST_CASE_FIXTURE(JsonLogStreamFixture, "Test journal attributes inheritable")
{
    beast::Journal j{
        journal(),
        log::attributes(log::attr("Field1", "Value1"), log::attr("Field2", 2))};
    beast::Journal j2{j, log::attributes(log::attr("Field3", "Value3"))};

    j2.debug() << "Test";

    boost::system::error_code ec;
    auto logValue = boost::json::parse(stream().str(), ec);
    CHECK(ec == boost::system::errc::success);

    CHECK(logValue.as_object()["JournalParams"]
              .as_object()["Field1"]
              .is_string());
    CHECK(
        logValue.as_object()["JournalParams"]
            .as_object()["Field1"]
            .get_string() == std::string{"Value1"});
    CHECK(logValue.as_object()["JournalParams"]
              .as_object()["Field3"]
              .is_string());
    CHECK(
        logValue.as_object()["JournalParams"]
            .as_object()["Field3"]
            .get_string() == std::string{"Value3"});
    CHECK(logValue.as_object()["JournalParams"]
              .as_object()["Field2"]
              .is_number());
    CHECK(
        logValue.as_object()["JournalParams"]
            .as_object()["Field2"]
            .get_int64() == 2);
}

TEST_CASE_FIXTURE(JsonLogStreamFixture, "Test copying journal")
{
    {
        beast::Journal j{
            journal(),
            log::attributes(
                log::attr("Field1", "Value1"), log::attr("Field2", 2))};
        beast::Journal j2{j};

        j2.debug() << "Test";

        boost::system::error_code ec;
        auto logValue = boost::json::parse(stream().str(), ec);
        CHECK(ec == boost::system::errc::success);

        CHECK(logValue.as_object()["JournalParams"]
                  .as_object()["Field1"]
                  .is_string());
        CHECK(
            logValue.as_object()["JournalParams"]
                .as_object()["Field1"]
                .get_string() == std::string{"Value1"});
        CHECK(logValue.as_object()["JournalParams"]
                  .as_object()["Field2"]
                  .is_number());
        CHECK(
            logValue.as_object()["JournalParams"]
                .as_object()["Field2"]
                .get_int64() == 2);
    }
    {
        stream().str("");
        beast::Journal j{journal().sink()};
        beast::Journal j2{
            j,
            log::attributes(
                log::attr("Field1", "Value1"), log::attr("Field2", 2))};

        j2.debug() << "Test";

        boost::system::error_code ec;
        auto logValue = boost::json::parse(stream().str(), ec);
        CHECK(ec == boost::system::errc::success);

        CHECK(logValue.as_object()["JournalParams"]
                  .as_object()["Field1"]
                  .is_string());
        CHECK(
            logValue.as_object()["JournalParams"]
                .as_object()["Field1"]
                .get_string() == std::string{"Value1"});
        CHECK(logValue.as_object()["JournalParams"]
                  .as_object()["Field2"]
                  .is_number());
        CHECK(
            logValue.as_object()["JournalParams"]
                .as_object()["Field2"]
                .get_int64() == 2);
    }
}

TEST_CASE_FIXTURE(
    JsonLogStreamFixture,
    "Test journal attributes inheritable after moving")
{
    beast::Journal j{
        journal(),
        log::attributes(log::attr("Field1", "Value1"), log::attr("Field2", 2))};
    beast::Journal j2{j, log::attributes(log::attr("Field3", "Value3"))};

    j2.debug() << "Test";

    boost::system::error_code ec;
    auto logValue = boost::json::parse(stream().str(), ec);
    CHECK(ec == boost::system::errc::success);

    CHECK(logValue.as_object()["JournalParams"]
              .as_object()["Field1"]
              .is_string());
    CHECK(
        logValue.as_object()["JournalParams"]
            .as_object()["Field1"]
            .get_string() == std::string{"Value1"});
    CHECK(logValue.as_object()["JournalParams"]
              .as_object()["Field3"]
              .is_string());
    CHECK(
        logValue.as_object()["JournalParams"]
            .as_object()["Field3"]
            .get_string() == std::string{"Value3"});
    // Field2 should be overwritten to 0
    CHECK(logValue.as_object()["JournalParams"]
              .as_object()["Field2"]
              .is_number());
    CHECK(
        logValue.as_object()["JournalParams"]
            .as_object()["Field2"]
            .get_int64() == 2);
}

TEST_CASE_FIXTURE(
    JsonLogStreamFixture,
    "Test journal attributes inheritable after copy assignment")
{
    beast::Journal j{
        std::move(journal()),
        log::attributes(log::attr("Field1", "Value1"), log::attr("Field2", 2))};

    beast::Journal j2{beast::Journal::getNullSink()};

    j2 = j;

    j2.debug() << "Test";

    boost::system::error_code ec;
    auto logValue = boost::json::parse(stream().str(), ec);
    CHECK(ec == boost::system::errc::success);

    CHECK(logValue.as_object()["JournalParams"]
              .as_object()["Field1"]
              .is_string());
    CHECK(
        logValue.as_object()["JournalParams"]
            .as_object()["Field1"]
            .get_string() == std::string{"Value1"});
    CHECK(logValue.as_object()["JournalParams"]
              .as_object()["Field2"]
              .is_number());
    CHECK(
        logValue.as_object()["JournalParams"]
            .as_object()["Field2"]
            .get_int64() == 2);
}

TEST_CASE_FIXTURE(
    JsonLogStreamFixture,
    "Test journal attributes inheritable after move assignment")
{
    beast::Journal j{
        journal(),
        log::attributes(log::attr("Field1", "Value1"), log::attr("Field2", 2))};

    beast::Journal j2{beast::Journal::getNullSink()};

    j2 = std::move(j);

    j2.debug() << "Test";

    boost::system::error_code ec;
    auto logValue = boost::json::parse(stream().str(), ec);
    CHECK(ec == boost::system::errc::success);

    CHECK(logValue.as_object()["JournalParams"]
              .as_object()["Field1"]
              .is_string());
    CHECK(
        logValue.as_object()["JournalParams"]
            .as_object()["Field1"]
            .get_string() == std::string{"Value1"});
    CHECK(logValue.as_object()["JournalParams"]
              .as_object()["Field2"]
              .is_number());
    CHECK(
        logValue.as_object()["JournalParams"]
            .as_object()["Field2"]
            .get_int64() == 2);
}
