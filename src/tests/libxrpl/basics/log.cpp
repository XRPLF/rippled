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
        write(beast::severities::Severity level, std::string const& text)
            override
        {
            logs_.logStream_ << text;
        }

        void
        writeAlways(beast::severities::Severity level, std::string const& text)
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

    virtual std::unique_ptr<beast::Journal::Sink>
    makeSink(
        std::string const& partition,
        beast::severities::Severity startingLevel)
    {
        return std::make_unique<Sink>(partition, startingLevel, *this);
    }
};

TEST_CASE("Enable Json Logs")
{
    static log::JsonStructuredJournal structuredJournal;

    std::stringstream logStream;

    MockLogs logs{logStream, beast::severities::kAll};

    logs.journal("Test").debug() << "Test";

    CHECK(logStream.str() == "Test");

    logStream.str("");

    beast::Journal::enableStructuredJournal(&structuredJournal);

    logs.journal("Test").debug() << "Test";

    Json::Reader reader;
    Json::Value jsonLog;
    bool result = reader.parse(logStream.str(), jsonLog);

    CHECK(result);

    CHECK(jsonLog.isObject());
    CHECK(jsonLog.isMember("Message"));
    CHECK(jsonLog["Message"].isString());
    CHECK(jsonLog["Message"].asString() == "Test");
}

TEST_CASE("Global attributes")
{
    static log::JsonStructuredJournal structuredJournal;

    std::stringstream logStream;

    MockLogs logs{logStream, beast::severities::kAll};

    beast::Journal::enableStructuredJournal(&structuredJournal);
    MockLogs::setGlobalAttributes(log::attributes({{"Field1", "Value1"}}));

    logs.journal("Test").debug() << "Test";

    Json::Reader reader;
    Json::Value jsonLog;
    bool result = reader.parse(logStream.str(), jsonLog);

    CHECK(result);

    CHECK(jsonLog.isObject());
    CHECK(jsonLog.isMember("Field1"));
    CHECK(jsonLog["Field1"].isString());
    CHECK(jsonLog["Field1"].asString() == "Value1");
}

TEST_CASE("Global attributes inheritable")
{
    static log::JsonStructuredJournal structuredJournal;

    std::stringstream logStream;

    MockLogs logs{logStream, beast::severities::kAll};

    beast::Journal::enableStructuredJournal(&structuredJournal);
    MockLogs::setGlobalAttributes(log::attributes({{"Field1", "Value1"}}));

    logs.journal(
            "Test",
            log::attributes({{"Field1", "Value3"}, {"Field2", "Value2"}}))
            .debug()
        << "Test";

    Json::Reader reader;
    Json::Value jsonLog;
    bool result = reader.parse(logStream.str(), jsonLog);

    CHECK(result);

    CHECK(jsonLog.isObject());
    CHECK(jsonLog.isMember("Field1"));
    CHECK(jsonLog["Field1"].isString());
    // Field1 should be overwritten to Value3
    CHECK(jsonLog["Field1"].asString() == "Value3");
    CHECK(jsonLog["Field2"].isString());
    CHECK(jsonLog["Field2"].asString() == "Value2");
}