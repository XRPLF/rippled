#pragma once

#include <xrpl/json/json_writer.h>

#include <string>

namespace json {

/** Writes a json::Value to an std::string. */
std::string
to_string(Value const&);

/** Writes a json::Value to an std::string. */
std::string
pretty(Value const&);

}  // namespace json
