// Copyright (c) 2020 Dev Null Productions

#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>

#include <xrpld/core/Config.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/config/Constants.h>
#include <xrpl/protocol/jss.h>

#include <memory>
#include <string>

namespace xrpl::test {

class ManifestRPC_test : public beast::unit_test::Suite
{
public:
    void
    testErrors()
    {
        testcase("Errors");

        using namespace jtx;
        Env env(*this);
        {
            // manifest with no public key
            auto const info = env.rpc("json", "manifest", "{ }");
            BEAST_EXPECT(info[jss::result][jss::error_message] == "Missing field 'public_key'.");
        }
        {
            // manifest with malformed public key
            auto const info = env.rpc(
                "json",
                "manifest",
                "{ \"public_key\": "
                "\"abcdef12345\"}");
            BEAST_EXPECT(info[jss::result][jss::error_message] == "Invalid parameters.");
        }
    }

    void
    testLookup()
    {
        testcase("Lookup");

        using namespace jtx;
        std::string const key = "n949f75evCHwgyP4fPVgaHqNHxUVN15PsJEZ3B3HnXPcPjcZAoy7";
        Env env{*this, envconfig([&key](std::unique_ptr<Config> cfg) {
                    cfg->section(Sections::kValidators).append(key);
                    return cfg;
                })};
        {
            auto const info = env.rpc(
                "json",
                "manifest",
                "{ \"public_key\": "
                "\"" +
                    key + "\"}");
            BEAST_EXPECT(info[jss::result][jss::requested] == key);
            BEAST_EXPECT(info[jss::result][jss::status] == "success");
        }
    }

    void
    run() override
    {
        testErrors();
        testLookup();
    }
};

BEAST_DEFINE_TESTSUITE(ManifestRPC, rpc, xrpl);
}  // namespace xrpl::test
