#pragma once

#include <iosfwd>
#include <type_traits>

namespace xrpl {

enum class StartUpType { FRESH, NORMAL, LOAD, LoadFile, REPLAY, NETWORK };

inline std::ostream&
operator<<(std::ostream& os, StartUpType const& type)
{
    return os << static_cast<std::underlying_type_t<StartUpType>>(type);
}

}  // namespace xrpl
