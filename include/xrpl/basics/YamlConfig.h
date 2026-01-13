#ifndef XRPL_BASICS_YAMLCONFIG_H_INCLUDED
#define XRPL_BASICS_YAMLCONFIG_H_INCLUDED

#include <xrpl/basics/BasicConfig.h>
#include <xrpl/beast/utility/Journal.h>

#include <boost/filesystem.hpp>

#include <optional>
#include <string>

namespace YAML {
class Node;
}

namespace xrpl {

/**
 * Parse a YAML configuration file.
 *
 * @param path The path to the YAML file to parse.
 * @param j Journal for logging errors.
 * @return The parsed YAML node, or std::nullopt on error.
 */
std::optional<YAML::Node>
parseYamlFile(boost::filesystem::path const& path, beast::Journal j);

/**
 * Parse a YAML configuration string.
 *
 * @param input The YAML content as a string.
 * @param j Journal for logging errors.
 * @return The parsed YAML node, or std::nullopt on error.
 */
std::optional<YAML::Node>
parseYamlString(std::string const& input, beast::Journal j);

/**
 * Convert a YAML::Node to IniFileSections format.
 *
 * This function transforms a YAML document into the equivalent IniFileSections
 * structure that can be used by BasicConfig::build(). The YAML schema follows
 * these mapping rules:
 *
 * 1. Top-level YAML keys correspond to INI section names.
 * 2. Null sections become empty sections.
 * 3. Scalar sections (single value) become a single line in the section.
 * 4. Mapping sections become key=value pairs in the section.
 * 5. Sequence sections become multiple lines in the section.
 * 6. The special "server.ports" nested structure is flattened into separate
 *    port sections.
 * 7. Sequences of maps with "host" and "port" keys are converted to
 *    "host port" format for compatibility with the ips section format.
 *    Example: [{host: "r.ripple.com", port: 51235}] becomes "r.ripple.com
 * 51235"
 *
 * @param node The YAML node to convert.
 * @param j Journal for logging errors.
 * @return The equivalent IniFileSections structure.
 */
IniFileSections
yamlToIniFileSections(YAML::Node const& node, beast::Journal j);

/**
 * Check if a file path has a YAML extension.
 *
 * @param path The file path to check.
 * @return true if the path ends with .yaml or .yml, false otherwise.
 */
bool
isYamlFile(boost::filesystem::path const& path);

}  // namespace xrpl

#endif
