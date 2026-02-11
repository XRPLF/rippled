#pragma once

#include <xrpl/basics/BasicConfig.h>
#include <xrpl/beast/utility/Journal.h>

#include <boost/filesystem.hpp>

#include <optional>
#include <string>

namespace toml {
inline namespace v3 {
class table;
}
}  // namespace toml

namespace xrpl {

/**
 * Parse a TOML configuration file.
 *
 * @param path The path to the TOML file to parse.
 * @param j Journal for logging errors.
 * @return The parsed TOML table, or std::nullopt on error.
 */
std::optional<toml::table>
parseTomlFile(boost::filesystem::path const& path, beast::Journal j);

/**
 * Parse a TOML configuration string.
 *
 * @param input The TOML content as a string.
 * @param j Journal for logging errors.
 * @return The parsed TOML table, or std::nullopt on error.
 */
std::optional<toml::table>
parseTomlString(std::string const& input, beast::Journal j);

/**
 * Convert a toml::table to IniFileSections format.
 *
 * This function transforms a TOML document into the equivalent IniFileSections
 * structure that can be used by BasicConfig::build(). The TOML schema follows
 * these mapping rules:
 *
 * 1. Top-level TOML keys correspond to INI section names.
 * 2. Scalar sections (single value) become a single line in the section.
 * 3. Table sections become key=value pairs in the section.
 * 4. Array sections become multiple lines in the section.
 * 5. The special "server.ports" nested structure is flattened into separate
 *    port sections.
 * 6. Arrays of tables with "host" and "port" keys are converted to
 *    "host port" format for compatibility with the ips section format.
 *    Example: [[ips]] host = "r.ripple.com" port = 51235 becomes
 *    "r.ripple.com 51235"
 *
 * @param table The TOML table to convert.
 * @param j Journal for logging errors.
 * @return The equivalent IniFileSections structure.
 */
IniFileSections
tomlToIniFileSections(toml::table const& table, beast::Journal j);

/**
 * Check if a file path has a TOML extension.
 *
 * @param path The file path to check.
 * @return true if the path ends with .toml, false otherwise.
 */
bool
isTomlFile(boost::filesystem::path const& path);

}  // namespace xrpl
