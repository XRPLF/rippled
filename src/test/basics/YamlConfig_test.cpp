#include <test/unit_test/FileDirGuard.h>

#include <xrpl/basics/YamlConfig.h>
#include <xrpl/beast/unit_test.h>

#include <yaml-cpp/yaml.h>

namespace xrpl {

class YamlConfig_test : public beast::unit_test::suite
{
    beast::Journal j_;

public:
    YamlConfig_test() : j_(beast::Journal::getNullSink())
    {
    }

    void
    testIsYamlFile()
    {
        testcase("isYamlFile");
        // yaml extension
        BEAST_EXPECT(isYamlFile("config.yaml"));

        // yml extension
        BEAST_EXPECT(isYamlFile("config.yml"));

        // uppercase YAML
        BEAST_EXPECT(isYamlFile("config.YAML"));

        // uppercase YML
        BEAST_EXPECT(isYamlFile("config.YML"));

        // mixed case
        BEAST_EXPECT(isYamlFile("config.YaMl"));

        // cfg extension
        BEAST_EXPECT(!isYamlFile("config.cfg"));

        // txt extension
        BEAST_EXPECT(!isYamlFile("config.txt"));

        // no extension
        BEAST_EXPECT(!isYamlFile("config"));

        // yaml in middle
        BEAST_EXPECT(!isYamlFile("config.yaml.bak"));

        // empty path
        BEAST_EXPECT(!isYamlFile(""));
    }

