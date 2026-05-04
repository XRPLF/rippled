#pragma once

#include <string>
#include <type_traits>

namespace xrpl {

/** to_string() generalizes std::to_string to handle bools, chars, and strings.

    It's also possible to provide implementation of to_string for a class
    which needs a string implementation.
 */

template <class T>
std::enable_if_t<std::is_arithmetic_v<T>, std::string>
to_string(T t)  // NOLINT(readability-identifier-naming)
{
    return std::to_string(t);
}

inline std::string
to_string(bool b)  // NOLINT(readability-identifier-naming)
{
    return b ? "true" : "false";
}

inline std::string
to_string(char c)  // NOLINT(readability-identifier-naming)
{
    return std::string(1, c);
}

inline std::string
to_string(std::string s)  // NOLINT(readability-identifier-naming)
{
    return s;
}

inline std::string
to_string(char const* s)  // NOLINT(readability-identifier-naming)
{
    return s;
}

}  // namespace xrpl
