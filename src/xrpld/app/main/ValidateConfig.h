#pragma once

#include <iosfwd>

namespace xrpl {

class Config;

/**
 * Validate startup settings without creating an Application or opening databases
 * or network listeners. Throws on invalid configuration.
 */
void
validateConfig(Config& config, std::ostream& errors);

}  // namespace xrpl
