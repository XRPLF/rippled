/** @file
 *  Runtime implementation of the XRPL programming-by-contract facility.
 *
 *  Provides `logThrow` (best-effort diagnostic logging before an exception
 *  propagates) and `logicError` (fatal termination on a broken invariant).
 *  The companion header `contract.h` builds `Throw<E>()` and `rethrow()` on
 *  top of these two functions.
 */

#include <xrpl/basics/contract.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace xrpl {

/** Emit a warning-level journal entry immediately before an exception propagates.
 *
 *  This is a best-effort diagnostic: `debugLog()` drains to a debug sink that
 *  may be a null sink when logging has not yet been configured, so the message
 *  may never be visible. It is not a reliable audit trail — callers that need
 *  the message preserved should use the exception object itself.
 *
 *  @param title Human-readable label identifying the throw site, typically
 *      including the exception type and `what()` string.
 */
void
logThrow(std::string const& title)
{
    JLOG(debugLog().warn()) << title;
}

/** Terminate the process after logging a broken invariant.
 *
 *  Called at sites where an "impossible" condition has been detected and
 *  continuing would risk data corruption or incorrect ledger state. Never
 *  call this on error paths that are expected to trigger under normal load.
 *
 *  The message is written to both the XRPL journal (fatal level) and
 *  `std::cerr`. The dual output ensures visibility even if the journal
 *  subsystem is uninitialised or itself corrupt.
 *
 *  In debug builds `UNREACHABLE` fires an assert, giving debuggers a clean
 *  crash point. In release builds `std::abort()` on the following line
 *  provides a guaranteed `SIGABRT` that crash-reporting infrastructure can
 *  capture. The function body is excluded from LCOV coverage because these
 *  lines are unreachable in any test that does not instrument process abort.
 *
 *  @param s Message describing the violated invariant.
 *  @note Marked `noexcept` to signal to callers and the compiler that this
 *      path never propagates an exception — it only terminates.
 */
[[noreturn]] void
logicError(std::string const& s) noexcept
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
