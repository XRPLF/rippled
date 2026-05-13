/** @file
 *  Implements `ApplyContext`, the sandboxed ledger view and invariant
 *  enforcement layer for transaction execution.
 *
 *  `ApplyContext` owns the `ApplyViewImpl` sandbox that a `Transactor` writes
 *  into during `doApply`. No mutation escapes to the live `OpenView` until
 *  `apply()` is called. When `doApply` returns, `checkInvariants` runs every
 *  registered checker against the pending changes; a failure triggers rollback
 *  and escalates the result to `tec`/`tef INVARIANT_FAILED` depending on
 *  whether a prior retry has already been attempted.
 */
#include <xrpl/tx/ApplyContext.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/invariants/InvariantCheck.h>

#include <array>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>

namespace xrpl {

/** Construct an `ApplyContext` for a batch-inner or standalone transaction.
 *
 *  Emplaces a fresh `ApplyViewImpl` sandbox on top of `base`. All transactor
 *  reads and writes go through this sandbox; `base` is not touched until
 *  `apply()` is called.
 *
 *  @param registry Service registry available to transactors.
 *  @param base The live open ledger view to sandbox against.
 *  @param parentBatchId ID of the enclosing batch transaction, or `nullopt`
 *      for non-batch transactions. Must be present if and only if `TapBatch`
 *      is set in `flags`.
 *  @param tx The pre-validated transaction to apply.
 *  @param preclaimResult TER returned by the preclaim phase.
 *  @param baseFee The base fee for this transaction type.
 *  @param flags Apply-phase flags (e.g., `TapDryRun`, `TapBatch`).
 *  @param journal Logging sink.
 *  @note Asserts that `parentBatchId.has_value() == (flags & TapBatch)`.
 *      Constructing with a mismatched batch ID / flag combination is a
 *      programming error and will fire in debug builds.
 */
ApplyContext::ApplyContext(
    ServiceRegistry& registry,
    OpenView& base,
    std::optional<uint256 const> const& parentBatchId,
    STTx const& tx,
    TER preclaimResult,
    XRPAmount baseFee,
    ApplyFlags flags,
    beast::Journal journal)
    : registry(registry)
    , tx(tx)
    , preclaimResult(preclaimResult)
    , baseFee(baseFee)
    , journal(journal)
    , base_(base)
    , flags_(flags)
    , parentBatchId_(parentBatchId)
{
    XRPL_ASSERT(
        parentBatchId.has_value() == ((flags_ & TapBatch) == TapBatch),
        "Parent Batch ID should be set if batch apply flag is set");
    view_.emplace(&base_, flags_);
}

/** Discard all pending sandbox changes and reset to a clean view over `base_`.
 *
 *  Re-emplaces a fresh `ApplyViewImpl` on top of `base_`, atomically
 *  discarding every ledger entry modification accumulated since construction
 *  or the previous `discard()`. Because the sandbox has never written through
 *  to `base_`, this rollback costs only object construction.
 *
 *  Called by `Transactor::reset()` when re-applying a fee-only path after an
 *  invariant failure, and by `Transactor::operator()` on the `tapFAIL_HARD`
 *  branch to suppress even fee collection.
 */
void
ApplyContext::discard()
{
    view_.emplace(&base_, flags_);
}

/** Commit the sandbox to the live ledger view and generate transaction metadata.
 *
 *  Flushes all pending `ApplyViewImpl` deltas into `base_` by delegating to
 *  `view_->apply()`. Passes `parentBatchId_` and the `TapDryRun` flag so the
 *  commit step remains aware of batch context and simulation mode without
 *  requiring the transactor to track those concerns.
 *
 *  @param ter The final transaction result code determined by the transactor.
 *  @return Transaction metadata describing ledger changes, or `nullopt` if
 *      the transaction was a dry run (`TapDryRun`) and no metadata is
 *      produced.
 *  @note Must not be called after `discard()` without re-running the
 *      transaction; calling on a fresh sandbox produces empty metadata.
 */
std::optional<TxMeta>
ApplyContext::apply(TER ter)
{
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) view_ emplaced in constructor
    return view_->apply(base_, tx, ter, parentBatchId_, (flags_ & TapDryRun) != 0u, journal);
}

/** Return the number of ledger entry modifications pending in the sandbox.
 *
 *  Delegates to `ApplyViewImpl::size()`. Used by `Transactor::operator()` to
 *  check whether the transaction metadata would exceed `oversizeMetaDataCap`
 *  before committing.
 *
 *  @return Count of modified ledger entries not yet committed to `base_`.
 */
std::size_t
ApplyContext::size()
{
    return view_->size();  // NOLINT(bugprone-unchecked-optional-access)
}

/** Enumerate every pending ledger entry modification in the sandbox.
 *
 *  Calls `func` once for each modified entry, providing the entry's key, a
 *  deletion flag, and shared pointers to the before- and after-states.
 *  Used internally by `checkInvariantsHelper` to feed every registered
 *  invariant checker via `visitEntry`.
 *
 *  @param func Visitor invoked as
 *      `func(key, isDelete, before, after)` for each modified entry.
 *      `before` is null for newly created entries; `after` is null for
 *      deleted entries.
 */
