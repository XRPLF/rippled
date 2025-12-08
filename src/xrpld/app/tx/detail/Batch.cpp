#include <xrpld/app/tx/apply.h>
#include <xrpld/app/tx/detail/Batch.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

/**
 * @brief Calculates the total base fee for a batch transaction.
 *
 * This function computes the required base fee for a batch transaction,
 * including the base fee for the batch itself, the sum of base fees for
 * all inner transactions, and additional fees for each batch signer.
 * It performs overflow checks and validates the structure of the batch
 * and its signers.
 *
 * @param view The ledger view providing fee and state information.
 * @param tx The batch transaction to calculate the fee for.
 * @return XRPAmount The total base fee required for the batch transaction.
 *
 * @throws std::overflow_error If any fee calculation would overflow the
 * XRPAmount type.
 * @throws std::length_error If the number of inner transactions or signers
 * exceeds the allowed maximum.
 * @throws std::invalid_argument If an inner transaction is itself a batch
 * transaction.
 */
XRPAmount
Batch::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    XRPAmount const maxAmount{
        std::numeric_limits<XRPAmount::value_type>::max()};

    // batchBase: view.fees().base for batch processing + default base fee
    XRPAmount const baseFee = Transactor::calculateBaseFee(view, tx);

    // LCOV_EXCL_START
    if (baseFee > maxAmount - view.fees().base)
    {
        JLOG(debugLog().error()) << "BatchTrace: Base fee overflow detected.";
        return XRPAmount{INITIAL_XRP};
    }
    // LCOV_EXCL_STOP

    XRPAmount const batchBase = view.fees().base + baseFee;

    // Calculate the Inner Txn Fees
    XRPAmount txnFees{0};
    if (tx.isFieldPresent(sfRawTransactions))
    {
        auto const& txns = tx.getFieldArray(sfRawTransactions);

        // LCOV_EXCL_START
        if (txns.size() > maxBatchTxCount)
        {
            JLOG(debugLog().error())
                << "BatchTrace: Raw Transactions array exceeds max entries.";
            return XRPAmount{INITIAL_XRP};
        }
        // LCOV_EXCL_STOP

        for (STObject txn : txns)
        {
            STTx const stx = STTx{std::move(txn)};

            // LCOV_EXCL_START
            if (stx.getTxnType() == ttBATCH)
            {
                JLOG(debugLog().error())
                    << "BatchTrace: Inner Batch transaction found.";
                return XRPAmount{INITIAL_XRP};
            }
            // LCOV_EXCL_STOP

            auto const fee = ripple::calculateBaseFee(view, stx);
            // LCOV_EXCL_START
            if (txnFees > maxAmount - fee)
            {
                JLOG(debugLog().error())
                    << "BatchTrace: XRPAmount overflow in txnFees calculation.";
                return XRPAmount{INITIAL_XRP};
            }
            // LCOV_EXCL_STOP
            txnFees += fee;
        }
    }

    // Calculate the Signers/BatchSigners Fees
    std::int32_t signerCount = 0;
    if (tx.isFieldPresent(sfBatchSigners))
    {
        auto const& signers = tx.getFieldArray(sfBatchSigners);

        // LCOV_EXCL_START
        if (signers.size() > maxBatchTxCount)
        {
            JLOG(debugLog().error())
                << "BatchTrace: Batch Signers array exceeds max entries.";
            return XRPAmount{INITIAL_XRP};
        }
        // LCOV_EXCL_STOP

        for (STObject const& signer : signers)
        {
            if (signer.isFieldPresent(sfTxnSignature))
                signerCount += 1;
            else if (signer.isFieldPresent(sfSigners))
                signerCount += signer.getFieldArray(sfSigners).size();
        }
    }

    // LCOV_EXCL_START
    if (signerCount > 0 && view.fees().base > maxAmount / signerCount)
    {
        JLOG(debugLog().error())
            << "BatchTrace: XRPAmount overflow in signerCount calculation.";
        return XRPAmount{INITIAL_XRP};
    }
    // LCOV_EXCL_STOP

    XRPAmount signerFees = signerCount * view.fees().base;

    // LCOV_EXCL_START
    if (signerFees > maxAmount - txnFees)
    {
        JLOG(debugLog().error())
            << "BatchTrace: XRPAmount overflow in signerFees calculation.";
        return XRPAmount{INITIAL_XRP};
    }
    if (txnFees + signerFees > maxAmount - batchBase)
    {
        JLOG(debugLog().error())
            << "BatchTrace: XRPAmount overflow in total fee calculation.";
        return XRPAmount{INITIAL_XRP};
    }
    // LCOV_EXCL_STOP

    // 10 drops per batch signature + sum of inner tx fees + batchBase
    return signerFees + txnFees + batchBase;
}

std::uint32_t
Batch::getFlagsMask(PreflightContext const& ctx)
{
    return tfBatchMask;
}

