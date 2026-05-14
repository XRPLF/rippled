/** @file
 *  Implements `doLogRotate`, the handler for the `logrotate` admin RPC command.
 *
 *  Rotating the log file allows external tools such as `logrotate(8)` to rename
 *  or truncate the on-disk file while the process keeps running; the handler
 *  closes and reopens the file descriptor so subsequent writes go to the new
 *  path.  The companion command `log_level` (implemented in `LogLevel.cpp`)
 *  completes the set of runtime log-management primitives.
 */

#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/Handler.h>

#include <xrpl/basics/Log.h>
#include <xrpl/core/PerfLog.h>
#include <xrpl/json/json_value.h>

namespace xrpl {

/** Rotate both the performance log and the application log (ADMIN).
 *
 *  Closes and reopens the application log file at its configured path, which
 *  is the standard interop pattern for `logrotate(8)`: the external tool renames
 *  or truncates the file first, then signals the process to reopen it.  The
 *  performance log is rotated via `PerfLog::rotate()` beforehand.
 *
 *  No request parameters are consumed; the handler delegates entirely to the
 *  two logging subsystems and returns their status message.
 *
 *  @param context  RPC dispatch context carrying the `app` reference.
 *  @return A JSON object whose value is the status string returned by
 *      `Logs::rotate()` (e.g., `"The log file has been rotated"`).
 *
 *  @note Access is enforced by the RPC framework before this function is
 *      called; the handler itself performs no role or condition checks.
 */
json::Value
doLogRotate(RPC::JsonContext& context)
{
    context.app.getPerfLog().rotate();
    return RPC::makeObjectValue(context.app.getLogs().rotate());
}

}  // namespace xrpl
