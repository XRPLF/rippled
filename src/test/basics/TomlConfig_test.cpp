#include <test/unit_test/FileDirGuard.h>

#include <xrpl/basics/TomlConfig.h>
#include <xrpl/beast/unit_test.h>

#include <toml++/toml.hpp>

namespace xrpl {

class TomlConfig_test : public beast::unit_test::suite
{
    beast::Journal j_;

public:
    TomlConfig_test() : j_(beast::Journal::getNullSink())
    {
    }

    void
    testIsTomlFile()
    {
        testcase("isTomlFile");
        // toml extension
        BEAST_EXPECT(isTomlFile("config.toml"));

        // uppercase TOML
        BEAST_EXPECT(isTomlFile("config.TOML"));

        // mixed case
        BEAST_EXPECT(isTomlFile("config.ToMl"));

        // cfg extension
        BEAST_EXPECT(!isTomlFile("config.cfg"));

        // txt extension
        BEAST_EXPECT(!isTomlFile("config.txt"));

        // yaml extension
        BEAST_EXPECT(!isTomlFile("config.yaml"));

        // no extension
        BEAST_EXPECT(!isTomlFile("config"));

        // toml in middle
        BEAST_EXPECT(!isTomlFile("config.toml.bak"));

        // empty path
        BEAST_EXPECT(!isTomlFile(""));
    }

