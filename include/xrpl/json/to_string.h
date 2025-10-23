#ifndef XRPL_JSON_TO_STRING_H_INCLUDED
#define XRPL_JSON_TO_STRING_H_INCLUDED

#include <ostream>
#include <string>

namespace Json {

class Value;

/** Writes a Json::Value to an std::string. */
std::string
to_string(Value const&);

/** Writes a Json::Value to an std::string. */
std::string
pretty(Value const&);

/** Output using the StyledStreamWriter. @see Json::operator>>(). */
std::ostream&
operator<<(std::ostream&, Value const& root);

}  // namespace Json

#endif  // JSON_TO_STRING_H_INCLUDED
