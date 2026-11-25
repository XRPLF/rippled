#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace json {

using Output = std::function<void(std::string_view)>;

inline Output
stringOutput(std::string& s)
{
    return [&](std::string_view b) { s.append(b.data(), b.size()); };
}

}  // namespace json
