/** @file
 *  Public interface for the RPC transaction signing and submission pipeline.
 *
 *  Declares the six functions and one type alias that together implement the
 *  `sign`, `submit`, `sign_for`, and `submit_multisigned` RPC methods. This
 *  header is the boundary between the JSON-speaking RPC handler layer and the
 *  signing/submission machinery; all heavy lifting lives in the companion
 *  `TransactionSign.cpp`.
 */
#pragma once

#include <xrpld/core/Config.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/server/LoadFeeTrack.h>
#include <xrpl/server/NetworkOPs.h>

namespace xrpl {

// Forward declarations
class Application;
class LoadFeeTrack;
class Transaction;
class TxQ;

namespace RPC {

/** Compute the recommended fee for a transaction against the current open ledger.
 *
 *  Returns the maximum of two independent estimates:
 *  - The load-scaled base fee from `LoadFeeTrack` (rises under heavy CPU/IO load).
 *  - The open-ledger escalated fee from `TxQ` (rises as the queue fills).
 *
 *  The result is capped at `baseFee * mult / div`. If the computed fee exceeds
 *  that ceiling, an error object with `rpcHIGH_FEE` is returned instead.
 *  Administrative and identified roles (`isUnlimited(role) == true`) are exempt
 *  from the load-scaling component and pay only the queue-escalated fee.
 *
 *  @param role      Request role; unlimited roles bypass load-fee scaling.
 *  @param config    Node configuration.
 *  @param feeTrack  Current load fee tracker.
 *  @param txQ       Transaction queue, used to read open-ledger fee metrics.
 *  @param app       Application reference.
 *  @param tx        The `tx_json` object; used to determine the transaction's
 *      base fee via `getTxFee`.
 *  @param mult      Ceiling multiplier; defaults to
 *      `Tuning::kDEFAULT_AUTO_FILL_FEE_MULTIPLIER` (10).
 *  @param div       Ceiling divisor; defaults to
 *      `Tuning::kDEFAULT_AUTO_FILL_FEE_DIVISOR` (1).
 *  @return A `json::Value` containing the fee in drops as a clipped integer,
 *      or an error object with `rpcHIGH_FEE` if the ceiling is breached.
 *  @throws std::overflow_error if the ceiling computation overflows.
 */
json::Value
getCurrentNetworkFee(
    Role const role,
    Config const& config,
    LoadFeeTrack const& feeTrack,
    TxQ const& txQ,
    Application const& app,
    json::Value const& tx,
    int mult = Tuning::kDEFAULT_AUTO_FILL_FEE_MULTIPLIER,
    int div = Tuning::kDEFAULT_AUTO_FILL_FEE_DIVISOR);

/** Ensure a transaction's `Fee` field is present, auto-filling it if permitted.
 *
 *  If `Fee` is already set in `request["tx_json"]`, returns immediately with
 *  an empty JSON value (success). If `Fee` is absent and `doAutoFill` is false,
 *  returns a missing-field error. Otherwise reads the optional `fee_mult_max`
 *  and `fee_div_max` fields from `request` and delegates to
 *  `getCurrentNetworkFee` to compute and inject the fee.
 *
 *  The default ceiling is `baseFee * 10 / 1` — the server will auto-fill any
 *  fee up to 10× the ledger's reference fee before returning `rpcHIGH_FEE`.
 *  Callers can tighten this cap by providing `fee_mult_max` / `fee_div_max`
 *  in the outer request JSON.
 *
 *  @param request    Mutable reference to the full RPC request JSON. The
 *      `tx_json.Fee` field is written in place on success.
 *  @param role       Request role; forwarded to `getCurrentNetworkFee` for
 *      load-fee exemption logic.
 *  @param doAutoFill If false and `Fee` is absent, returns a missing-field
 *      error rather than computing a fee.
 *  @param config     Node configuration.
 *  @param feeTrack   Current load fee tracker.
 *  @param txQ        Transaction queue.
 *  @param app        Application reference.
 *  @return An empty `json::Value` on success, or an error object on failure.
 */
json::Value
checkFee(
    json::Value& request,
    Role const role,
    bool doAutoFill,
    Config const& config,
    LoadFeeTrack const& feeTrack,
    TxQ const& txQ,
    Application const& app);

/** Callback type for submitting a signed transaction to the P2P network.
 *
 *  The submit functions accept this instead of calling
 *  `NetworkOPs::processTransaction` directly, which allows unit tests to
 *  supply a mock without spinning up a full `NetworkOPs` instance.
 *  Sign-only functions (`transactionSign`, `transactionSignFor`) do not
 *  accept this type at all, making it structurally impossible for them to
 *  accidentally submit.
 *
 *  @see getProcessTxnFn
 */
using ProcessTransactionFn = std::function<void(
    std::shared_ptr<Transaction>& transaction,
    bool bUnlimited,
    bool bLocal,
    NetworkOPs::FailHard failType)>;

/** Create a `ProcessTransactionFn` that forwards to `NetworkOPs::processTransaction`.
 *
 *  This is the production factory for `ProcessTransactionFn`. It captures
 *  `netOPs` by reference; the returned function must not outlive the
 *  `NetworkOPs` object.
 *
 *  @param netOPs  The `NetworkOPs` instance to forward submissions to.
 *  @return A callable that delegates to `netOPs.processTransaction`.
 */
inline ProcessTransactionFn
getProcessTxnFn(NetworkOPs& netOPs)
{
    return [&netOPs](
               std::shared_ptr<Transaction>& transaction,
               bool bUnlimited,
               bool bLocal,
               NetworkOPs::FailHard failType) {
        netOPs.processTransaction(transaction, bUnlimited, bLocal, failType);
    };
}

/** Sign a transaction and return it without broadcasting it to the network.
 *
 *  Implements the `sign` RPC command. Runs the full pre-processing pipeline
 *  in single-signing mode — including field auto-fill (`Fee`, `Sequence`,
 *  `SigningPubKey`), keypair extraction, `STTx` construction, and
 *  cryptographic signing — then sterilizes the result via a round-trip
 *  serialization/deserialization cycle. The signed transaction is returned
 *  to the caller as `tx_blob` and `tx_json` for independent submission.
 *
 *  @param params             Full RPC request JSON. Passed by value because
 *      the implementation mutates it freely during preprocessing.
 *  @param apiVersion         Negotiated API version; governs error code selection
 *      (`rpcNO_CURRENT` for v1, `rpcNOT_SYNCED` for v2+).
 *  @param failType           Failure-hard flag (accepted for API symmetry with
 *      `transactionSubmit`; not acted on by this function).
 *  @param role               Request role; used for load-fee exemption and
 *      cluster-overload gating.
 *  @param validatedLedgerAge Age of the most recently validated ledger. Requests
 *      are rejected if this exceeds `Tuning::kMAX_VALIDATED_LEDGER_AGE` (2 min).
 *  @param app                Application reference.
 *  @return JSON object containing `tx_json` and `tx_blob` on success, or an
 *      error object on failure.
 */
json::Value
transactionSign(
    json::Value params,
    unsigned apiVersion,
    NetworkOPs::FailHard failType,
    Role role,
    std::chrono::seconds validatedLedgerAge,
    Application& app);

/** Sign and immediately submit a transaction to the network.
 *
 *  Implements the `submit` RPC command when no pre-signed `tx_blob` is
 *  provided. Performs the same preprocessing and signing as `transactionSign`,
 *  then calls `processTransaction` to forward the transaction to `NetworkOPs`.
 *  The response includes engine result fields (`engine_result`,
 *  `engine_result_code`, `engine_result_message`) in addition to `tx_blob`
 *  and `tx_json`.
 *
 *  @param params             Full RPC request JSON. Passed by value because
 *      the implementation mutates it freely during preprocessing.
 *  @param apiVersion         Negotiated API version.
 *  @param failType           Failure-hard flag forwarded to `processTransaction`.
 *  @param role               Request role; unlimited roles receive queue priority
 *      via `bUnlimited = true` in the `processTransaction` call.
 *  @param validatedLedgerAge Age of the most recently validated ledger. Requests
 *      are rejected if this exceeds `Tuning::kMAX_VALIDATED_LEDGER_AGE` (2 min).
 *  @param app                Application reference.
 *  @param processTransaction Callback that forwards the signed transaction to
 *      the P2P layer; use `getProcessTxnFn` for production callers.
 *  @return JSON object with `tx_json`, `tx_blob`, and engine result fields on
 *      success, or an error object on failure.
 */
json::Value
transactionSubmit(
    json::Value params,
    unsigned apiVersion,
    NetworkOPs::FailHard failType,
    Role role,
    std::chrono::seconds validatedLedgerAge,
    Application& app,
    ProcessTransactionFn const& processTransaction);

/** Add one participant's signature to a multi-signed transaction.
 *
 *  Implements the `sign_for` RPC command. Signs on behalf of the `account`
 *  specified in `params` using that account's supplied secret, then appends
 *  a `Signer` entry to the `Signers` array in `tx_json`. Field auto-fill is
 *  skipped — the transaction must already be fully formed before any signer
 *  contributes their signature. The result is returned without submission.
 *
 *  @param params             Full RPC request JSON. Passed by value because
 *      the implementation mutates it freely during preprocessing.
 *  @param apiVersion         Negotiated API version.
 *  @param failType           Failure-hard flag (accepted for API symmetry;
 *      not acted on by this function).
 *  @param role               Request role.
 *  @param validatedLedgerAge Age of the most recently validated ledger. Requests
 *      are rejected if this exceeds `Tuning::kMAX_VALIDATED_LEDGER_AGE` (2 min).
 *  @param app                Application reference.
 *  @return JSON object with the updated `tx_json` (including the new `Signer`
 *      entry) and `tx_blob`, or an error object on failure.
 */
json::Value
transactionSignFor(
    json::Value params,
    unsigned apiVersion,
    NetworkOPs::FailHard failType,
    Role role,
    std::chrono::seconds validatedLedgerAge,
    Application& app);

/** Submit a fully assembled multi-signed transaction to the network.
 *
 *  Implements the `submit_multisigned` RPC command. Accepts a transaction
 *  whose `Signers` array is already populated with all required signatures
 *  and forwards it to `NetworkOPs`. Fee is validated but not auto-filled —
 *  the transaction must carry an explicit `Fee` field.
 *
 *  @param params             Full RPC request JSON. Passed by value because
 *      the implementation mutates it freely during preprocessing.
 *  @param apiVersion         Negotiated API version.
 *  @param failType           Failure-hard flag forwarded to `processTransaction`.
 *  @param role               Request role; unlimited roles receive queue priority.
 *  @param validatedLedgerAge Age of the most recently validated ledger. Requests
 *      are rejected if this exceeds `Tuning::kMAX_VALIDATED_LEDGER_AGE` (2 min).
 *  @param app                Application reference.
 *  @param processTransaction Callback that forwards the signed transaction to
 *      the P2P layer; use `getProcessTxnFn` for production callers.
 *  @return JSON object with `tx_json`, `tx_blob`, and engine result fields on
 *      success, or an error object on failure.
 */
json::Value
transactionSubmitMultiSigned(
    json::Value params,
    unsigned apiVersion,
    NetworkOPs::FailHard failType,
    Role role,
    std::chrono::seconds validatedLedgerAge,
    Application& app,
    ProcessTransactionFn const& processTransaction);

}  // namespace RPC
}  // namespace xrpl
