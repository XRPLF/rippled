#include <xrpl/tx/transactors/system/Batch.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/applySteps.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace xrpl {

/** Compute the total base fee for a Batch transaction.
 *
 *  Fee = `view.fees().base` (batch overhead) + `Transactor::calculateBaseFee`
 *  (normal outer-tx base) + sum(inner tx fees) + signerCount × `view.fees().base`.
 *  Each `sfBatchSigners` entry is expanded to its actual key count: a
 *  single-sig entry contributes 1, a multi-sig entry contributes the number of
 *  sub-signers in `sfSigners`.
 *
 *  Every accumulation is overflow-checked. On overflow or any structural error
 *  that should have been caught by `preflight`, `kINITIAL_XRP` is returned as
 *  a sentinel and the error is logged. These paths are unreachable in practice
 *  (covered by LCOV exclusions) because `preflight` enforces all size limits
 *  and rejects nested Batch transactions.
 *
 *  @param view The ledger view, used to obtain the network base fee.
 *  @param tx   The outer Batch transaction.
 *  @return Total fee in drops, or `kINITIAL_XRP` if an overflow is detected.
 */
XRPAmount
Batch::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    XRPAmount const maxAmount{std::numeric_limits<XRPAmount::value_type>::max()};

    XRPAmount const baseFee = Transactor::calculateBaseFee(view, tx);

    // LCOV_EXCL_START
    if (baseFee > maxAmount - view.fees().base)
    {
        JLOG(debugLog().error()) << "BatchTrace: Base fee overflow detected.";
        return XRPAmount{kINITIAL_XRP};
    }
    // LCOV_EXCL_STOP

    XRPAmount const batchBase = view.fees().base + baseFee;

    XRPAmount txnFees{0};
    if (tx.isFieldPresent(sfRawTransactions))
    {
        auto const& txns = tx.getFieldArray(sfRawTransactions);

        // LCOV_EXCL_START
        if (txns.size() > kMAX_BATCH_TX_COUNT)
        {
            JLOG(debugLog().error()) << "BatchTrace: Raw Transactions array exceeds max entries.";
            return XRPAmount{kINITIAL_XRP};
        }
        // LCOV_EXCL_STOP

        for (STObject txn : txns)
        {
            STTx const stx = STTx{std::move(txn)};

            // LCOV_EXCL_START
            if (stx.getTxnType() == ttBATCH)
            {
                JLOG(debugLog().error()) << "BatchTrace: Inner Batch transaction found.";
                return XRPAmount{kINITIAL_XRP};
            }
            // LCOV_EXCL_STOP

            auto const fee = xrpl::calculateBaseFee(view, stx);
            // LCOV_EXCL_START
            if (txnFees > maxAmount - fee)
            {
                JLOG(debugLog().error())
                    << "BatchTrace: XRPAmount overflow in txnFees calculation.";
                return XRPAmount{kINITIAL_XRP};
            }
            // LCOV_EXCL_STOP
            txnFees += fee;
        }
    }

    std::int32_t signerCount = 0;
    if (tx.isFieldPresent(sfBatchSigners))
    {
        auto const& signers = tx.getFieldArray(sfBatchSigners);

        // LCOV_EXCL_START
        if (signers.size() > kMAX_BATCH_TX_COUNT)
        {
            JLOG(debugLog().error()) << "BatchTrace: Batch Signers array exceeds max entries.";
            return XRPAmount{kINITIAL_XRP};
        }
        // LCOV_EXCL_STOP

        for (STObject const& signer : signers)
        {
            if (signer.isFieldPresent(sfTxnSignature))
            {
                signerCount += 1;
            }
            else if (signer.isFieldPresent(sfSigners))
            {
                signerCount += signer.getFieldArray(sfSigners).size();
            }
        }
    }

    // LCOV_EXCL_START
    if (signerCount > 0 && view.fees().base > maxAmount / signerCount)
    {
        JLOG(debugLog().error()) << "BatchTrace: XRPAmount overflow in signerCount calculation.";
        return XRPAmount{kINITIAL_XRP};
    }
    // LCOV_EXCL_STOP

    XRPAmount const signerFees = signerCount * view.fees().base;

    // LCOV_EXCL_START
    if (signerFees > maxAmount - txnFees)
    {
        JLOG(debugLog().error()) << "BatchTrace: XRPAmount overflow in signerFees calculation.";
        return XRPAmount{kINITIAL_XRP};
    }
    if (txnFees + signerFees > maxAmount - batchBase)
    {
        JLOG(debugLog().error()) << "BatchTrace: XRPAmount overflow in total fee calculation.";
        return XRPAmount{kINITIAL_XRP};
    }
    // LCOV_EXCL_STOP

    return signerFees + txnFees + batchBase;
}