/**
 * @brief Performs preflight validation checks for a Batch transaction.
 *
 * This function validates the structure and contents of a Batch transaction
 * before it is processed. It ensures that the Batch feature is enabled,
 * checks for valid flags, validates the number and uniqueness of inner
 * transactions, and enforces correct signing and fee requirements.
 *
 * The following validations are performed:
 * - The Batch feature must be enabled in the current rules.
 * - Only one of the mutually exclusive batch flags must be set.
 * - The batch must contain at least two and no more than the maximum allowed
 * inner transactions.
 * - Each inner transaction must:
 *   - Be unique within the batch.
 *   - Not itself be a Batch transaction.
 *   - Have the tfInnerBatchTxn flag set.
 *   - Not include a TxnSignature or Signers field.
 *   - Have an empty SigningPubKey.
 *   - Pass its own preflight checks.
 *   - Have a fee of zero.
 *   - Have either Sequence or TicketSequence set, but not both or neither.
 *   - Not duplicate Sequence or TicketSequence values for the same account (for
 * certain flags).
 * - Validates that all required inner transaction accounts are present in the
 * batch signers array, and that all batch signers are unique and not the outer
 * account.
 * - Verifies the batch signature if batch signers are present.
 *
 * @param ctx The PreflightContext containing the transaction and environment.
 * @return NotTEC Returns tesSUCCESS if all checks pass, or an appropriate error
 * code otherwise.
 */
