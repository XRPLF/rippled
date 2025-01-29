//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <xrpld/app/tx/apply.h>
#include <xrpld/app/tx/detail/Batch.h>
#include <xrpld/ledger/Sandbox.h>
#include <xrpld/ledger/View.h>
#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

XRPAmount
Batch::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // Calculate the Inner Txn Fees
    XRPAmount txnFees{0};
    if (tx.isFieldPresent(sfRawTransactions))
    {
        auto const& txns = tx.getFieldArray(sfRawTransactions);
        for (STObject txn : txns)
        {
            STTx const stx = STTx{std::move(txn)};
            txnFees += ripple::calculateBaseFee(view, stx);
        }
    }

    // Calculate the Signers/BatchSigners Fees
    std::int32_t signerCount = 0;
    if (tx.isFieldPresent(sfBatchSigners))
    {
        for (STObject const& signer : tx.getFieldArray(sfBatchSigners))
        {
            if (signer.isFieldPresent(sfTxnSignature))
                signerCount += 1;
            else if (signer.isFieldPresent(sfSigners))
                signerCount += signer.getFieldArray(sfSigners).size();
        }
    }

    // 10 drops per batch signature + sum of inner tx fees + 10 drops for
    // processing + default base fee
    XRPAmount const baseFee = view.fees().base;
    return (signerCount * baseFee) + txnFees + baseFee +
        Transactor::calculateBaseFee(view, tx);
}

NotTEC
Batch::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureBatch))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto const parentBatchId = ctx.tx.getTransactionID();
    auto const outerAccount = ctx.tx.getAccountID(sfAccount);
    auto const flags = ctx.tx.getFlags();

    if (flags & tfBatchMask)
    {
        JLOG(ctx.j.trace()) << "BatchTrace[" << parentBatchId << "]:"
                            << "invalid flags.";
        return temINVALID_FLAG;
    }

    if (std::popcount(
            flags &
            (tfAllOrNothing | tfOnlyOne | tfUntilFailure | tfIndependent)) != 1)
    {
        JLOG(ctx.j.trace()) << "BatchTrace[" << parentBatchId << "]:"
                            << "too many flags.";
        return temINVALID_FLAG;
    }

    auto const& rawTxns = ctx.tx.getFieldArray(sfRawTransactions);
    if (rawTxns.size() <= 1)
    {
        JLOG(ctx.j.trace()) << "BatchTrace[" << parentBatchId << "]:"
                            << "txns array must have at least 2 entries.";
        return temARRAY_EMPTY;
    }

    if (rawTxns.size() > maxBatchTxCount)
    {
        JLOG(ctx.j.trace()) << "BatchTrace[" << parentBatchId << "]:"
                            << "txns array exceeds 8 entries.";
        return temARRAY_TOO_LARGE;
    }

    // Validation Inner Batch Txns
    std::unordered_set<AccountID> requiredSigners;
    std::unordered_set<uint256> uniqueHashes;
    for (STObject rb : rawTxns)
    {
        STTx const stx = STTx{std::move(rb)};
        auto const hash = stx.getTransactionID();
        if (!uniqueHashes.emplace(hash).second)
        {
            JLOG(ctx.j.trace()) << "BatchTrace[" << parentBatchId << "]: "
                                << "duplicate TxID found. "
                                << "txID: " << hash;
            return temMALFORMED;
        }

        if (stx.getFieldU16(sfTransactionType) == ttBATCH)
        {
            JLOG(ctx.j.trace()) << "BatchTrace[" << parentBatchId << "]: "
                                << "batch cannot have an inner batch txn. "
                                << "txID: " << hash;
            return temINVALID_INNER_BATCH;
        }

        if (stx.isFieldPresent(sfTxnSignature) ||
            stx.isFieldPresent(sfSigners) || !stx.getSigningPubKey().empty())
        {
            JLOG(ctx.j.trace()) << "BatchTrace[" << parentBatchId << "]: "
                                << "inner txn cannot include TxnSignature and "
                                   "Signers and SigningPubKey must be empty. "
                                << "txID: " << hash;
            return temINVALID_INNER_BATCH;
        }

        auto const innerAccount = stx.getAccountID(sfAccount);
        if (auto const preflightResult = ripple::preflight(
                ctx.app, ctx.rules, parentBatchId, stx, tapBATCH, ctx.j);
            preflightResult.ter != tesSUCCESS)
        {
            JLOG(ctx.j.trace()) << "BatchTrace[" << parentBatchId << "]: "
                                << "inner txn preflight failed. "
                                << "txID: " << hash;
            return temINVALID_INNER_BATCH;
        }

        // If the inner account is the same as the outer account, do not add the
        // inner account to the required signers set.
        if (innerAccount != outerAccount)
            requiredSigners.insert(innerAccount);
    }

    if (auto const ret = preflight2(ctx); !isTesSuccess(ret))
        return ret;

    // Validation Batch Signers
    std::unordered_set<AccountID> batchSigners;
    if (ctx.tx.isFieldPresent(sfBatchSigners))
    {
        STArray const& signers = ctx.tx.getFieldArray(sfBatchSigners);

        // Check that the batch signers array is not too large.
        if (signers.size() > maxBatchTxCount)
        {
            JLOG(ctx.j.trace()) << "BatchTrace[" << parentBatchId << "]: "
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
                JLOG(ctx.j.trace())
                    << "BatchTrace[" << parentBatchId << "]: "
                    << "signer cannot be the outer account: " << signerAccount;
                return temBAD_SIGNER;
            }

            if (!batchSigners.insert(signerAccount).second)
            {
                JLOG(ctx.j.trace())
                    << "BatchTrace[" << parentBatchId << "]: "
                    << "duplicate signer found: " << signerAccount;
                return temBAD_SIGNER;
            }

            // Check that the batch signer is in the required signers set.
            // Remove it if it does, as it can be crossed off the list.
            if (requiredSigners.erase(signerAccount) == 0)
            {
                JLOG(ctx.j.trace()) << "BatchTrace[" << parentBatchId << "]: "
                                    << "no account signature for inner txn.";
                return temBAD_SIGNER;
            }
        }

        // Check the batch signers signatures.
        auto const sigResult = ctx.tx.checkBatchSign(
            STTx::RequireFullyCanonicalSig::yes, ctx.rules);

        if (!sigResult)
        {
            JLOG(ctx.j.trace()) << "BatchTrace[" << parentBatchId << "]: "
                                << "invalid batch txn signature.";
            return temBAD_SIGNATURE;
        }
    }

    if (!requiredSigners.empty())
    {
        JLOG(ctx.j.trace()) << "BatchTrace[" << parentBatchId << "]: "
                            << "invalid batch signers.";
        return temBAD_SIGNER;
    }
    return tesSUCCESS;
}

TER
Batch::doApply()
{
    // Inner txns are applied in `applyBatchTransactions`, after the outer batch
    // txn is applied
    return tesSUCCESS;
}

}  // namespace ripple