std::uint32_t
Batch::getFlagsMask(PreflightContext const& ctx)
{
    return tfBatchMask;
}

/** Validate the structural integrity of the Batch transaction.
 *
 *  `std::popcount` is used on the policy-flag bits to guarantee exactly one
 *  execution policy is active — a simple range or switch check would silently
 *  pass flag combinations.
 *
 *  Duplicate-sequence detection (via `accountSeqTicket`) is enforced only for
 *  `tfAllOrNothing` and `tfUntilFailure`. Those modes commit or abort as a
 *  unit, so two inner transactions from the same account consuming the same
 *  sequence slot would be structurally incoherent. `tfIndependent` and
 *  `tfOnlyOne` relax this: only one of a pair may actually execute, so the
 *  collision is not guaranteed to matter.
 *
 *  Signer reconciliation (building `requiredSigners` and comparing against
 *  `sfBatchSigners`) is deferred to `preflightSigValidated` so it runs only
 *  after the outer account's own signature is confirmed valid by the framework.
 *
 *  @param ctx Preflight context for the outer Batch transaction.
 *  @return `tesSUCCESS` on success, or a `tem*` code describing the first
 *      structural violation found.
 */
NotTEC
Batch::preflight(PreflightContext const& ctx)
{
    auto const parentBatchId = ctx.tx.getTransactionID();
    auto const flags = ctx.tx.getFlags();

    if (std::popcount(flags & (tfAllOrNothing | tfOnlyOne | tfUntilFailure | tfIndependent)) != 1)
    {
        JLOG(ctx.j.debug()) << "BatchTrace[" << parentBatchId << "]:"
                            << "too many flags.";
        return temINVALID_FLAG;
    }

    auto const& rawTxns = ctx.tx.getFieldArray(sfRawTransactions);
    if (rawTxns.size() <= 1)
    {
        JLOG(ctx.j.debug()) << "BatchTrace[" << parentBatchId << "]:"
                            << "txns array must have at least 2 entries.";
        return temARRAY_EMPTY;
    }

    if (rawTxns.size() > kMAX_BATCH_TX_COUNT)
    {
        JLOG(ctx.j.debug()) << "BatchTrace[" << parentBatchId << "]:"
                            << "txns array exceeds 8 entries.";
        return temARRAY_TOO_LARGE;
    }

    std::unordered_set<uint256> uniqueHashes;
    std::unordered_map<AccountID, std::unordered_set<std::uint32_t>> accountSeqTicket;
    auto checkSignatureFields =
        [&parentBatchId, &j = ctx.j](
            STObject const& sig, uint256 const& hash, char const* label = "") -> NotTEC {
        if (sig.isFieldPresent(sfTxnSignature))
        {
            JLOG(j.debug()) << "BatchTrace[" << parentBatchId << "]: "
                            << "inner txn " << label << "cannot include TxnSignature. "
                            << "txID: " << hash;
            return temBAD_SIGNATURE;
        }

        if (sig.isFieldPresent(sfSigners))
        {
            JLOG(j.debug()) << "BatchTrace[" << parentBatchId << "]: "
                            << "inner txn " << label << " cannot include Signers. "
                            << "txID: " << hash;
            return temBAD_SIGNER;
        }

        if (!sig.getFieldVL(sfSigningPubKey).empty())
        {
            JLOG(j.debug()) << "BatchTrace[" << parentBatchId << "]: "
                            << "inner txn " << label << " SigningPubKey must be empty. "
                            << "txID: " << hash;
            return temBAD_REGKEY;
        }

        return tesSUCCESS;
    };
    for (STObject rb : rawTxns)
    {
        STTx const stx = STTx{std::move(rb)};
        auto const hash = stx.getTransactionID();
        if (!uniqueHashes.emplace(hash).second)
        {
            JLOG(ctx.j.debug()) << "BatchTrace[" << parentBatchId << "]: "
                                << "duplicate Txn found. "
                                << "txID: " << hash;
            return temREDUNDANT;
        }

        auto const txType = stx.getFieldU16(sfTransactionType);
        if (txType == ttBATCH)
        {
            JLOG(ctx.j.debug()) << "BatchTrace[" << parentBatchId << "]: "
                                << "batch cannot have an inner batch txn. "
                                << "txID: " << hash;
            return temINVALID;
        }

        if (std::ranges::any_of(
                kDISABLED_TX_TYPES, [txType](auto const& disabled) { return txType == disabled; }))
        {
            return temINVALID_INNER_BATCH;
        }

        if ((stx.getFlags() & tfInnerBatchTxn) == 0u)
        {
            JLOG(ctx.j.debug()) << "BatchTrace[" << parentBatchId << "]: "
                                << "inner txn must have the tfInnerBatchTxn flag. "
                                << "txID: " << hash;
            return temINVALID_FLAG;
        }

        if (auto const ret = checkSignatureFields(stx, hash))
            return ret;

        // Note that the CounterpartySignature is optional, and should not be
        // included, but if it is, ensure it doesn't contain a signature.
        if (stx.isFieldPresent(sfCounterpartySignature))
        {
            auto const counterpartySignature = stx.getFieldObject(sfCounterpartySignature);
            if (auto const ret =
                    checkSignatureFields(counterpartySignature, hash, "counterparty signature "))
            {
                return ret;
            }
        }

        if (auto const fee = stx.getFieldAmount(sfFee); !fee.native() || fee.xrp() != beast::kZERO)
        {
            JLOG(ctx.j.debug()) << "BatchTrace[" << parentBatchId << "]: "
                                << "inner txn must have a fee of 0. "
                                << "txID: " << hash;
            return temBAD_FEE;
        }

        auto const innerAccount = stx.getAccountID(sfAccount);
        if (auto const preflightResult =
                xrpl::preflight(ctx.registry, ctx.rules, parentBatchId, stx, TapBatch, ctx.j);
            !isTesSuccess(preflightResult.ter))
        {
            JLOG(ctx.j.debug()) << "BatchTrace[" << parentBatchId << "]: "
                                << "inner txn preflight failed: " << transHuman(preflightResult.ter)
                                << " "
                                << "txID: " << hash;
            return temINVALID_INNER_BATCH;
        }

        if (stx.isFieldPresent(sfTicketSequence) && stx.getFieldU32(sfSequence) != 0)
        {
            JLOG(ctx.j.debug()) << "BatchTrace[" << parentBatchId << "]: "
                                << "inner txn must have exactly one of Sequence and "
                                   "TicketSequence. "
                                << "txID: " << hash;
            return temSEQ_AND_TICKET;
        }

        if (!stx.isFieldPresent(sfTicketSequence) && stx.getFieldU32(sfSequence) == 0)
        {
            JLOG(ctx.j.debug()) << "BatchTrace[" << parentBatchId << "]: "
                                << "inner txn must have either Sequence or "
                                   "TicketSequence. "
                                << "txID: " << hash;
            return temSEQ_AND_TICKET;
        }

        if ((flags & (tfAllOrNothing | tfUntilFailure)) != 0u)
        {
            if (auto const seq = stx.getFieldU32(sfSequence); seq != 0)
            {
                if (!accountSeqTicket[innerAccount].insert(seq).second)
                {
                    JLOG(ctx.j.debug()) << "BatchTrace[" << parentBatchId << "]: "
                                        << "duplicate sequence found: "
                                        << "txID: " << hash;
                    return temREDUNDANT;
                }
            }

            if (stx.isFieldPresent(sfTicketSequence))
            {
                if (auto const ticket = stx.getFieldU32(sfTicketSequence);
                    !accountSeqTicket[innerAccount].insert(ticket).second)
                {
                    JLOG(ctx.j.debug()) << "BatchTrace[" << parentBatchId << "]: "
                                        << "duplicate ticket found: "
                                        << "txID: " << hash;
                    return temREDUNDANT;
                }
            }
        }
    }

    return tesSUCCESS;
}

