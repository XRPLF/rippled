#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/PathAsset.h>

namespace xrpl {

std::string
to_string(PathAsset const& asset)
{
    return std::visit([&](auto const& issue) { return to_string(issue); }, asset.value());
}

std::ostream&
operator<<(std::ostream& os, PathAsset const& x)
{
    os << to_string(x);
    return os;
}

}  // namespace xrpl
