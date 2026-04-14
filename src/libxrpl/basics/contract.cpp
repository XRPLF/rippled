#include <xrpl/basics/Log.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>

#ifndef BOOST_STACKTRACE_GNU_SOURCE_NOT_REQUIRED
#define BOOST_STACKTRACE_GNU_SOURCE_NOT_REQUIRED
#endif
#include <boost/stacktrace.hpp>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace xrpl {

void
LogThrow(std::string const& title)
{
    std::ostringstream oss;
    oss << title << '\n' << boost::stacktrace::stacktrace();
    JLOG(debugLog().warn()) << oss.str();
    // Also mirror to stderr so uncaught exceptions leave a trace even when
    // log output is buffered/lost before terminate().
    std::cerr << oss.str() << std::endl;
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

}  // namespace xrpl