/** Validate batch-signer authorization after the outer signature is verified.
 *
 *  Builds `requiredSigners` from every inner-transaction account and every
 *  `sfCounterparty` field that differs from the outer account, then performs a
 *  bidirectional reconciliation against `sfBatchSigners`: each batch signer is
 *  removed from `requiredSigners` as matched; a signer absent from
 *  `requiredSigners` is extraneous (`temBAD_SIGNER`). After the loop,
 *  any non-empty `requiredSigners` means a required party did not provide a
 *  batch signature. Finally, `ctx.tx.checkBatchSign()` verifies the
 *  cryptographic payload produced by `serializeBatch()`.
 *
 *  Called by the framework only after the outer account's own signature is
 *  confirmed valid, ensuring the submitter is authenticated before evaluating
 *  whether other parties have also signed.
 *
 *  @param ctx Preflight context, post outer-signature verification.
 *  @return `tesSUCCESS` if all required parties have valid batch signatures;
 *      a `tem*` code otherwise.
 */
NotTEC
Batch::preflightSigValidated(PreflightContext const& ctx)
{
    auto const parentBatchId = ctx.tx.getTransactionID();
    auto const outerAccount = ctx.tx.getAccountID(sfAccount);
    auto const& rawTxns = ctx.tx.getFieldArray(sfRawTransactions);

    std::unordered_set<AccountID> requiredSigners;
    for (STObject const& rb : rawTxns)
    {
        auto const innerAccount = rb.getAccountID(sfAccount);

        // If the inner account is the same as the outer account, do not add the
        // inner account to the required signers set.
        if (innerAccount != outerAccount)
            requiredSigners.insert(innerAccount);
        // Some transactions have a Counterparty, who must also sign the
        // transaction if they are not the outer account
        if (auto const counterparty = rb.at(~sfCounterparty);
            counterparty && counterparty != outerAccount)
            requiredSigners.insert(*counterparty);
    }

    std::unordered_set<AccountID> batchSigners;
    if (ctx.tx.isFieldPresent(sfBatchSigners))
    {
        STArray const& signers = ctx.tx.getFieldArray(sfBatchSigners);

        if (signers.size() > kMAX_BATCH_TX_COUNT)
        {
            JLOG(ctx.j.debug()) << "BatchTrace[" << parentBatchId << "]: "
                                << "signers array exceeds 8 entries.";
            return temARRAY_TOO_LARGE;
        }

        for (auto const& signer : signers)
        {
            AccountID const signerAccount = signer.getAccountID(sfAccount);
            if (signerAccount == outerAccount)
            {
                JLOG(ctx.j.debug()) << "BatchTrace[" << parentBatchId << "]: "
                                    << "signer cannot be the outer account: " << signerAccount;
                return temBAD_SIGNER;
            }

            if (!batchSigners.insert(signerAccount).second)
            {
                JLOG(ctx.j.debug()) << "BatchTrace[" << parentBatchId << "]: "
                                    << "duplicate signer found: " << signerAccount;
                return temREDUNDANT;
            }

            if (requiredSigners.erase(signerAccount) == 0)
            {
                JLOG(ctx.j.debug()) << "BatchTrace[" << parentBatchId << "]: "
                                    << "no account signature for inner txn.";
                return temBAD_SIGNER;
            }
        }

        auto const sigResult = ctx.tx.checkBatchSign(ctx.rules);

        if (!sigResult)
        {
            JLOG(ctx.j.debug()) << "BatchTrace[" << parentBatchId << "]: "
                                << "invalid batch txn signature: " << sigResult.error();
            return temBAD_SIGNATURE;
        }
    }

    if (!requiredSigners.empty())
    {
        JLOG(ctx.j.debug()) << "BatchTrace[" << parentBatchId << "]: "
                            << "invalid batch signers.";
        return temBAD_SIGNER;
    }
    return tesSUCCESS;
}

