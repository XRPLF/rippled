#include <xrpl/protocol/PathAsset.h>
#include <xrpl/basics/TraceLog.h>

#include <ostream>
#include <string>
#include <variant>

namespace xrpl {

std::string
to_string(PathAsset const& asset)
{
    TRACE_FUNC();
    return std::visit([&](auto const& issue) { return to_string(issue); }, asset.value());
}

std::ostream&
operator<<(std::ostream& os, PathAsset const& x)
{
    TRACE_FUNC();
    os << to_string(x);
    return os;
}

}  // namespace xrpl