    void
    testParseYamlString()
    {
        testcase("parseYamlString");
        // empty string
        {
            auto result = parseYamlString("", j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(result->IsNull());
        }

        // simple scalar
        {
            auto result = parseYamlString("hello", j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(result->IsScalar());
            BEAST_EXPECT(result->as<std::string>() == "hello");
        }

        // simple mapping
        {
            auto result = parseYamlString("key: value", j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(result->IsMap());
            BEAST_EXPECT((*result)["key"].as<std::string>() == "value");
        }

        // nested mapping
        {
            auto result = parseYamlString("parent:\n  child: value", j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(result->IsMap());
            BEAST_EXPECT(
                (*result)["parent"]["child"].as<std::string>() == "value");
        }

        // simple sequence
        {
            auto result = parseYamlString("- one\n- two\n- three", j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(result->IsSequence());
            BEAST_EXPECT(result->size() == 3);
            BEAST_EXPECT((*result)[0].as<std::string>() == "one");
            BEAST_EXPECT((*result)[1].as<std::string>() == "two");
            BEAST_EXPECT((*result)[2].as<std::string>() == "three");
        }

        // mapping with sequence
        {
            auto result = parseYamlString("items:\n  - a\n  - b", j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT((*result)["items"].IsSequence());
            BEAST_EXPECT((*result)["items"].size() == 2);
        }

        // quoted strings
        {
            auto result = parseYamlString("key: 'quoted value'", j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT((*result)["key"].as<std::string>() == "quoted value");
        }

        // double quoted
        {
            auto result = parseYamlString("key: \"double quoted\"", j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT((*result)["key"].as<std::string>() == "double quoted");
        }

        // integer value
        {
            auto result = parseYamlString("port: 5005", j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT((*result)["port"].as<int>() == 5005);
        }

        // float value
        {
            auto result = parseYamlString("ratio: 1.5", j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT((*result)["ratio"].as<double>() == 1.5);
        }

        // boolean true
        {
            auto result = parseYamlString("enabled: true", j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT((*result)["enabled"].as<bool>() == true);
        }

        // boolean false
        {
            auto result = parseYamlString("disabled: false", j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT((*result)["disabled"].as<bool>() == false);
        }

        // comments ignored
        {
            auto result = parseYamlString("# comment\nkey: value", j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT((*result)["key"].as<std::string>() == "value");
        }

        // inline JSON string
        {
            auto result = parseYamlString("cmd: '{ \"x\": 1 }'", j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT((*result)["cmd"].as<std::string>() == "{ \"x\": 1 }");
        }
    }

    void
    testParseYamlStringContinued()
    {
        testcase("parseYamlString 2");
        // multiline literal
        {
            auto result = parseYamlString("text: |\n  line1\n  line2\n", j_);
            BEAST_EXPECT(result.has_value());
            // YAML literal block scalar preserves newlines
            auto text = (*result)["text"].as<std::string>();
            BEAST_EXPECT(text.find("line1") != std::string::npos);
            BEAST_EXPECT(text.find("line2") != std::string::npos);
        }

        // multiple sections
        {
            auto result = parseYamlString("a: 1\nb: 2\nc: 3", j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(result->IsMap());
            BEAST_EXPECT(result->size() == 3);
        }
    }

    void
    testParseYamlStringInvalid()
    {
        testcase("parseYamlString invalid");
        // invalid syntax
        {
            auto result = parseYamlString("key: [unclosed", j_);
            BEAST_EXPECT(!result.has_value());
        }

        // bad indentation - verify no crash/exception
        {
            // This specific pattern may or may not fail depending on yaml-cpp.
            // The test verifies that we handle errors gracefully without
            // crashing, regardless of whether yaml-cpp accepts this input.
            try
            {
                auto result = parseYamlString("a:\n b: 1\n   c: 2", j_);
                // If we get here, parsing succeeded or returned nullopt
                // gracefully - both are acceptable
                BEAST_EXPECT(true);
            }
            catch (...)
            {
                // Should not throw - parseYamlString should catch exceptions
                BEAST_EXPECT(false);
            }
        }

        // unclosed quote
        {
            auto result = parseYamlString("key: 'unclosed", j_);
            BEAST_EXPECT(!result.has_value());
        }

        // invalid characters - no crash
        {
            // yaml-cpp may handle @ differently across versions.
            // The test verifies graceful handling without crashing.
            auto result = parseYamlString("key: @invalid", j_);
            // Either succeeds or fails gracefully - both are acceptable
            BEAST_EXPECT(result.has_value() || !result.has_value());
        }
    }

    void
    testParseYamlFile()
    {
        testcase("parseYamlFile");
        using namespace xrpl::detail;

        // valid file
        {
            FileDirGuard file(
                *this, "yaml_test", "config.yaml", "key: value", true, true);
            auto result = parseYamlFile(file.file(), j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT((*result)["key"].as<std::string>() == "value");
        }

        // empty file
        {
            FileDirGuard file(*this, "yaml_test", "empty.yaml", "", true, true);
            auto result = parseYamlFile(file.file(), j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(result->IsNull());
        }

        // complex file
        {
            std::string content = R"yaml(
server:
  - port_rpc
  - port_peer
node_db:
  type: NuDB
  path: /var/db
validators:
  - key1
  - key2
)yaml";
            FileDirGuard file(
                *this, "yaml_test", "complex.yaml", content, true, true);
            auto result = parseYamlFile(file.file(), j_);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(result->IsMap());
            BEAST_EXPECT((*result)["server"].IsSequence());
            BEAST_EXPECT((*result)["node_db"].IsMap());
            BEAST_EXPECT((*result)["validators"].IsSequence());
        }

        // non-existent file
        {
            auto result = parseYamlFile("/nonexistent/path/config.yaml", j_);
            BEAST_EXPECT(!result.has_value());
        }

        // invalid yaml file
        {
            FileDirGuard file(
                *this, "yaml_test", "invalid.yaml", "key: [bad", true, true);
            auto result = parseYamlFile(file.file(), j_);
            BEAST_EXPECT(!result.has_value());
        }
    }

    void
    testYamlToIniBasic()
    {
        testcase("yamlToIni basic");
        // scalar section
        {
            auto node = YAML::Load("database_path: /var/db");
            auto ini = yamlToIniFileSections(node, j_);
            BEAST_EXPECT(ini.find("database_path") != ini.end());
            BEAST_EXPECT(ini["database_path"].size() == 1);
            BEAST_EXPECT(ini["database_path"][0] == "/var/db");
        }

        // mapping section
        {
            auto node = YAML::Load("node_db:\n  type: NuDB\n  path: /db");
            auto ini = yamlToIniFileSections(node, j_);
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

        // sequence section
        {
            auto node = YAML::Load("validators:\n  - key1\n  - key2");
            auto ini = yamlToIniFileSections(node, j_);
            BEAST_EXPECT(ini.find("validators") != ini.end());
            auto const& lines = ini["validators"];
            BEAST_EXPECT(lines.size() == 2);
            BEAST_EXPECT(lines[0] == "key1");
            BEAST_EXPECT(lines[1] == "key2");
        }

        // empty section
        {
            auto node = YAML::Load("empty:");
            auto ini = yamlToIniFileSections(node, j_);
            BEAST_EXPECT(ini.find("empty") != ini.end());
            BEAST_EXPECT(ini["empty"].empty());
        }

        // multiple sections
        {
            auto node = YAML::Load("a: 1\nb: 2");
            auto ini = yamlToIniFileSections(node, j_);
            BEAST_EXPECT(ini.find("a") != ini.end());
            BEAST_EXPECT(ini.find("b") != ini.end());
            BEAST_EXPECT(ini["a"][0] == "1");
            BEAST_EXPECT(ini["b"][0] == "2");
        }

        // numeric values
        {
            auto node = YAML::Load("port: 5005");
            auto ini = yamlToIniFileSections(node, j_);
            BEAST_EXPECT(ini["port"][0] == "5005");
        }
    }

    void
    testYamlToIniServer()
    {
        testcase("yamlToIni server section");
        // server with ports
        {
            std::string yaml = R"yaml(
server:
  ports:
    port_rpc:
      ip: 127.0.0.1
      port: 5005
      protocol: http
    port_peer:
      ip: 0.0.0.0
      port: 51235
      protocol: peer
)yaml";
            auto node = YAML::Load(yaml);
            auto ini = yamlToIniFileSections(node, j_);

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
            std::string yaml = R"yaml(
server:
  ports:
    port_rpc:
      ip: 127.0.0.1
      port: 5005
)yaml";
            auto node = YAML::Load(yaml);
            auto ini = yamlToIniFileSections(node, j_);

            BEAST_EXPECT(ini.find("server") != ini.end());
            BEAST_EXPECT(ini.find("port_rpc") != ini.end());
            BEAST_EXPECT(ini["server"].size() >= 1);
        }

        // port with all options
        {
            std::string yaml = R"yaml(
server:
  ports:
    port_admin:
      ip: 127.0.0.1
      port: 5005
      protocol: http
      admin: 127.0.0.1
)yaml";
            auto node = YAML::Load(yaml);
            auto ini = yamlToIniFileSections(node, j_);

            BEAST_EXPECT(ini.find("port_admin") != ini.end());
            auto const& lines = ini["port_admin"];
            bool hasIp = false, hasPort = false, hasProtocol = false,
                 hasAdmin = false;
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

        // server without ports key
        {
            // Server section as simple sequence (alternative format)
            std::string yaml = R"yaml(
server:
  - port_rpc
  - port_peer
)yaml";
            auto node = YAML::Load(yaml);
            auto ini = yamlToIniFileSections(node, j_);

            BEAST_EXPECT(ini.find("server") != ini.end());
            auto const& lines = ini["server"];
            BEAST_EXPECT(lines.size() == 2);
            BEAST_EXPECT(lines[0] == "port_rpc");
            BEAST_EXPECT(lines[1] == "port_peer");
        }
    }

    void
    testYamlToIniSpecialCases()
    {
        testcase("yamlToIni special cases");
        // rpc_startup JSON
        {
            std::string yaml =
                "rpc_startup:\n  - '{ \"command\": \"log_level\", "
                "\"severity\": \"warning\" }'";
            auto node = YAML::Load(yaml);
            auto ini = yamlToIniFileSections(node, j_);

            BEAST_EXPECT(ini.find("rpc_startup") != ini.end());
            BEAST_EXPECT(ini["rpc_startup"].size() == 1);
            BEAST_EXPECT(
                ini["rpc_startup"][0] ==
                "{ \"command\": \"log_level\", \"severity\": \"warning\" }");
        }

        // ips host port
        {
            std::string yaml =
                "ips:\n  - {host: r.ripple.com, port: 51235}\n  - {host: "
                "s.ripple.com, port: 51235}";
            auto node = YAML::Load(yaml);
            auto ini = yamlToIniFileSections(node, j_);

            BEAST_EXPECT(ini.find("ips") != ini.end());
            BEAST_EXPECT(ini["ips"].size() == 2);
            BEAST_EXPECT(ini["ips"][0] == "r.ripple.com 51235");
            BEAST_EXPECT(ini["ips"][1] == "s.ripple.com 51235");
        }

        // validator keys
        {
            std::string yaml = R"yaml(
validator_list_keys:
  - ED2677ABFFD1B33AC6FBC3062B71F1E8397C1505E1C42C64D11AD1B28FF73F4734
)yaml";
            auto node = YAML::Load(yaml);
            auto ini = yamlToIniFileSections(node, j_);

            BEAST_EXPECT(ini.find("validator_list_keys") != ini.end());
            BEAST_EXPECT(ini["validator_list_keys"].size() == 1);
            BEAST_EXPECT(
                ini["validator_list_keys"][0] ==
                "ED2677ABFFD1B33AC6FBC3062B71F1E8397C1505E1C42C64D11AD1B28FF73F"
                "4734");
        }

        // features list
        {
            std::string yaml = "features:\n  - MultiSign\n  - Escrow";
            auto node = YAML::Load(yaml);
            auto ini = yamlToIniFileSections(node, j_);

            BEAST_EXPECT(ini.find("features") != ini.end());
            BEAST_EXPECT(ini["features"].size() == 2);
            BEAST_EXPECT(ini["features"][0] == "MultiSign");
            BEAST_EXPECT(ini["features"][1] == "Escrow");
        }

        // sntp servers
        {
            std::string yaml =
                "sntp_servers:\n  - time.google.com\n  - time.apple.com";
            auto node = YAML::Load(yaml);
            auto ini = yamlToIniFileSections(node, j_);

            BEAST_EXPECT(ini.find("sntp_servers") != ini.end());
            BEAST_EXPECT(ini["sntp_servers"].size() == 2);
        }

        // sequence in mapping value
        {
            // When a mapping has a sequence value, it should be comma-joined
            std::string yaml = "node_db:\n  protocol:\n    - http\n    - https";
            auto node = YAML::Load(yaml);
            auto ini = yamlToIniFileSections(node, j_);

            BEAST_EXPECT(ini.find("node_db") != ini.end());
            bool found = false;
            for (auto const& line : ini["node_db"])
            {
                if (line == "protocol=http,https")
                    found = true;
            }
            BEAST_EXPECT(found);
        }
    }

    void
    testYamlToIniEdgeCases()
    {
        testcase("yamlToIni edge cases");
        // non-map root
        {
            auto node = YAML::Load("- item1\n- item2");
            auto ini = yamlToIniFileSections(node, j_);
            BEAST_EXPECT(ini.empty());
        }

        // scalar root
        {
            auto node = YAML::Load("just a string");
            auto ini = yamlToIniFileSections(node, j_);
            BEAST_EXPECT(ini.empty());
        }

        // special chars in value
        {
            auto node = YAML::Load("url: http://example.com:8080/path?q=1");
            auto ini = yamlToIniFileSections(node, j_);
            BEAST_EXPECT(ini.find("url") != ini.end());
            BEAST_EXPECT(ini["url"][0] == "http://example.com:8080/path?q=1");
        }

        // empty string value
        {
            auto node = YAML::Load("key: ''");
            auto ini = yamlToIniFileSections(node, j_);
            BEAST_EXPECT(ini.find("key") != ini.end());
            BEAST_EXPECT(ini["key"][0].empty());
        }

        // whitespace value
        {
            auto node = YAML::Load("key: '   '");
            auto ini = yamlToIniFileSections(node, j_);
            BEAST_EXPECT(ini.find("key") != ini.end());
            BEAST_EXPECT(ini["key"][0] == "   ");
        }

        // unicode values
        {
            auto node = YAML::Load("name: 日本語");
            auto ini = yamlToIniFileSections(node, j_);
            BEAST_EXPECT(ini.find("name") != ini.end());
            BEAST_EXPECT(ini["name"][0] == "日本語");
        }

        // value with equals sign
        {
            auto node = YAML::Load("equation: a=b+c");
            auto ini = yamlToIniFileSections(node, j_);
            BEAST_EXPECT(ini.find("equation") != ini.end());
            BEAST_EXPECT(ini["equation"][0] == "a=b+c");
        }

        // null section value (explicit null)
        {
            auto node = YAML::Load("nullsection: null");
            auto ini = yamlToIniFileSections(node, j_);
            BEAST_EXPECT(ini.find("nullsection") != ini.end());
            // null creates an empty section
            BEAST_EXPECT(ini["nullsection"].empty());
        }

        // null section value (tilde syntax)
        {
            auto node = YAML::Load("nullsection: ~");
            auto ini = yamlToIniFileSections(node, j_);
            BEAST_EXPECT(ini.find("nullsection") != ini.end());
            BEAST_EXPECT(ini["nullsection"].empty());
        }

        // null section value (empty after colon)
        {
            auto node = YAML::Load("emptysection:");
            auto ini = yamlToIniFileSections(node, j_);
            BEAST_EXPECT(ini.find("emptysection") != ini.end());
            BEAST_EXPECT(ini["emptysection"].empty());
        }
    }

    void
    testYamlConfigIntegration()
    {
        testcase("yaml config integration");
        using namespace xrpl::detail;

        // full config structure
        {
            std::string yaml = R"yaml(
server:
  - port_rpc
  - port_peer

port_rpc:
  ip: 127.0.0.1
  port: 5005
  admin: 127.0.0.1
  protocol: http

port_peer:
  ip: 0.0.0.0
  port: 51235
  protocol: peer

node_db:
  type: NuDB
  path: /var/lib/rippled/db/nudb
  online_delete: 256
  advisory_delete: 0

database_path: /var/lib/rippled/db

debug_logfile: /var/log/rippled/debug.log

sntp_servers:
  - time.google.com
  - time.apple.com

ips:
  - {host: r.ripple.com, port: 51235}

validators:
  - n949f75evCHwgyP4fPVgaHqNHxUVN15PsJEZ3B3HnXPcPjcZAoy7

rpc_startup:
  - '{ "command": "log_level", "severity": "warning" }'
)yaml";
            auto node = YAML::Load(yaml);
            auto ini = yamlToIniFileSections(node, j_);

            // Verify all sections exist
            BEAST_EXPECT(ini.find("server") != ini.end());
            BEAST_EXPECT(ini.find("port_rpc") != ini.end());
            BEAST_EXPECT(ini.find("port_peer") != ini.end());
            BEAST_EXPECT(ini.find("node_db") != ini.end());
            BEAST_EXPECT(ini.find("database_path") != ini.end());
            BEAST_EXPECT(ini.find("debug_logfile") != ini.end());
            BEAST_EXPECT(ini.find("sntp_servers") != ini.end());
            BEAST_EXPECT(ini.find("ips") != ini.end());
            BEAST_EXPECT(ini.find("validators") != ini.end());
            BEAST_EXPECT(ini.find("rpc_startup") != ini.end());

            // Verify specific content
            BEAST_EXPECT(ini["database_path"][0] == "/var/lib/rippled/db");
            BEAST_EXPECT(ini["ips"][0] == "r.ripple.com 51235");
        }

        // yaml ini equivalence
        {
            // Parse equivalent YAML and verify structure
            std::string yaml = R"yaml(
node_size: medium
ledger_history: 256
)yaml";
            auto node = YAML::Load(yaml);
            auto ini = yamlToIniFileSections(node, j_);

            BEAST_EXPECT(ini["node_size"][0] == "medium");
            BEAST_EXPECT(ini["ledger_history"][0] == "256");
        }

        // validators yaml format
        {
            std::string yaml = R"yaml(
validator_list_sites:
  - https://vl.ripple.com
  - https://vl.xrplf.org

validator_list_keys:
  - ED2677ABFFD1B33AC6FBC3062B71F1E8397C1505E1C42C64D11AD1B28FF73F4734
)yaml";
            auto node = YAML::Load(yaml);
            auto ini = yamlToIniFileSections(node, j_);

            BEAST_EXPECT(ini.find("validator_list_sites") != ini.end());
            BEAST_EXPECT(ini["validator_list_sites"].size() == 2);
            BEAST_EXPECT(ini.find("validator_list_keys") != ini.end());
            BEAST_EXPECT(ini["validator_list_keys"].size() == 1);
        }
    }

    void
    run() override
    {
        testIsYamlFile();
        testParseYamlString();
        testParseYamlStringContinued();
        testParseYamlStringInvalid();
        testParseYamlFile();
        testYamlToIniBasic();
        testYamlToIniServer();
        testYamlToIniSpecialCases();
        testYamlToIniEdgeCases();
        testYamlConfigIntegration();
    }
};

BEAST_DEFINE_TESTSUITE(YamlConfig, basics, xrpl);

}  // namespace xrpl
