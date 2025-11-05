#ifndef XRPL_RPC_OUTPUT_H_INCLUDED
#define XRPL_RPC_OUTPUT_H_INCLUDED

#include <boost/utility/string_ref.hpp>

namespace ripple {
namespace RPC {

using Output = std::function<void(boost::string_ref const&)>;

inline Output
stringOutput(std::string& s)
{
    return [&](boost::string_ref const& b) { s.append(b.data(), b.size()); };
}

}  // namespace RPC
}  // namespace ripple

#endif
