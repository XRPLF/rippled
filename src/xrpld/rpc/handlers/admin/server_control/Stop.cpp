#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/detail/Handler.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SystemParameters.h>

namespace xrpl {

namespace RPC {
struct JsonContext;
}  // namespace RPC

/** Implements the `stop` admin RPC command.
 *
 *  Signals the application to begin a graceful shutdown, then immediately
 *  returns a confirmation message to the caller. The shutdown itself proceeds
 *  asynchronously after this handler returns — the response is queued for
 *  delivery before teardown begins, so the caller reliably receives
 *  `"<name> server stopping"` even though the server is about to exit.
 *
 *  The delegate, `Application::signalStop("RPC")`, sets a C++20
 *  `std::atomic_flag` exactly once (idempotent under concurrent callers) and
 *  unblocks the `run()` loop. The `"RPC"` reason string is written to the
 *  warning log, letting operators distinguish an administrative RPC shutdown
 *  from a SIGTERM or an internal fault condition.
 *
 *  Permission enforcement is handled entirely by the dispatch layer:
 *  the handler is registered with `Role::ADMIN` and `NO_CONDITION`, so it is
 *  unreachable for non-admin connections and does not require the server to
 *  hold a current ledger — a node that is still syncing can be stopped via
 *  RPC just as readily as a fully-synced one.
 *
 *  @param context  The RPC dispatch context providing access to the
 *      `Application` instance whose `signalStop()` is invoked.
 *  @return A JSON object with a `"message"` field containing
 *      `"<systemName> server stopping"`.
 */
json::Value
doStop(RPC::JsonContext& context)
{
    context.app.signalStop("RPC");
    return RPC::makeObjectValue(systemName() + " server stopping");
}

}  // namespace xrpl