    void
    testParseTomlString()
    {
        testcase("parseTomlString");
        // empty string
        {
            auto result = parseTomlString("", j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(result->empty());
        }

        // simple key-value
        {
            auto result = parseTomlString("key = \"value\"", j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT((*result)["key"].as_string()->get() == "value");
        }

        // nested table
        {
            auto result = parseTomlString("[parent]\nchild = \"value\"", j_);
            BEAST_EXPECT(result.has_value());
            auto* parent = (*result)["parent"].as_table();
            BEAST_EXPECT(parent != nullptr);
            BEAST_EXPECT((*parent)["child"].as_string()->get() == "value");
        }

        // simple array
        {
            auto result = parseTomlString("items = [\"one\", \"two\", \"three\"]", j_);
            BEAST_EXPECT(result.has_value());
            auto* arr = (*result)["items"].as_array();
            BEAST_EXPECT(arr != nullptr);
            BEAST_EXPECT(arr->size() == 3);
            BEAST_EXPECT((*arr)[0].as_string()->get() == "one");
            BEAST_EXPECT((*arr)[1].as_string()->get() == "two");
            BEAST_EXPECT((*arr)[2].as_string()->get() == "three");
        }

        // integer value
        {
            auto result = parseTomlString("port = 5005", j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT((*result)["port"].as_integer()->get() == 5005);
        }

        // float value
        {
            auto result = parseTomlString("ratio = 1.5", j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT((*result)["ratio"].as_floating_point()->get() == 1.5);
        }

        // boolean true
        {
            auto result = parseTomlString("enabled = true", j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT((*result)["enabled"].as_boolean()->get() == true);
        }

        // boolean false
        {
            auto result = parseTomlString("disabled = false", j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT((*result)["disabled"].as_boolean()->get() == false);
        }

        // comments ignored
        {
            auto result = parseTomlString("# comment\nkey = \"value\"", j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT((*result)["key"].as_string()->get() == "value");
        }

        // inline table
        {
            auto result = parseTomlString("point = { x = 1, y = 2 }", j_);
            BEAST_EXPECT(result.has_value());
            auto* point = (*result)["point"].as_table();
            BEAST_EXPECT(point != nullptr);
            BEAST_EXPECT((*point)["x"].as_integer()->get() == 1);
            BEAST_EXPECT((*point)["y"].as_integer()->get() == 2);
        }
    }

    void
    testParseTomlStringContinued()
    {
        testcase("parseTomlString 2");
        // multiline string
        {
            auto result = parseTomlString("text = \"\"\"\nline1\nline2\n\"\"\"", j_);
            BEAST_EXPECT(result.has_value());
            auto text = (*result)["text"].as_string()->get();
            BEAST_EXPECT(text.find("line1") != std::string::npos);
            BEAST_EXPECT(text.find("line2") != std::string::npos);
        }

        // multiple sections
        {
            auto result = parseTomlString("a = 1\nb = 2\nc = 3", j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(result->size() == 3);
        }
    }

    void
    testParseTomlStringInvalid()
    {
        testcase("parseTomlString invalid");
        // invalid syntax - unclosed bracket
        {
            auto result = parseTomlString("key = [unclosed", j_);
            BEAST_EXPECT(!result.has_value());
        }

        // unclosed quote
        {
            auto result = parseTomlString("key = \"unclosed", j_);
            BEAST_EXPECT(!result.has_value());
        }

        // invalid key
        {
            auto result = parseTomlString("= value", j_);
            BEAST_EXPECT(!result.has_value());
        }

        // duplicate keys
        {
            auto result = parseTomlString("key = 1\nkey = 2", j_);
            BEAST_EXPECT(!result.has_value());
        }
    }

    void
    testParseTomlFile()
    {
        testcase("parseTomlFile");
        using namespace xrpl::detail;

        // valid file
        {
            FileDirGuard file(*this, "toml_test", "config.toml", "key = \"value\"", true, true);
            auto result = parseTomlFile(file.file(), j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT((*result)["key"].as_string()->get() == "value");
        }

        // empty file
        {
            FileDirGuard file(*this, "toml_test", "empty.toml", "", true, true);
            auto result = parseTomlFile(file.file(), j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(result->empty());
        }

        // complex file
        {
            std::string content = R"toml(
validators = ["key1", "key2"]

[server]
ports = ["port_rpc", "port_peer"]

[node_db]
type = "NuDB"
path = "/var/db"
)toml";
            FileDirGuard file(*this, "toml_test", "complex.toml", content, true, true);
            auto result = parseTomlFile(file.file(), j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT((*result)["server"].is_table());
            BEAST_EXPECT((*result)["node_db"].is_table());
            BEAST_EXPECT((*result)["validators"].is_array());
        }

        // non-existent file
        {
            auto result = parseTomlFile("/nonexistent/path/config.toml", j_);
            BEAST_EXPECT(!result.has_value());
        }

        // invalid toml file
        {
            FileDirGuard file(*this, "toml_test", "invalid.toml", "key = [bad", true, true);
            auto result = parseTomlFile(file.file(), j_);
            BEAST_EXPECT(!result.has_value());
        }
    }

    void
    testTomlToIniBasic()
    {
        testcase("tomlToIni basic");
        // scalar section
        {
            auto tbl = toml::parse("database_path = \"/var/db\"");
            auto ini = tomlToIniFileSections(tbl, j_);
            BEAST_EXPECT(ini.find("database_path") != ini.end());
            BEAST_EXPECT(ini["database_path"].size() == 1);
            BEAST_EXPECT(ini["database_path"][0] == "/var/db");
        }

        // table section
        {
            auto tbl = toml::parse("[node_db]\ntype = \"NuDB\"\npath = \"/db\"");
            auto ini = tomlToIniFileSections(tbl, j_);
            BEAST_EXPECT(ini.find("node_db") != ini.end());
            auto const& lines = ini["node_db"];
            BEAST_EXPECT(lines.size() == 2);
            bool hasType = false, hasPath = false;
            for (auto const& line : lines)
            {
                if (line == "type=NuDB")
                    hasType = true;
                if (line == "path=/db")
                    hasPath = true;
            }
            BEAST_EXPECT(hasType);
            BEAST_EXPECT(hasPath);
        }

        // array section
        {
            auto tbl = toml::parse("validators = [\"key1\", \"key2\"]");
            auto ini = tomlToIniFileSections(tbl, j_);
            BEAST_EXPECT(ini.find("validators") != ini.end());
            auto const& lines = ini["validators"];
            BEAST_EXPECT(lines.size() == 2);
            BEAST_EXPECT(lines[0] == "key1");
            BEAST_EXPECT(lines[1] == "key2");
        }

        // multiple sections
        {
            auto tbl = toml::parse("a = 1\nb = 2");
            auto ini = tomlToIniFileSections(tbl, j_);
            BEAST_EXPECT(ini.find("a") != ini.end());
            BEAST_EXPECT(ini.find("b") != ini.end());
            BEAST_EXPECT(ini["a"][0] == "1");
            BEAST_EXPECT(ini["b"][0] == "2");
        }

        // numeric values
        {
            auto tbl = toml::parse("port = 5005");
            auto ini = tomlToIniFileSections(tbl, j_);
            BEAST_EXPECT(ini["port"][0] == "5005");
        }
    }

    void
    testTomlToIniServer()
    {
        testcase("tomlToIni server section");
        // server with ports
        {
            std::string tomlStr = R"toml(
[server.ports.port_rpc]
ip = "127.0.0.1"
port = 5005
protocol = "http"

[server.ports.port_peer]
ip = "0.0.0.0"
port = 51235
protocol = "peer"
)toml";
            auto tbl = toml::parse(tomlStr);
            auto ini = tomlToIniFileSections(tbl, j_);

            // Server section should have port names
            BEAST_EXPECT(ini.find("server") != ini.end());
            auto const& serverLines = ini["server"];
            bool hasRpc = false, hasPeer = false;
            for (auto const& line : serverLines)
            {
                if (line == "port_rpc")
                    hasRpc = true;
                if (line == "port_peer")
                    hasPeer = true;
            }
            BEAST_EXPECT(hasRpc);
            BEAST_EXPECT(hasPeer);

            // Individual port sections should exist
            BEAST_EXPECT(ini.find("port_rpc") != ini.end());
            BEAST_EXPECT(ini.find("port_peer") != ini.end());
        }

        // single port
        {
            std::string tomlStr = R"toml(
[server.ports.port_rpc]
ip = "127.0.0.1"
port = 5005
)toml";
            auto tbl = toml::parse(tomlStr);
            auto ini = tomlToIniFileSections(tbl, j_);

            BEAST_EXPECT(ini.find("server") != ini.end());
            BEAST_EXPECT(ini.find("port_rpc") != ini.end());
            BEAST_EXPECT(ini["server"].size() >= 1);
        }

        // port with all options
        {
            std::string tomlStr = R"toml(
[server.ports.port_admin]
ip = "127.0.0.1"
port = 5005
protocol = "http"
admin = "127.0.0.1"
)toml";
            auto tbl = toml::parse(tomlStr);
            auto ini = tomlToIniFileSections(tbl, j_);

            BEAST_EXPECT(ini.find("port_admin") != ini.end());
            auto const& lines = ini["port_admin"];
            bool hasIp = false, hasPort = false, hasProtocol = false, hasAdmin = false;
            for (auto const& line : lines)
            {
                if (line == "ip=127.0.0.1")
                    hasIp = true;
                if (line == "port=5005")
                    hasPort = true;
                if (line == "protocol=http")
                    hasProtocol = true;
                if (line == "admin=127.0.0.1")
                    hasAdmin = true;
            }
            BEAST_EXPECT(hasIp);
            BEAST_EXPECT(hasPort);
            BEAST_EXPECT(hasProtocol);
            BEAST_EXPECT(hasAdmin);
        }
    }

    void
    testTomlToIniSpecialCases()
    {
        testcase("tomlToIni special cases");
        // rpc_startup JSON
        {
            std::string tomlStr =
                "rpc_startup = ['{ \"command\": \"log_level\", "
                "\"severity\": \"warning\" }']";
            auto tbl = toml::parse(tomlStr);
            auto ini = tomlToIniFileSections(tbl, j_);

            BEAST_EXPECT(ini.find("rpc_startup") != ini.end());
            BEAST_EXPECT(ini["rpc_startup"].size() == 1);
            BEAST_EXPECT(
                ini["rpc_startup"][0] ==
                "{ \"command\": \"log_level\", \"severity\": \"warning\" }");
        }

        // ips host port using array of tables
        {
            std::string tomlStr = R"toml(
[[ips]]
host = "r.ripple.com"
port = 51235

[[ips]]
host = "s.ripple.com"
port = 51235
)toml";
            auto tbl = toml::parse(tomlStr);
            auto ini = tomlToIniFileSections(tbl, j_);

            BEAST_EXPECT(ini.find("ips") != ini.end());
            BEAST_EXPECT(ini["ips"].size() == 2);
            BEAST_EXPECT(ini["ips"][0] == "r.ripple.com 51235");
            BEAST_EXPECT(ini["ips"][1] == "s.ripple.com 51235");
        }

        // validator keys
        {
            std::string tomlStr = R"toml(
validator_list_keys = [
    "ED2677ABFFD1B33AC6FBC3062B71F1E8397C1505E1C42C64D11AD1B28FF73F4734"
]
)toml";
            auto tbl = toml::parse(tomlStr);
            auto ini = tomlToIniFileSections(tbl, j_);

            BEAST_EXPECT(ini.find("validator_list_keys") != ini.end());
            BEAST_EXPECT(ini["validator_list_keys"].size() == 1);
            BEAST_EXPECT(
                ini["validator_list_keys"][0] ==
                "ED2677ABFFD1B33AC6FBC3062B71F1E8397C1505E1C42C64D11AD1B28FF73F"
                "4734");
        }

        // features list
        {
            auto tbl = toml::parse("features = [\"MultiSign\", \"Escrow\"]");
            auto ini = tomlToIniFileSections(tbl, j_);

            BEAST_EXPECT(ini.find("features") != ini.end());
            BEAST_EXPECT(ini["features"].size() == 2);
            BEAST_EXPECT(ini["features"][0] == "MultiSign");
            BEAST_EXPECT(ini["features"][1] == "Escrow");
        }

        // sntp servers
        {
            auto tbl = toml::parse("sntp_servers = [\"time.google.com\", \"time.apple.com\"]");
            auto ini = tomlToIniFileSections(tbl, j_);

            BEAST_EXPECT(ini.find("sntp_servers") != ini.end());
            BEAST_EXPECT(ini["sntp_servers"].size() == 2);
        }
    }

    void
    testTomlToIniEdgeCases()
    {
        testcase("tomlToIni edge cases");
        // special chars in value
        {
            auto tbl = toml::parse("url = \"http://example.com:8080/path?q=1\"");
            auto ini = tomlToIniFileSections(tbl, j_);
            BEAST_EXPECT(ini.find("url") != ini.end());
            BEAST_EXPECT(ini["url"][0] == "http://example.com:8080/path?q=1");
        }

        // empty string value
        {
            auto tbl = toml::parse("key = \"\"");
            auto ini = tomlToIniFileSections(tbl, j_);
            BEAST_EXPECT(ini.find("key") != ini.end());
            BEAST_EXPECT(ini["key"][0].empty());
        }

        // whitespace value
        {
            auto tbl = toml::parse("key = \"   \"");
            auto ini = tomlToIniFileSections(tbl, j_);
            BEAST_EXPECT(ini.find("key") != ini.end());
            BEAST_EXPECT(ini["key"][0] == "   ");
        }

        // unicode values
        {
            auto tbl = toml::parse("name = \"日本語\"");
            auto ini = tomlToIniFileSections(tbl, j_);
            BEAST_EXPECT(ini.find("name") != ini.end());
            BEAST_EXPECT(ini["name"][0] == "日本語");
        }

        // value with equals sign
        {
            auto tbl = toml::parse("equation = \"a=b+c\"");
            auto ini = tomlToIniFileSections(tbl, j_);
            BEAST_EXPECT(ini.find("equation") != ini.end());
            BEAST_EXPECT(ini["equation"][0] == "a=b+c");
        }

        // boolean values
        {
            auto tbl = toml::parse("flag = true");
            auto ini = tomlToIniFileSections(tbl, j_);
            BEAST_EXPECT(ini.find("flag") != ini.end());
            BEAST_EXPECT(ini["flag"][0] == "true");
        }
    }

    void
    testTomlConfigIntegration()
    {
        testcase("toml config integration");
        using namespace xrpl::detail;

        // full config structure
        {
            // In TOML, root-level keys must come before any [section] headers
            std::string tomlStr = R"toml(
database_path = "/var/lib/rippled/db"
debug_logfile = "/var/log/rippled/debug.log"
sntp_servers = ["time.google.com", "time.apple.com"]
validators = ["n949f75evCHwgyP4fPVgaHqNHxUVN15PsJEZ3B3HnXPcPjcZAoy7"]
rpc_startup = ['{ "command": "log_level", "severity": "warning" }']

[server]
name = "test"

[port_rpc]
ip = "127.0.0.1"
port = 5005
admin = "127.0.0.1"
protocol = "http"

[port_peer]
ip = "0.0.0.0"
port = 51235
protocol = "peer"

[node_db]
type = "NuDB"
path = "/var/lib/rippled/db/nudb"
online_delete = 256
advisory_delete = 0
)toml";
            auto tbl = toml::parse(tomlStr);
            auto ini = tomlToIniFileSections(tbl, j_);

            // Verify all sections exist
            BEAST_EXPECT(ini.find("server") != ini.end());
            BEAST_EXPECT(ini.find("port_rpc") != ini.end());
            BEAST_EXPECT(ini.find("port_peer") != ini.end());
            BEAST_EXPECT(ini.find("node_db") != ini.end());
            BEAST_EXPECT(ini.find("database_path") != ini.end());
            BEAST_EXPECT(ini.find("debug_logfile") != ini.end());
            BEAST_EXPECT(ini.find("sntp_servers") != ini.end());
            BEAST_EXPECT(ini.find("validators") != ini.end());
            BEAST_EXPECT(ini.find("rpc_startup") != ini.end());

            // Verify specific content
            BEAST_EXPECT(ini["database_path"][0] == "/var/lib/rippled/db");
        }

        // toml ini equivalence
        {
            auto tbl = toml::parse("node_size = \"medium\"\nledger_history = 256");
            auto ini = tomlToIniFileSections(tbl, j_);

            BEAST_EXPECT(ini["node_size"][0] == "medium");
            BEAST_EXPECT(ini["ledger_history"][0] == "256");
        }

        // validators toml format
        {
            std::string tomlStr = R"toml(
validator_list_sites = [
    "https://vl.ripple.com",
    "https://vl.xrplf.org"
]

validator_list_keys = [
    "ED2677ABFFD1B33AC6FBC3062B71F1E8397C1505E1C42C64D11AD1B28FF73F4734"
]
)toml";
            auto tbl = toml::parse(tomlStr);
            auto ini = tomlToIniFileSections(tbl, j_);

            BEAST_EXPECT(ini.find("validator_list_sites") != ini.end());
            BEAST_EXPECT(ini["validator_list_sites"].size() == 2);
            BEAST_EXPECT(ini.find("validator_list_keys") != ini.end());
            BEAST_EXPECT(ini["validator_list_keys"].size() == 1);
        }
    }

    void
    run() override
    {
        testIsTomlFile();
        testParseTomlString();
        testParseTomlStringContinued();
        testParseTomlStringInvalid();
        testParseTomlFile();
        testTomlToIniBasic();
        testTomlToIniServer();
        testTomlToIniSpecialCases();
        testTomlToIniEdgeCases();
        testTomlConfigIntegration();
    }
};

BEAST_DEFINE_TESTSUITE(TomlConfig, basics, xrpl);

}  // namespace xrpl
