#include <test/jtx/Env.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>

#include <boost/container/static_vector.hpp>

#include <array>
#include <string>
#include <utility>

namespace xrpl {

class LogLevel_test : public beast::unit_test::Suite
{
    // The bad values shared by both parameters. Arrays and objects make
    // json::Value::asString() throw; the scalars are silently stringified by
    // it, so without a type check they reach the handler as "42", "1.500000",
    // "true" and "". Capacity leaves room for one parameter-specific value.
    static boost::container::static_vector<json::Value, 7>
    badValues()
    {
        json::Value objValue{json::ValueType::Object};
        objValue["nope"] = 0;
        json::Value arrValue{json::ValueType::Array};
        arrValue.append("warn");

        return {
            json::Value{42},    // int
            json::Value{1.5},   // real
            json::Value{true},  // bool
            json::Value{},      // null
            objValue,
            arrValue,
        };
    }

    // The names the bad scalars above would be stringified into if they were
    // not rejected. "" covers both the null and the empty-string partition.
    static constexpr std::array<char const*, 4> kJunkPartitions{
        "42",
        "1.500000",
        "true",
        "",
    };

    // A partition no other subsystem writes to, so changing its threshold has
    // no effect on how much this suite logs.
    static constexpr char const* kTestPartition = "LogLevelTestPartition";

    static json::Value
    levels(test::jtx::Env& env)
    {
        return env.client().invoke("log_level")[jss::result][jss::levels];
    }

    void
    testGetLevels()
    {
        testcase("Get levels");
        using namespace test::jtx;
        Env env{*this};

        // Omitting the severity lists the current thresholds rather than
        // setting one.
        auto const result = env.client().invoke("log_level")[jss::result];
        BEAST_EXPECT(!result.isMember(jss::error));
        BEAST_EXPECT(result[jss::status] == "success");
        BEAST_EXPECT(result[jss::levels].isObject());
        BEAST_EXPECT(result[jss::levels].isMember(jss::base));
    }

    void
    testValidSeverity()
    {
        testcase("Valid severity");
        using namespace test::jtx;
        Env env{*this};

        // Every alias Logs::fromString accepts, paired with the name
        // Logs::toString reports it back as. Matching is case insensitive.
        std::array<std::pair<char const*, char const*>, 12> const goodSeverities{{
            {"trace", "Trace"},
            {"debug", "Debug"},
            {"info", "Info"},
            {"information", "Info"},
            {"warn", "Warning"},
            {"warning", "Warning"},
            {"warnings", "Warning"},
            {"error", "Error"},
            {"errors", "Error"},
            {"fatal", "Fatal"},
            {"fatals", "Fatal"},
            {"FaTaL", "Fatal"},
        }};

        // Apply the aliases to a partition of this suite's own rather than to
        // the base threshold: nothing writes to it, so walking down to Trace
        // does not flood the test output the way lowering the base would.
        std::string const name{kTestPartition};

        for (auto const& [severity, reported] : goodSeverities)
        {
            json::Value params{json::ValueType::Object};
            params[jss::severity] = severity;
            params[jss::partition] = name;
            auto const result = env.client().invoke("log_level", params)[jss::result];
            BEAST_EXPECTS(!result.isMember(jss::error), severity);
            BEAST_EXPECTS(result[jss::status] == "success", severity);
            BEAST_EXPECTS(levels(env)[name] == reported, severity);
        }

        // With no partition given, the severity applies to the base threshold,
        // which the listing reports under "base".
        json::Value params{json::ValueType::Object};
        params[jss::severity] = "fatals";
        auto const result = env.client().invoke("log_level", params)[jss::result];
        BEAST_EXPECT(!result.isMember(jss::error));
        BEAST_EXPECT(levels(env)[jss::base] == "Fatal");
    }

    void
    testValidPartition()
    {
        testcase("Valid partition");
        using namespace test::jtx;
        Env env{*this};

        std::string const name{kTestPartition};

        // A partition is created on demand, so naming one that does not exist
        // yet is legitimate, and it shows up in the listing at the level given.
        json::Value params{json::ValueType::Object};
        params[jss::severity] = "fatal";
        params[jss::partition] = name;
        auto const result = env.client().invoke("log_level", params)[jss::result];
        BEAST_EXPECT(!result.isMember(jss::error));
        BEAST_EXPECT(result[jss::status] == "success");
        BEAST_EXPECT(levels(env)[name] == "Fatal");

        // "base" is matched case insensitively and sets the base threshold. It
        // must not create a partition of that name, so a mixed-case spelling is
        // what tells the two apart.
        params[jss::severity] = "error";
        params[jss::partition] = "BASE";
        auto const baseResult = env.client().invoke("log_level", params)[jss::result];
        BEAST_EXPECT(!baseResult.isMember(jss::error));
        BEAST_EXPECT(baseResult[jss::status] == "success");

        auto const current = levels(env);
        BEAST_EXPECT(current[jss::base] == "Error");
        BEAST_EXPECT(!current.isMember("BASE"));

        // Setting the base threshold is not additive: Logs::threshold assigns
        // it to every existing sink, so the partition set above is reset too.
        BEAST_EXPECT(current[name] == "Error");
    }

    void
    testBadSeverity()
    {
        testcase("Invalid severity");
        using namespace test::jtx;
        Env env{*this};

        auto bad = badValues();
        bad.push_back(json::Value{"err"});  // a string, but not a severity

        for (auto const& badSeverity : bad)
        {
            json::Value params{json::ValueType::Object};
            params[jss::severity] = badSeverity;
            auto const result = env.client().invoke("log_level", params)[jss::result];
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(result[jss::status] == "error");
        }

        // A rejected severity must not have moved the base threshold off the
        // level Env starts at.
        BEAST_EXPECT(levels(env)[jss::base] == "Error");
    }

    void
    testBadPartition()
    {
        testcase("Invalid partition");
        using namespace test::jtx;
        Env env{*this};

        auto bad = badValues();
        // Logs::get creates the partition, so an empty name is as damaging as a
        // coerced one: it wedges a nameless sink into the listing forever.
        bad.push_back(json::Value{""});

        for (auto const& badPartition : bad)
        {
            json::Value params{json::ValueType::Object};
            params[jss::severity] = "warn";
            params[jss::partition] = badPartition;
            auto const result = env.client().invoke("log_level", params)[jss::result];
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(result[jss::status] == "error");
        }

        // The point of the type check: none of the rejected values may have
        // been stringified into a partition name. Other partitions can appear
        // on their own as the server logs, so look for these names rather than
        // comparing the whole listing.
        auto const current = levels(env);
        for (auto const* junk : kJunkPartitions)
            BEAST_EXPECTS(!current.isMember(junk), std::string{"partition '"} + junk + "'");

        // A rejected partition must not have moved the base threshold either.
        BEAST_EXPECT(current[jss::base] == "Error");
    }

public:
    void
    run() override
    {
        testGetLevels();
        testValidSeverity();
        testValidPartition();
        testBadSeverity();
        testBadPartition();
    }
};

BEAST_DEFINE_TESTSUITE(LogLevel, rpc, xrpl);

}  // namespace xrpl
