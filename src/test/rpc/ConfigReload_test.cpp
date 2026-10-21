#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>
#include <test/unit_test/FileDirGuard.h>

#include <xrpld/app/misc/TxQ.h>
#include <xrpld/core/Config.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace xrpl {

class ConfigReload_test : public beast::unit_test::Suite
{
    static constexpr auto kIpsMessage =
        "Section 'ips' in 'sections' cannot be reloaded without a restart. Sections that can be "
        "reloaded: transaction_queue.";

    static std::string
    configText(std::string const& txqLines)
    {
        return "[transaction_queue]\n" + txqLines +
            "ledgers_in_queue = 2\n"
            "minimum_queue_size = 2\n"
            "\n"
            "[ips]\n"
            "r.ripple.com 51235\n";
    }

    static void
    writeFile(std::filesystem::path const& path, std::string const& contents)
    {
        std::ofstream o(path.string(), std::ios::trunc);
        o << contents;
    }

    static std::unique_ptr<Config>
    configFromFile(std::filesystem::path const& path)
    {
        auto cfg = std::make_unique<Config>();
        cfg->setup(path.string(), true, true, true);
        test::setupConfigForUnitTests(*cfg);
        return cfg;
    }

    void
    testParams()
    {
        testcase("Params");
        using namespace test::jtx;

        Env env{*this};
        auto const expectInvalid = [&](std::string const& params, std::string const& message) {
            auto const result = env.rpc("json", "config_reload", params)[jss::result];
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(result[jss::error_message] == message);
        };
        expectInvalid("{}", "Missing field 'sections'.");
        expectInvalid(R"({"sections": "transaction_queue"})", "Invalid field 'sections'.");
        expectInvalid(R"({"sections": []})", "Invalid field 'sections'.");
        expectInvalid(R"({"sections": [1]})", "Invalid field 'sections'.");
        expectInvalid(R"({"sections": ["transaction_queue", "ips"]})", kIpsMessage);

        // Valid params, but the server did not load a config file.
        auto const result = env.rpc("config_reload", "transaction_queue")[jss::result];
        BEAST_EXPECT(result[jss::error] == "notSupported");
        BEAST_EXPECT(result[jss::error_code] == RpcNotSupported);
    }

    void
    testReload()
    {
        testcase("Reload");
        using namespace test::jtx;

        detail::FileDirGuard const file(
            *this,
            "test_config_reload",
            "xrpld.cfg",
            configText("minimum_txn_in_ledger_standalone = 3\n"));

        Env env{*this, configFromFile(file.file())};
        auto const perLedger = [&] {
            return env.app().getTxQ().getMetrics(*env.current()).txPerLedger;
        };
        BEAST_EXPECT(perLedger() == 3);

        writeFile(file.file(), configText("minimum_txn_in_ledger_standalone = 7\n"));

        // A request that includes a section that needs a restart applies
        // nothing.
        {
            auto const result = env.rpc("config_reload", "transaction_queue", "ips")[jss::result];
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(result[jss::error_code] == RpcInvalidParams);
            BEAST_EXPECT(result[jss::error_message] == kIpsMessage);
            BEAST_EXPECT(perLedger() == 3);
        }

        {
            auto const result = env.rpc("config_reload", "transaction_queue")[jss::result];
            BEAST_EXPECT(result[jss::status] == "success");
            BEAST_EXPECT(result[jss::applied].size() == 1);
            BEAST_EXPECT(result[jss::applied][0u] == "transaction_queue");
            BEAST_EXPECT(perLedger() == 7);
        }

        // An invalid file changes nothing.
        writeFile(
            file.file(),
            configText(
                "minimum_txn_in_ledger_standalone = 9\n"
                "maximum_txn_in_ledger = 1\n"));
        {
            auto const result = env.rpc("config_reload", "transaction_queue")[jss::result];
            BEAST_EXPECT(result[jss::error] == "internal");
            BEAST_EXPECT(
                result[jss::error_message].asString().starts_with("Invalid config file: "));
            BEAST_EXPECT(perLedger() == 7);
        }

        // A missing file changes nothing.
        std::filesystem::remove(file.file());
        {
            auto const result = env.rpc("config_reload", "transaction_queue")[jss::result];
            BEAST_EXPECT(result[jss::error] == "internal");
            BEAST_EXPECT(perLedger() == 7);
        }
        // Put a file back so that the guard can remove it.
        writeFile(file.file(), "");
    }

public:
    void
    run() override
    {
        testParams();
        testReload();
    }
};

BEAST_DEFINE_TESTSUITE(ConfigReload, rpc, xrpl);

}  // namespace xrpl
