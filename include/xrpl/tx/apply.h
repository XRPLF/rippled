#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/tx/applySteps.h>

#include <utility>

namespace xrpl {

class HashRouter;
class ServiceRegistry;

/** Describes the pre-processing validity of a transaction.
 *
 *  The three levels form a strict hierarchy: `SigBad < SigGoodOnly < Valid`.
 *  Local checks are only worth performing when the signature is good, so
 *  `SigGoodOnly` implies the signature passed but local checks failed.
 *  This hierarchy maps directly to P2P relay semantics: `SigBad` transactions
 *  are not forwarded; `SigGoodOnly` transactions are relayed but not applied;
 *  `Valid` transactions are both relayed and applied.
 *
 *  @see checkValidity, forceValidity
 */
enum class Validity {
    /// Signature is invalid. Local checks were not attempted.
    SigBad,
    /// Signature is valid, but local checks failed.
    SigGoodOnly,
    /// Signature is valid and local checks passed.
    Valid
};

/** Check a transaction's cryptographic signature and local well-formedness.
 *
 *  Results are cached in the `HashRouter` using four private flag bits
 *  (`PRIVATE1`–`PRIVATE4`). Subsequent calls for the same transaction ID
 *  return immediately from the cache rather than re-verifying the signature.
 *  The cache inherits its TTL from the `HashRouter`'s aged map.
 *
 *  Batch inner transactions (flagged `tfInnerBatchTxn`) follow a separate
 *  code path: they must have no signature fields, and after `fixBatchInnerSigs`
 *  activates they are permanently treated as never-valid to prevent erroneous
 *  `SF_SIGGOOD` cache entries on unsigned objects.
 *
 *  @param router The hash router used to cache validity flags.
 *  @param tx The transaction to check.
 *  @param rules The current ledger rules (used for amendment-gated logic).
 *  @return A pair whose `.first` is the `Validity` status and whose `.second`
 *      is a human-readable reason string when the transaction is not `Valid`
 *      (empty on success).
 *
 *  @see Validity, forceValidity
 */
std::pair<Validity, std::string>
checkValidity(HashRouter& router, STTx const& tx, Rules const& rules);

/** Assert a specific validity level for a transaction in the hash-router cache.
 *
 *  Uses a deliberate `[[fallthrough]]` switch to enforce monotonicity: setting
 *  `Valid` also sets the `SigGoodOnly` flag, because local checks cannot pass
 *  without a valid signature. This can only raise the cached state — it never
 *  marks a transaction as `SigBad`, so calling with `SigBad` is a no-op.
 *
 *  The primary use case is for locally-constructed transactions that were never
 *  signed by a remote peer; the transaction queue can mark them pre-verified to
 *  avoid redundant signature checks on re-application.
 *
 *  @param router The hash router that holds the cached flags.
 *  @param txid The transaction ID whose cached validity to update.
 *  @param validity The minimum validity level to assert. Passing `SigBad`
 *      has no effect.
 *
 *  @warning Calling this bypasses real cryptographic verification. Only use
 *      when you have an out-of-band guarantee that the transaction is valid
 *      (e.g., a transaction you constructed locally and submitted yourself).
 *
 *  @see checkValidity, Validity
 */
void
forceValidity(HashRouter& router, uint256 const& txid, Validity validity);

/** Apply a transaction to an `OpenView`, running all three pipeline stages.
 *
 *  Convenience wrapper that composes `preflight → preclaim → doApply` into a
 *  single call. The `preflight` result can be safely cached and reused across
 *  threads, but `preclaim` and `doApply` must run on the same thread and with
 *  the same view.
 *
 *  This function does not throw. Exceptions inside a `Transactor` are caught
 *  and converted to `tefEXCEPTION`. For closed ledgers, if full application
 *  fails the `Transactor` will attempt a best-effort fee deduction and return
 *  `tecFAILED_PROCESSING`; if even the fee-deduction path throws, that
 *  exception is also caught and returned as `tefEXCEPTION`. This best-effort
 *  fee guarantee prevents fee-free spam vectors during consensus.
 *
 *  @param registry The service registry providing transactor implementations.
 *  @param view The open ledger to which the transaction will be applied.
 *  @param tx The transaction to apply.
 *  @param flags `ApplyFlags` controlling processing options (e.g., `tapRETRY`,
 *      `tapDRY_RUN`).
 *  @param journal Logging sink.
 *  @return An `ApplyResult` whose `.ter` is the transaction result code and
 *      whose `.applied` is `true` if the transaction's mutations were committed
 *      to `view`.
 *
 *  @see preflight, preclaim, doApply, applyTransaction
 */
ApplyResult
apply(
    ServiceRegistry& registry,
    OpenView& view,
    STTx const& tx,
    ApplyFlags flags,
    beast::Journal journal);

/** Outcome classification returned by `applyTransaction`.
 *
 *  Wraps the raw `TER` code from `apply()` into the three-way decision the
 *  transaction queue needs: commit, evict, or hold for a retry pass.
 *
 *  @see applyTransaction
 */
enum class ApplyTransactionResult {
    /// Transaction was applied and its mutations committed to the ledger view.
    Success,
    /// Terminal failure — do not retry in this ledger.
    /// Covers `tef*` (internal failures), `tem*` (malformed), and `tel*`
    /// (local-node rejections).
    Fail,
    /// Soft failure — the transaction may succeed in a later pass or ledger.
    /// Covers all other non-applied results (e.g., `ter*`, `tec*` with
    /// `tapRETRY`).
    Retry
};

/** Apply a transaction and classify the outcome for the transaction queue.
 *
 *  Calls `apply()` and maps its `TER` result to `ApplyTransactionResult`:
 *  - `tefFailure`, `temMalformed`, `telLocal` → `Fail` (evict, no retry)
 *  - Any other non-applied result → `Retry` (hold for later)
 *  - Applied result → `Success`
 *
 *  When `retryAssured` is `true`, `tapRETRY` is added to `flags` before
 *  calling `apply()`. With `tapRETRY` set, `tec` results are treated as soft
 *  failures rather than hard fee-claims; this affects `preclaim`'s
 *  `likelyToClaimFee` signal and determines whether the transaction is safe
 *  to relay without first applying it to the open ledger.
 *
 *  For `ttBATCH` transactions that succeed, inner transactions are applied in
 *  a nested `OpenView` sandbox. Inner-transaction changes are committed to the
 *  main view only if the batch as a whole succeeds under its execution policy
 *  (`tfAllOrNothing`, `tfUntilFailure`, `tfOnlyOne`, `tfIndependent`).
 *
 *  Exceptions from `apply()` are caught and returned as `Fail`.
 *
 *  @param registry The service registry providing transactor implementations.
 *  @param view The open ledger to which the transaction will be applied.
 *  @param tx The transaction to apply.
 *  @param retryAssured If `true`, adds `tapRETRY` to `flags` so that `tec`
 *      results are treated as retryable soft failures rather than fee claims.
 *  @param flags Base `ApplyFlags` controlling processing options.
 *  @param journal Logging sink.
 *  @return An `ApplyTransactionResult` indicating success, terminal failure,
 *      or retryable failure.
 *
 *  @see apply, ApplyTransactionResult
 */
ApplyTransactionResult
applyTransaction(
    ServiceRegistry& registry,
    OpenView& view,
    STTx const& tx,
    bool retryAssured,
    ApplyFlags flags,
    beast::Journal journal);

}  // namespace xrpl
