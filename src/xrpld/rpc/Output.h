#pragma once

#include <boost/utility/string_ref.hpp>
#include <xrpl/basics/TraceLog.h>

namespace xrpl {
namespace RPC {

using Output = std::function<void(boost::string_ref const&)>;

inline Output
stringOutput(std::string& s)
{
    TRACE_FUNC();
    return [&](boost::string_ref const& b) { s.append(b.data(), b.size()); };
}

}  // namespace RPC
}  // namespace xrpl