NotTEC
Batch::preflight(PreflightContext const& ctx)
{
    auto const parentBatchId = ctx.tx.getTransactionID();
    auto const flags = ctx.tx.getFlags();

    if (std::popcount(
            flags &
            (tfAllOrNothing | tfOnlyOne | tfUntilFailure | tfIndependent)) != 1)
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

    if (rawTxns.size() > maxBatchTxCount)
    {
        JLOG(ctx.j.debug()) << "BatchTrace[" << parentBatchId << "]:"
                            << "txns array exceeds 8 entries.";
        return temARRAY_TOO_LARGE;
    }

    // Validation Inner Batch Txns
    std::unordered_set<uint256> uniqueHashes;
    std::unordered_map<AccountID, std::unordered_set<std::uint32_t>>
        accountSeqTicket;
    auto checkSignatureFields = [&parentBatchId, &j = ctx.j](
                                    STObject const& sig,
                                    uint256 const& hash,
                                    char const* label = "") -> NotTEC {
        if (sig.isFieldPresent(sfTxnSignature))
        {
            JLOG(j.debug())
                << "BatchTrace[" << parentBatchId << "]: "
                << "inner txn " << label << "cannot include TxnSignature. "
                << "txID: " << hash;
            return temBAD_SIGNATURE;
        }

        if (sig.isFieldPresent(sfSigners))
        {
            JLOG(j.debug())
                << "BatchTrace[" << parentBatchId << "]: "
                << "inner txn " << label << " cannot include Signers. "
                << "txID: " << hash;
            return temBAD_SIGNER;
        }

        if (!sig.getFieldVL(sfSigningPubKey).empty())
        {
            JLOG(j.debug())
                << "BatchTrace[" << parentBatchId << "]: "
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

        if (std::any_of(
                disabledTxTypes.begin(),
                disabledTxTypes.end(),
                [txType](auto const& disabled) { return txType == disabled; }))
        {
            return temINVALID_INNER_BATCH;
        }

        if (!(stx.getFlags() & tfInnerBatchTxn))
        {
            JLOG(ctx.j.debug())
                << "BatchTrace[" << parentBatchId << "]: "
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
            auto const counterpartySignature =
                stx.getFieldObject(sfCounterpartySignature);
            if (auto const ret = checkSignatureFields(
                    counterpartySignature, hash, "counterparty signature "))
            {
                return ret;
            }
        }

        auto const innerAccount = stx.getAccountID(sfAccount);
        if (auto const preflightResult = ripple::preflight(
                ctx.app, ctx.rules, parentBatchId, stx, tapBATCH, ctx.j);
            preflightResult.ter != tesSUCCESS)
        {
            JLOG(ctx.j.debug()) << "BatchTrace[" << parentBatchId << "]: "
                                << "inner txn preflight failed: "
                                << transHuman(preflightResult.ter) << " "
                                << "txID: " << hash;
            return temINVALID_INNER_BATCH;
        }

        // Check that the fee is zero
        if (auto const fee = stx.getFieldAmount(sfFee);
            !fee.native() || fee.xrp() != beast::zero)
        {
            JLOG(ctx.j.debug()) << "BatchTrace[" << parentBatchId << "]: "
                                << "inner txn must have a fee of 0. "
                                << "txID: " << hash;
            return temBAD_FEE;
        }

        // Check that Sequence and TicketSequence are not both present
        if (stx.isFieldPresent(sfTicketSequence) &&
            stx.getFieldU32(sfSequence) != 0)
        {
            JLOG(ctx.j.debug())
                << "BatchTrace[" << parentBatchId << "]: "
                << "inner txn must have exactly one of Sequence and "
                   "TicketSequence. "
                << "txID: " << hash;
            return temSEQ_AND_TICKET;
        }

        // Verify that either Sequence or TicketSequence is present
        if (!stx.isFieldPresent(sfTicketSequence) &&
            stx.getFieldU32(sfSequence) == 0)
        {
            JLOG(ctx.j.debug()) << "BatchTrace[" << parentBatchId << "]: "
                                << "inner txn must have either Sequence or "
                                   "TicketSequence. "
                                << "txID: " << hash;
            return temSEQ_AND_TICKET;
        }

        // Duplicate sequence and ticket checks
        if (flags & (tfAllOrNothing | tfUntilFailure))
        {
            if (auto const seq = stx.getFieldU32(sfSequence); seq != 0)
            {
                if (!accountSeqTicket[innerAccount].insert(seq).second)
                {
                    JLOG(ctx.j.debug())
                        << "BatchTrace[" << parentBatchId << "]: "
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
                    JLOG(ctx.j.debug())
                        << "BatchTrace[" << parentBatchId << "]: "
                        << "duplicate ticket found: "
                        << "txID: " << hash;
                    return temREDUNDANT;
                }
            }
        }
    }

    return tesSUCCESS;
}

NotTEC
Batch::preflightSigValidated(PreflightContext const& ctx)
{
    auto const parentBatchId = ctx.tx.getTransactionID();
    auto const outerAccount = ctx.tx.getAccountID(sfAccount);
    auto const& rawTxns = ctx.tx.getFieldArray(sfRawTransactions);

    // Build the signers list
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

    // Validation Batch Signers
    std::unordered_set<AccountID> batchSigners;
    if (ctx.tx.isFieldPresent(sfBatchSigners))
    {
        STArray const& signers = ctx.tx.getFieldArray(sfBatchSigners);

        // Check that the batch signers array is not too large.
        if (signers.size() > maxBatchTxCount)
        {
            JLOG(ctx.j.debug()) << "BatchTrace[" << parentBatchId << "]: "
                                << "signers array exceeds 8 entries.";
            return temARRAY_TOO_LARGE;
        }

        // Add batch signers to the set to ensure all signer accounts are
        // unique. Meanwhile, remove signer accounts from the set of inner
        // transaction accounts (`requiredSigners`). By the end of the loop,
        // `requiredSigners` should be empty, indicating that all inner
        // accounts are matched with signers.
        for (auto const& signer : signers)
        {
            AccountID const signerAccount = signer.getAccountID(sfAccount);
            if (signerAccount == outerAccount)
            {
                JLOG(ctx.j.debug())
                    << "BatchTrace[" << parentBatchId << "]: "
                    << "signer cannot be the outer account: " << signerAccount;
                return temBAD_SIGNER;
            }

            if (!batchSigners.insert(signerAccount).second)
            {
                JLOG(ctx.j.debug())
                    << "BatchTrace[" << parentBatchId << "]: "
                    << "duplicate signer found: " << signerAccount;
                return temREDUNDANT;
            }

            // Check that the batch signer is in the required signers set.
            // Remove it if it does, as it can be crossed off the list.
            if (requiredSigners.erase(signerAccount) == 0)
            {
                JLOG(ctx.j.debug()) << "BatchTrace[" << parentBatchId << "]: "
                                    << "no account signature for inner txn.";
                return temBAD_SIGNER;
            }
        }

        // Check the batch signers signatures.
        auto const sigResult = ctx.tx.checkBatchSign(ctx.rules);

        if (!sigResult)
        {
            JLOG(ctx.j.debug())
                << "BatchTrace[" << parentBatchId << "]: "
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

/**
 * @brief Checks the validity of signatures for a batch transaction.
 *
 * This method first verifies the standard transaction signature by calling
 * Transactor::checkSign. If the signature is not valid it returns the
 * corresponding error code.
 *
 * Next, it verifies the batch-specific signature requirements by calling
 * Transactor::checkBatchSign. If this check fails, it also returns the
 * corresponding error code.
 *
 * If both checks succeed, the function returns tesSUCCESS.
 *
 * @param ctx The PreclaimContext containing transaction and environment data.
 * @return NotTEC Returns tesSUCCESS if all signature checks pass, or an error
 * code otherwise.
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

/**
 * @brief Applies the outer batch transaction.
 *
 * This method is responsible for applying the outer batch transaction.
 * The inner transactions within the batch are applied separately in the
 * `applyBatchTransactions` method after the outer transaction is processed.
 *
 * @return TER Returns tesSUCCESS to indicate successful application of the
 * outer batch transaction.
 */
TER
Batch::doApply()
{
    return tesSUCCESS;
}

}  // namespace ripple