/** Verify the outer account's signature and the batch-signers' signatures.
 *
 *  Chains `Transactor::checkSign` (outer account: regular key, master key, or
 *  signer list) and `Transactor::checkBatchSign` (on-ledger credentials for
 *  each entry in `sfBatchSigners`). Both must pass; failure at the first check
 *  short-circuits.
 *
 *  @param ctx Preclaim context providing a read-only ledger view.
 *  @return `tesSUCCESS` if both checks pass; a `tef*` code otherwise.
 */
NotTEC
Batch::checkSign(PreclaimContext const& ctx)
{
    if (auto ret = Transactor::checkSign(ctx); !isTesSuccess(ret))
        return ret;

    if (auto ret = Transactor::checkBatchSign(ctx); !isTesSuccess(ret))
        return ret;

    return tesSUCCESS;
}

/** Apply the outer Batch transaction.
 *
 *  Returns `tesSUCCESS` immediately. The outer Batch does not directly mutate
 *  any ledger objects — fee deduction and sequence consumption are handled by
 *  the base-class `apply()` method. Inner transaction execution is performed
 *  by `applyBatchTransactions()` in `apply.cpp`, called by `applyTransaction()`
 *  after this method returns.
 *
 *  @return Always `tesSUCCESS`.
 */
TER
Batch::doApply()
{
    return tesSUCCESS;
}

/** Invariant visitor for the outer Batch transaction — currently a no-op.
 *
 *  The outer Batch does not directly create or modify ledger objects beyond
 *  fee and sequence handling, so there are no Batch-specific per-entry
 *  invariants to enforce. Inner-transaction invariant checks are performed by
 *  each inner transactor's own apply context.
 */
void
Batch::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
}

/** Finalize invariant checks for the outer Batch transaction — currently a no-op.
 *
 *  Returns `true` unconditionally. No outer-Batch invariant violations are
 *  possible: the outer transaction only deducts a fee and consumes a sequence,
 *  both of which are covered by shared invariant checkers. Inner-transaction
 *  invariants are finalized within each inner transactor's own apply context.
 *
 *  @return Always `true`.
 */
bool
Batch::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
