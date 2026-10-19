#include <xrpl/tx/wasm/HostFailure.h>

#include <xrpl_host_functions.h>

#include <cstdint>
#include <ostream>
#include <variant>

namespace xrpl {

HostFailure::HostFailure(HostFunctionError error) : error_(error)
{
}

HostFailure::HostFailure(HostTrapError error) : error_(error)
{
}

std::int32_t
HostFailure::toInt() const
{
    return std::visit([](auto error) { return static_cast<std::int32_t>(error); }, error_);
}

std::ostream&
operator<<(std::ostream& os, HostFailure failure)
{
    return os << failure.toInt();
}

}  // namespace xrpl
