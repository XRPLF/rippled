#pragma once

#include <xrpl/json/json_writer.h>

#include <string>

namespace Json {

/** Writes a Json::Value to an std::string. */
std::string
to_string(Value const&);

/** Writes a Json::Value to an std::string. */
std::string
pretty(Value const&);

}  // namespace Json
