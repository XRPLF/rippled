// Copyright (c) 2020 Dev Null Productions

#include <test/jtx.h>

#include <xrpld/core/ConfigSections.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/jss.h>

#include <string>

namespace ripple {
namespace test {

class ManifestRPC_test : public beast::unit_test::suite
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
            BEAST_EXPECT(
                info[jss::result][jss::error_message] ==
                "Missing field 'public_key'.");
        }
        {
            // manifest with manlformed public key
            auto const info = env.rpc(
                "json",
                "manifest",
                "{ \"public_key\": "
                "\"abcdef12345\"}");
            BEAST_EXPECT(
                info[jss::result][jss::error_message] == "Invalid parameters.");
        }
    }

    void
    testLookup()
    {
        testcase("Lookup");

        using namespace jtx;
        std::string const key =
            "n949f75evCHwgyP4fPVgaHqNHxUVN15PsJEZ3B3HnXPcPjcZAoy7";
        Env env{*this, envconfig([&key](std::unique_ptr<Config> cfg) {
                    cfg->section(SECTION_VALIDATORS).append(key);
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

BEAST_DEFINE_TESTSUITE(ManifestRPC, rpc, ripple);
}  // namespace test
}  // namespace ripple
