#pragma once

#include <boost/utility/string_ref.hpp>

namespace xrpl {
namespace RPC {

using Output = std::function<void(boost::string_ref const&)>;

inline Output
stringOutput(std::string& s)
{
    return [&](boost::string_ref const& b) { s.append(b.data(), b.size()); };
}

}  // namespace RPC
}  // namespace xrpl