void
ApplyContext::visit(
    std::function<void(
        uint256 const&,
        bool,
        std::shared_ptr<SLE const> const&,
        std::shared_ptr<SLE const> const&)> const& func)
{
    view_->visit(base_, func);  // NOLINT(bugprone-unchecked-optional-access)
}

/** Escalate an invariant failure to the appropriate TER code.
 *
 *  On a first-time invariant failure the caller receives `tecINVARIANT_FAILED`,
 *  which is included in the ledger so the sender is still charged a fee.
 *  If the incoming `result` is already `tecINVARIANT_FAILED` or
 *  `tefINVARIANT_FAILED` — meaning the caller is on a fee-only retry and even
 *  that minimal path broke an invariant — this function escalates to
 *  `tefINVARIANT_FAILED`, which is NOT included in any ledger. Nothing is
 *  committed when the result is `tef`.
 *
 *  @param result The TER that was in effect when the invariant failure occurred.
 *  @return `tefINVARIANT_FAILED` if `result` signals a prior invariant failure;
 *      `tecINVARIANT_FAILED` otherwise.
 */
TER
ApplyContext::failInvariantCheck(TER const result)
{
    // If we already failed invariant checks before and we are now attempting to
    // only charge a fee, and even that fails the invariant checks something is
    // very wrong. We switch to tefINVARIANT_FAILED, which does NOT get included
    // in a ledger.

    return (result == tecINVARIANT_FAILED || result == tefINVARIANT_FAILED)
        ? TER{tefINVARIANT_FAILED}
        : TER{tecINVARIANT_FAILED};
}

/** Drive all registered invariant checkers against the current sandbox state.
 *
 *  Uses compile-time index unpacking to avoid virtual dispatch. Execution has
 *  two phases for each checker in `InvariantChecks`:
 *
 *  1. **visitEntry** — called once per modified ledger entry via `visit()`.
 *     Each checker accumulates per-entry state (e.g., running XRP drop totals,
 *     account deletion counts).
 *  2. **finalize** — called after all entries have been visited. Results are
 *     collected into a `std::array<bool>` (NOT a `...&&` fold) so every
 *     failing checker logs its own diagnostic before the verdict is rendered.
 *     Short-circuiting with `&&` would silence all but the first failure,
 *     which is unacceptable during incident diagnosis.
 *
 *  Checkers run even when `result` is a `tec*` failure code; a bug or exploit
 *  could mutate ledger state on a failed transaction, and invariants are the
 *  last line of defense against that.
 *
 *  Any exception thrown by a checker is treated as an invariant failure and
 *  routed through `failInvariantCheck`.
 *
 *  @tparam Is Index pack over `InvariantChecks` tuple positions.
 *  @param result The transaction result code to validate against.
 *  @param fee The fee actually charged for the transaction.
 *  @param Index sequence used to unpack the checker tuple at compile time.
 *  @return `result` unchanged if all checkers pass; `failInvariantCheck(result)`
 *      if any checker fails or throws.
 */
template <std::size_t... Is>
TER
ApplyContext::checkInvariantsHelper(
    TER const result,
    XRPAmount const fee,
    std::index_sequence<Is...>)
{
    try
    {
        auto checkers = getInvariantChecks();

        visit([&checkers](
                  uint256 const& index,
                  bool isDelete,
                  std::shared_ptr<SLE const> const& before,
                  std::shared_ptr<SLE const> const& after) {
            (..., std::get<Is>(checkers).visitEntry(isDelete, before, after));
        });

        std::array<bool, sizeof...(Is)> const finalizers{{std::get<Is>(checkers).finalize(
            tx, result, fee, *view_, journal)...}};  // NOLINT(bugprone-unchecked-optional-access)

        if (!std::all_of(finalizers.cbegin(), finalizers.cend(), [](auto const& b) { return b; }))
        {
            JLOG(journal.fatal()) << "Transaction has failed one or more global invariants: "
                                  << to_string(tx.getJson(JsonOptions::Values::None));

            return failInvariantCheck(result);
        }
    }
    catch (std::exception const& ex)
    {
        JLOG(journal.fatal()) << "Transaction caused an exception in a global invariant"
                              << ", ex: " << ex.what()
                              << ", tx: " << to_string(tx.getJson(JsonOptions::Values::None));

        return failInvariantCheck(result);
    }

    return result;
}

TER
ApplyContext::checkInvariants(TER const result, XRPAmount const fee)
{
    XRPL_ASSERT(
        isTesSuccess(result) || isTecClaim(result),
        "xrpl::ApplyContext::checkInvariants : is tesSUCCESS or tecCLAIM");

    return checkInvariantsHelper(
        result, fee, std::make_index_sequence<std::tuple_size_v<InvariantChecks>>{});
}

}  // namespace xrpl
