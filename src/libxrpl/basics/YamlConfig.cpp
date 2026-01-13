#include <xrpl/basics/FileUtilities.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/YamlConfig.h>

#include <yaml-cpp/yaml.h>

#include <algorithm>

namespace xrpl {

std::optional<YAML::Node>
parseYamlFile(boost::filesystem::path const& path, beast::Journal j)
{
    boost::system::error_code ec;
    auto const contents = getFileContents(ec, path);

    if (ec)
    {
        JLOG(j.error()) << "Failed to read YAML file '" << path.string()
                        << "': " << ec.message();
        return std::nullopt;
    }

    return parseYamlString(contents, j);
}

std::optional<YAML::Node>
parseYamlString(std::string const& input, beast::Journal j)
{
    try
    {
        return YAML::Load(input);
    }
    catch (YAML::ParserException const& e)
    {
        JLOG(j.error()) << "YAML parse error at line " << e.mark.line + 1
                        << ", column " << e.mark.column + 1 << ": " << e.msg;
        return std::nullopt;
    }
    catch (YAML::Exception const& e)
    {
        JLOG(j.error()) << "YAML error: " << e.what();
        return std::nullopt;
    }
}

namespace {

// Helper to convert a YAML scalar to string
std::string
nodeToString(YAML::Node const& node)
{
    if (node.IsScalar())
        return node.as<std::string>();
    return {};
}

// Helper to process a YAML mapping into key=value lines
void
processMapping(
    YAML::Node const& node,
    std::vector<std::string>& lines,
    beast::Journal j)
{
    for (auto const& pair : node)
    {
        auto const key = pair.first.as<std::string>();
        auto const& value = pair.second;

        if (value.IsScalar())
        {
            lines.push_back(key + "=" + value.as<std::string>());
        }
        else if (value.IsSequence())
        {
            // For sequences in a mapping, join values with comma
            std::string combined;
            for (std::size_t i = 0; i < value.size(); ++i)
            {
                if (i > 0)
                    combined += ",";
                combined += value[i].as<std::string>();
            }
            lines.push_back(key + "=" + combined);
        }
        else
        {
            JLOG(j.warn()) << "Skipping nested mapping for key '" << key << "'";
        }
    }
}

// Helper to process a YAML sequence into multiple lines
void
processSequence(
    YAML::Node const& node,
    std::vector<std::string>& lines,
    beast::Journal j)
{
    for (auto const& item : node)
    {
        if (item.IsScalar())
        {
            lines.push_back(item.as<std::string>());
        }
        else if (item.IsMap())
        {
            // For sequences of maps, convert each map item to key=value
            // This handles cases like ips: [{host: "x", port: 51235}]
            // Convert to "x 51235" format for compatibility
            auto hostIt = item["host"];
            auto portIt = item["port"];
            if (hostIt && portIt)
            {
                lines.push_back(
                    hostIt.as<std::string>() + " " + portIt.as<std::string>());
            }
            else
            {
                // Generic map handling: key=value on separate lines
                processMapping(item, lines, j);
            }
        }
    }
}

}  // namespace

IniFileSections
yamlToIniFileSections(YAML::Node const& node, beast::Journal j)
{
    IniFileSections result;

    if (!node.IsMap())
    {
        JLOG(j.error()) << "YAML root must be a mapping";
        return result;
    }

    for (auto const& section : node)
    {
        auto const sectionName = section.first.as<std::string>();
        auto const& sectionValue = section.second;
        std::vector<std::string> lines;

        if (sectionValue.IsScalar())
        {
            // Single value section (e.g., database_path: "/path")
            lines.push_back(sectionValue.as<std::string>());
        }
        else if (sectionValue.IsSequence())
        {
            // List section (e.g., validators: [...])
            processSequence(sectionValue, lines, j);
        }
        else if (sectionValue.IsMap())
        {
            // Handle special case: server section with nested ports
            if (sectionName == "server" && sectionValue["ports"])
            {
                // Add port names to server section
                auto const& ports = sectionValue["ports"];
                for (auto const& portEntry : ports)
                {
                    lines.push_back(portEntry.first.as<std::string>());
                }
                result[sectionName] = std::move(lines);

                // Create separate sections for each port
                for (auto const& portEntry : ports)
                {
                    auto const portName = portEntry.first.as<std::string>();
                    auto const& portConfig = portEntry.second;
                    std::vector<std::string> portLines;

                    if (portConfig.IsMap())
                    {
                        processMapping(portConfig, portLines, j);
                    }
                    result[portName] = std::move(portLines);
                }

                // Also process any other keys in server section
                for (auto const& serverItem : sectionValue)
                {
                    auto const key = serverItem.first.as<std::string>();
                    if (key != "ports")
                    {
                        auto& serverLines = result[sectionName];
                        auto const& val = serverItem.second;
                        if (val.IsScalar())
                        {
                            serverLines.push_back(
                                key + "=" + val.as<std::string>());
                        }
                    }
                }
                continue;
            }

            // Regular mapping section (e.g., node_db: {type: "NuDB", ...})
            processMapping(sectionValue, lines, j);
        }

        result[sectionName] = std::move(lines);
    }

    return result;
}

bool
isYamlFile(boost::filesystem::path const& path)
{
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    return ext == ".yaml" || ext == ".yml";
}

}  // namespace xrpl
