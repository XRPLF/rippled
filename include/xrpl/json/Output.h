#pragma once

#include <boost/beast/core/string.hpp>

#include <functional>
#include <string>

namespace json {

class Value;

using Output = std::function<void(boost::beast::string_view const&)>;

inline Output
stringOutput(std::string& s)
{
    return [&](boost::beast::string_view const& b) { s.append(b.data(), b.size()); };
}

/** Writes a minimal representation of a Json value to an Output in O(n) time.

    Data is streamed right to the output, so only a marginal amount of memory is
    used.  This can be very important for a very large json::Value.
 */
void
outputJson(json::Value const&, Output const&);

/** Return the minimal string representation of a json::Value in O(n) time.

    This requires a memory allocation for the full size of the output.
    If possible, use outputJson().
 */
std::string
jsonAsString(json::Value const&);

}  // namespace json
