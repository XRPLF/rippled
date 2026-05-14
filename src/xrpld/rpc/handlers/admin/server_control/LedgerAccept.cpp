#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/core/Config.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>

#include <mutex>

namespace xrpl {

/** Implements the `ledger_accept` admin RPC command.
 *
 *  Forces the node to close the current open ledger and advance to the next
 *  one without waiting for peer votes or the normal consensus timer. This is
 *  only valid in standalone mode, where no validator peers participate and
 *  ledger progression must be triggered on demand — for example, by the
 *  `jtx` integration-test harness between transaction submissions.
 *
 *  If the node is not in standalone mode the response contains
 *  `{"error": "notStandAlone"}` and no state is mutated.
 *
 *  When standalone mode is confirmed the handler acquires
 *  `Application::getMasterMutex()` (a recursive mutex that serialises all
 *  open-ledger and consensus-engine mutations) before delegating to
 *  `NetworkOPs::acceptLedger()`. That method drives a synthetic consensus
 *  round via `mConsensus.simulate()`, bypassing real peer participation and
 *  producing a fully validated ledger as if consensus had concluded normally,
 *  including fee adjustments and any pending open-ledger transactions.
 *
 *  The response on success contains a single field `ledger_current_index`
 *  holding the sequence number of the ledger now open for new transactions
 *  (one past the ledger just closed). Test frameworks use this value to
 *  anchor subsequent state queries with confidence that the triggered
 *  transactions have been processed.
 *
 *  @note This handler accepts no JSON parameters beyond the implicit RPC
 *      envelope; validation here is purely a mode-enforcement check rather
 *      than input sanitisation.
 *  @note Registered in Handler.cpp with `Role::ADMIN` and
 *      `Condition::NeedsCurrentLedger`, so it is unreachable for
 *      non-admin connections and when the ledger is too stale.
 *
 *  @param context  The RPC dispatch context providing app services, config,
 *      `netOps`, and `ledgerMaster`.
 *  @return A JSON object with `ledger_current_index` on success, or
 *      `{"error": "notStandAlone"}` when not in standalone mode.
 */
json::Value
doLedgerAccept(RPC::JsonContext& context)
{
    json::Value jvResult;

    if (!context.app.config().standalone())
    {
        jvResult[jss::error] = "notStandAlone";
    }
    else
    {
        std::unique_lock const lock{context.app.getMasterMutex()};
        context.netOps.acceptLedger();
        jvResult[jss::ledger_current_index] = context.ledgerMaster.getCurrentLedgerIndex();
    }

    return jvResult;
}

}  // namespace xrpl
