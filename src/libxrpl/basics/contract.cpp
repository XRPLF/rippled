#include <xrpl/basics/Log.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace ripple {

void
LogThrow(std::string const& title)
{
    JLOG(debugLog().warn()) << title;
}

[[noreturn]] void
LogicError(std::string const& s) noexcept
{
    // LCOV_EXCL_START
    JLOG(debugLog().fatal()) << s;
    std::cerr << "Logic error: " << s << std::endl;
    // Use a non-standard contract naming here (without namespace) because
    // it's the only location where various unrelated execution paths may
    // register an error; this is also why the "message" parameter is passed
    // here.
    // For the above reasons, we want this contract to stand out.
    UNREACHABLE("LogicError", {{"message", s}});
    std::abort();
    // LCOV_EXCL_STOP
}

}  // namespace ripple
