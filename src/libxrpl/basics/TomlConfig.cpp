#include <xrpl/basics/FileUtilities.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/TomlConfig.h>

#include <toml++/toml.hpp>

#include <algorithm>

namespace xrpl {

std::optional<toml::table>
parseTomlFile(boost::filesystem::path const& path, beast::Journal j)
{
    boost::system::error_code ec;
    auto const contents = getFileContents(ec, path);

    if (ec)
    {
        JLOG(j.error()) << "Failed to read TOML file '" << path.string() << "': " << ec.message();
        return std::nullopt;
    }

    return parseTomlString(contents, j);
}

std::optional<toml::table>
parseTomlString(std::string const& input, beast::Journal j)
{
    try
    {
        return toml::parse(input);
    }
    catch (toml::parse_error const& e)
    {
        JLOG(j.error()) << "TOML parse error at line " << e.source().begin.line << ", column "
                        << e.source().begin.column << ": " << e.description();
        return std::nullopt;
    }
}

namespace {

// Helper to process a TOML table into key=value lines
void
processTable(toml::table const& tbl, std::vector<std::string>& lines, beast::Journal j)
{
    for (auto const& [key, value] : tbl)
    {
        if (value.is_value())
        {
            std::ostringstream oss;
            oss << key << "=";
            if (auto* str = value.as_string())
                oss << str->get();
            else if (auto* i = value.as_integer())
                oss << i->get();
            else if (auto* f = value.as_floating_point())
                oss << f->get();
            else if (auto* b = value.as_boolean())
                oss << (b->get() ? "true" : "false");
            lines.push_back(oss.str());
        }
        else if (value.is_array())
        {
            // For arrays in a table, join values with comma
            auto const* arr = value.as_array();
            std::string combined;
            bool first = true;
            for (auto const& elem : *arr)
            {
                if (!first)
                    combined += ",";
                first = false;
                if (auto* str = elem.as_string())
                    combined += str->get();
                else if (auto* i = elem.as_integer())
                    combined += std::to_string(i->get());
            }
            lines.push_back(std::string(key) + "=" + combined);
        }
        else
        {
            JLOG(j.warn()) << "Skipping nested table for key '" << key << "'";
        }
    }
}

// Helper to process a TOML array into multiple lines
void
processArray(toml::array const& arr, std::vector<std::string>& lines, beast::Journal j)
{
    for (auto const& item : arr)
    {
        if (item.is_value())
        {
            if (auto* str = item.as_string())
                lines.push_back(str->get());
            else if (auto* i = item.as_integer())
                lines.push_back(std::to_string(i->get()));
            else if (auto* f = item.as_floating_point())
                lines.push_back(std::to_string(f->get()));
            else if (auto* b = item.as_boolean())
                lines.push_back(b->get() ? "true" : "false");
        }
        else if (item.is_table())
        {
            // For arrays of tables, check for host/port pattern
            auto const* tbl = item.as_table();
            auto hostIt = tbl->find("host");
            auto portIt = tbl->find("port");
            if (hostIt != tbl->end() && portIt != tbl->end())
            {
                std::string host, port;
                if (auto* s = hostIt->second.as_string())
                    host = s->get();
                if (auto* s = portIt->second.as_string())
                    port = s->get();
                else if (auto* i = portIt->second.as_integer())
                    port = std::to_string(i->get());
                lines.push_back(host + " " + port);
            }
            else
            {
                // Generic table handling: key=value on separate lines
                processTable(*tbl, lines, j);
            }
        }
        else if (item.is_array())
        {
            // Nested arrays are not supported in INI format
            JLOG(j.warn()) << "Skipping nested array in TOML config";
        }
    }
}

}  // namespace

IniFileSections
tomlToIniFileSections(toml::table const& tbl, beast::Journal j)
{
    IniFileSections result;

    for (auto const& [sectionName, sectionValue] : tbl)
    {
        std::vector<std::string> lines;

        if (sectionValue.is_value())
        {
            // Single value section (e.g., database_path = "/path")
            std::ostringstream oss;
            if (auto* str = sectionValue.as_string())
                oss << str->get();
            else if (auto* i = sectionValue.as_integer())
                oss << i->get();
            else if (auto* f = sectionValue.as_floating_point())
                oss << f->get();
            else if (auto* b = sectionValue.as_boolean())
                oss << (b->get() ? "true" : "false");
            lines.push_back(oss.str());
        }
        else if (sectionValue.is_array())
        {
            // List section (e.g., validators = [...])
            processArray(*sectionValue.as_array(), lines, j);
        }
        else if (sectionValue.is_table())
        {
            auto const* sectionTable = sectionValue.as_table();

            // Handle special case: server section with nested ports
            if (sectionName == "server" && sectionTable->contains("ports"))
            {
                auto const* ports = sectionTable->at("ports").as_table();
                if (ports)
                {
                    // Add port names to server section
                    for (auto const& [portName, portConfig] : *ports)
                    {
                        lines.push_back(std::string(portName));
                    }
                    result[std::string(sectionName)] = std::move(lines);

                    // Create separate sections for each port
                    for (auto const& [portName, portConfig] : *ports)
                    {
                        std::vector<std::string> portLines;
                        if (portConfig.is_table())
                        {
                            processTable(*portConfig.as_table(), portLines, j);
                        }
                        result[std::string(portName)] = std::move(portLines);
                    }

                    // Also process any other keys in server section
                    for (auto const& [key, val] : *sectionTable)
                    {
                        if (key != "ports" && val.is_value())
                        {
                            auto& serverLines = result[std::string(sectionName)];
                            std::ostringstream oss;
                            oss << key << "=";
                            if (auto* str = val.as_string())
                                oss << str->get();
                            else if (auto* i = val.as_integer())
                                oss << i->get();
                            serverLines.push_back(oss.str());
                        }
                    }
                    continue;
                }
            }

            // Regular table section (e.g., [node_db])
            processTable(*sectionTable, lines, j);
        }

        result[std::string(sectionName)] = std::move(lines);
    }

    return result;
}

bool
isTomlFile(boost::filesystem::path const& path)
{
    auto ext = path.extension().string();
    std::transform(
        ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext == ".toml";
}

}  // namespace xrpl
