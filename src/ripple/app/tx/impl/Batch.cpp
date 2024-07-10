//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <ripple/app/tx/applySteps.h>
#include <ripple/app/tx/impl/AMMBid.h>
#include <ripple/app/tx/impl/AMMCreate.h>
#include <ripple/app/tx/impl/AMMDelete.h>
#include <ripple/app/tx/impl/AMMDeposit.h>
#include <ripple/app/tx/impl/AMMVote.h>
#include <ripple/app/tx/impl/AMMWithdraw.h>
#include <ripple/app/tx/impl/ApplyContext.h>
#include <ripple/app/tx/impl/Batch.h>
#include <ripple/app/tx/impl/CancelCheck.h>
#include <ripple/app/tx/impl/CancelOffer.h>
#include <ripple/app/tx/impl/CashCheck.h>
#include <ripple/app/tx/impl/Change.h>
#include <ripple/app/tx/impl/Clawback.h>
#include <ripple/app/tx/impl/CreateCheck.h>
#include <ripple/app/tx/impl/CreateOffer.h>
#include <ripple/app/tx/impl/CreateTicket.h>
#include <ripple/app/tx/impl/DID.h>
#include <ripple/app/tx/impl/DeleteAccount.h>
#include <ripple/app/tx/impl/DepositPreauth.h>
#include <ripple/app/tx/impl/Escrow.h>
#include <ripple/app/tx/impl/NFTokenAcceptOffer.h>
#include <ripple/app/tx/impl/NFTokenBurn.h>
#include <ripple/app/tx/impl/NFTokenCancelOffer.h>
#include <ripple/app/tx/impl/NFTokenCreateOffer.h>
#include <ripple/app/tx/impl/NFTokenMint.h>
#include <ripple/app/tx/impl/PayChan.h>
#include <ripple/app/tx/impl/Payment.h>
#include <ripple/app/tx/impl/SetAccount.h>
#include <ripple/app/tx/impl/SetRegularKey.h>
#include <ripple/app/tx/impl/SetSignerList.h>
#include <ripple/app/tx/impl/SetTrust.h>
#include <ripple/app/tx/impl/XChainBridge.h>
#include <ripple/basics/Log.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/TER.h>

namespace ripple {

namespace {

struct UnknownTxnType : std::exception
{
    TxType txnType;
    UnknownTxnType(TxType t) : txnType{t}
    {
    }
};

// Call a lambda with the concrete transaction type as a template parameter
// throw an "UnknownTxnType" exception on error
template <class F>
auto
with_txn_type(TxType txnType, F&& f)
{
    switch (txnType)
    {
        case ttACCOUNT_DELETE:
            return f.template operator()<DeleteAccount>();
        case ttACCOUNT_SET:
            return f.template operator()<SetAccount>();
        case ttCHECK_CANCEL:
            return f.template operator()<CancelCheck>();
        case ttCHECK_CASH:
            return f.template operator()<CashCheck>();
        case ttCHECK_CREATE:
            return f.template operator()<CreateCheck>();
        case ttDEPOSIT_PREAUTH:
            return f.template operator()<DepositPreauth>();
        case ttOFFER_CANCEL:
            return f.template operator()<CancelOffer>();
        case ttOFFER_CREATE:
            return f.template operator()<CreateOffer>();
        case ttESCROW_CREATE:
            return f.template operator()<EscrowCreate>();
        case ttESCROW_FINISH:
            return f.template operator()<EscrowFinish>();
        case ttESCROW_CANCEL:
            return f.template operator()<EscrowCancel>();
        case ttPAYCHAN_CLAIM:
            return f.template operator()<PayChanClaim>();
        case ttPAYCHAN_CREATE:
            return f.template operator()<PayChanCreate>();
        case ttPAYCHAN_FUND:
            return f.template operator()<PayChanFund>();
        case ttPAYMENT:
            return f.template operator()<Payment>();
        case ttREGULAR_KEY_SET:
            return f.template operator()<SetRegularKey>();
        case ttSIGNER_LIST_SET:
            return f.template operator()<SetSignerList>();
        case ttTICKET_CREATE:
            return f.template operator()<CreateTicket>();
        case ttTRUST_SET:
            return f.template operator()<SetTrust>();
        case ttAMENDMENT:
        case ttFEE:
        case ttUNL_MODIFY:
            return f.template operator()<Change>();
        case ttNFTOKEN_MINT:
            return f.template operator()<NFTokenMint>();
        case ttNFTOKEN_BURN:
            return f.template operator()<NFTokenBurn>();
        case ttNFTOKEN_CREATE_OFFER:
            return f.template operator()<NFTokenCreateOffer>();
        case ttNFTOKEN_CANCEL_OFFER:
            return f.template operator()<NFTokenCancelOffer>();
        case ttNFTOKEN_ACCEPT_OFFER:
            return f.template operator()<NFTokenAcceptOffer>();
        case ttCLAWBACK:
            return f.template operator()<Clawback>();
        case ttAMM_CREATE:
            return f.template operator()<AMMCreate>();
        case ttAMM_DEPOSIT:
            return f.template operator()<AMMDeposit>();
        case ttAMM_WITHDRAW:
            return f.template operator()<AMMWithdraw>();
        case ttAMM_VOTE:
            return f.template operator()<AMMVote>();
        case ttAMM_BID:
            return f.template operator()<AMMBid>();
        case ttAMM_DELETE:
            return f.template operator()<AMMDelete>();
        case ttXCHAIN_CREATE_BRIDGE:
            return f.template operator()<XChainCreateBridge>();
        case ttXCHAIN_MODIFY_BRIDGE:
            return f.template operator()<BridgeModify>();
        case ttXCHAIN_CREATE_CLAIM_ID:
            return f.template operator()<XChainCreateClaimID>();
        case ttXCHAIN_COMMIT:
            return f.template operator()<XChainCommit>();
        case ttXCHAIN_CLAIM:
            return f.template operator()<XChainClaim>();
        case ttXCHAIN_ADD_CLAIM_ATTESTATION:
            return f.template operator()<XChainAddClaimAttestation>();
        case ttXCHAIN_ADD_ACCOUNT_CREATE_ATTESTATION:
            return f.template operator()<XChainAddAccountCreateAttestation>();
        case ttXCHAIN_ACCOUNT_CREATE_COMMIT:
            return f.template operator()<XChainCreateAccountCommit>();
        case ttDID_SET:
            return f.template operator()<DIDSet>();
        case ttDID_DELETE:
            return f.template operator()<DIDDelete>();
        case ttBATCH:
            return f.template operator()<Batch>();
        default:
            throw UnknownTxnType(txnType);
    }
}
}  // namespace

// clang-format off
// Current formatter for rippled is based on clang-10, which does not handle `requires` clauses
template <class T>
requires(T::ConsequencesFactory == Transactor::Normal)
TxConsequences
    consequences_helper(PreflightContext const& ctx)
{
    return TxConsequences(ctx.tx);
};

// For Transactor::Blocker
template <class T>
requires(T::ConsequencesFactory == Transactor::Blocker)
TxConsequences
    consequences_helper(PreflightContext const& ctx)
{
    return TxConsequences(ctx.tx, TxConsequences::blocker);
};

// For Transactor::Custom
template <class T>
requires(T::ConsequencesFactory == Transactor::Custom)
TxConsequences
    consequences_helper(PreflightContext const& ctx)
{
    return T::makeTxConsequences(ctx);
};
// clang-format on

static std::pair<NotTEC, TxConsequences>
invoke_preflight(PreflightContext const& ctx)
{
    try
    {
        return with_txn_type(ctx.tx.getTxnType(), [&]<typename T>() {
            auto const tec = T::preflight(ctx);
            return std::make_pair(
                tec,
                isTesSuccess(tec) ? consequences_helper<T>(ctx)
                                  : TxConsequences{tec});
        });
    }
    catch (UnknownTxnType const& e)
    {
        // Should never happen
        JLOG(ctx.j.fatal())
            << "Unknown transaction type in preflight: " << e.txnType;
        assert(false);
        return {temUNKNOWN, TxConsequences{temUNKNOWN}};
    }
}

static TER
invoke_preclaim(PreclaimContext const& ctx)
{
    try
    {
        // use name hiding to accomplish compile-time polymorphism of static
        // class functions for Transactor and derived classes.
        return with_txn_type(ctx.tx.getTxnType(), [&]<typename T>() {
            // If the transactor requires a valid account and the transaction
            // doesn't list one, preflight will have already a flagged a
            // failure.
            auto const id = ctx.tx.getAccountID(sfAccount);

            if (id != beast::zero)
            {
                // TER result = T::checkSeqProxy(ctx.view, ctx.tx, ctx.j);

                // if (result != tesSUCCESS)
                //     return result;

                // Ignore Sequence Validation on ttBATCH txns
                TER result = tesSUCCESS;

                JLOG(ctx.j.trace()) << "invoke_preclaim.Batch: " << "\n";
                result = T::checkPriorTxAndLastLedger(ctx);

                if (result != tesSUCCESS)
                    return result;

                // result = T::checkFee(ctx, calculateBaseFee(ctx.view, ctx.tx));

                // if (result != tesSUCCESS)
                //     return result;

                result = T::checkSign(ctx);

                if (result != tesSUCCESS)
                    return result;
            }

            return T::preclaim(ctx);
        });
    }
    catch (UnknownTxnType const& e)
    {
        // Should never happen
        JLOG(ctx.j.fatal())
            << "Unknown transaction type in preclaim: " << e.txnType;
        assert(false);
        return temUNKNOWN;
    }
}

static std::pair<TER, bool>
invoke_apply(ApplyContext& ctx)
{
    try
    {
        return with_txn_type(ctx.tx.getTxnType(), [&]<typename T>() {
            T p(ctx);
            return p();
        });
    }
    catch (UnknownTxnType const& e)
    {
        // Should never happen
        JLOG(ctx.journal.fatal())
            << "Unknown transaction type in apply: " << e.txnType;
        assert(false);
        return {temUNKNOWN, false};
    }
}

TxConsequences
Batch::makeTxConsequences(PreflightContext const& ctx)
{
    return TxConsequences{ctx.tx, TxConsequences::normal};
}

std::vector<NotTEC> preflightResponses;

NotTEC
Batch::preflight(PreflightContext const& ctx)
{
    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto& tx = ctx.tx;

    auto const& txns = tx.getFieldArray(sfRawTransactions);
    if (txns.empty())
    {
        JLOG(ctx.j.error()) << "Batch: txns array empty.";
        return temMALFORMED;
    }

    if (txns.size() > 8)
    {
        JLOG(ctx.j.error()) << "Batch: txns array exceeds 12 entries.";
        return temMALFORMED;
    }

    for (auto const& txn : txns)
    {
        if (!txn.isFieldPresent(sfTransactionType))
        {
            JLOG(ctx.j.error())
                << "Batch: TransactionType missing in array entry.";
            return temMALFORMED;
        }

        auto const tt = txn.getFieldU16(sfTransactionType);
        auto const txtype = safe_cast<TxType>(tt);
        auto const account = txn.getAccountID(sfAccount);
        auto const stx =
            STTx(txtype, [&txn](STObject& obj) { obj = std::move(txn); });
        PreflightContext const pfctx(
            ctx.app,
            stx,
            ctx.rules,
            tapPREFLIGHT_BATCH,
            ctx.j);
        auto const response = invoke_preflight(pfctx);
        preflightResponses.push_back(response.first);
    }

    return preflight2(ctx);
}

std::vector<TER> preclaimResponses;

TER
Batch::preclaim(PreclaimContext const& ctx)
{
    if (!ctx.view.rules().enabled(featureBatch))
        return temDISABLED;

    auto const& txns = ctx.tx.getFieldArray(sfRawTransactions);
    for (std::size_t i = 0; i < txns.size(); ++i)
    {
        // Cannot continue on failed txns
        if (preflightResponses[i] != tesSUCCESS)
        {
            JLOG(ctx.j.error()) << "Batch: Failed Preflight Response: " << preflightResponses[i];
            preclaimResponses.push_back(TER(preflightResponses[i]));
            continue;
        }

        auto const& txn = txns[i];
        if (!txn.isFieldPresent(sfTransactionType))
        {
            JLOG(ctx.j.error())
                << "Batch: TransactionType missing in array entry.";
            return temMALFORMED;
        }

        auto const tt = txn.getFieldU16(sfTransactionType);
        auto const txtype = safe_cast<TxType>(tt);
        auto const stx =
            STTx(txtype, [&txn](STObject& obj) { obj = std::move(txn); });
        PreclaimContext const pcctx(
            ctx.app, ctx.view, preflightResponses[i], stx, ctx.flags, ctx.j);
        auto const response = invoke_preclaim(pcctx);
        preclaimResponses.push_back(response);
    }

    for (auto const& response : preclaimResponses)
    {
        if (response != tesSUCCESS)
        {
            return response;
        }
    }

    return tesSUCCESS;
}

TER
Batch::doApply()
{
    std::cout << "Batch::doApply(): " << "0" << "\n";
    Sandbox sb(&ctx_.view(), tapRETRY);

    uint32_t flags = ctx_.tx.getFlags();
    if (flags & tfBatchMask)
            return temINVALID_FLAG;

    // SANITIZE
    std::vector<STTx> stxTxns;
    auto const& txns = ctx_.tx.getFieldArray(sfRawTransactions);
    for (std::size_t i = 0; i < txns.size(); ++i)
    {
        auto const& txn = txns[i];
        if (!txn.isFieldPresent(sfTransactionType))
        {
            JLOG(ctx_.journal.error())
                << "Batch: TransactionType missing in array entry.";
            return temMALFORMED;
        }

        auto const tt = txn.getFieldU16(sfTransactionType);
        auto const txtype = safe_cast<TxType>(tt);
        auto const stx = STTx(txtype, [&txn](STObject& obj) { obj = std::move(txn); });
        stxTxns.push_back(stx);
    }

    // DRY RUN
    std::cout << "Batch::doApply(): " << "DRY 1" << "\n";
    std::vector<std::pair<std::uint16_t, TER>> dryVector;
    for (std::size_t i = 0; i < stxTxns.size(); ++i)
    {
        auto const& stx = stxTxns[i];
        ApplyContext actx(
            ctx_.app,
            ctx_.base_,
            stx,
            preclaimResponses[i],
            ctx_.view().fees().base,
            tapPREFLIGHT_BATCH,
            ctx_.journal);
        auto const result = invoke_apply(actx);
        dryVector.emplace_back(stx.getTxnType(), result.first);
        actx.discard();
    }

    TER preResult = tesSUCCESS;
    std::cout << "Batch::doApply(): " << "DRY 2" << "\n";
    ApplyViewImpl& avi = dynamic_cast<ApplyViewImpl&>(ctx_.view());
    for (auto const& dryRun : dryVector)
    {
        STObject meta{sfBatchExecution};
        meta.setFieldU8(sfTransactionResult, TERtoInt(dryRun.second));
        meta.setFieldU16(sfTransactionType, dryRun.first);
        avi.addBatchExecutionMetaData(std::move(meta));

        // tfAllOrNothing
        if (dryRun.second != tesSUCCESS && flags & tfAllOrNothing)
        {
            preResult = tecBATCH_FAILURE;
        }
    }

    std::cout << "Batch::doApply(): " << "1" << "\n";
    
    // WET RUN
    TER result = tesSUCCESS;
    std::cout << "Batch::doApply(): " << ctx_.base_.open() << "\n";
    if (preResult == tesSUCCESS)
    {
        std::vector<STObject> batch;
        avi.setHookMetaData(std::move(batch));
        for (std::size_t i = 0; i < stxTxns.size(); ++i)
        {
            auto const& stx = stxTxns[i];
            ApplyContext actx(
                ctx_.app,
                ctx_.base_,
                stx,
                preclaimResponses[i],
                ctx_.view().fees().base,
                ctx_.base_.open() == 1 ? tapPREFLIGHT_BATCH : ctx_.view().flags(),
                ctx_.journal);
            auto const _result = invoke_apply(actx);

            STObject meta{sfBatchExecution};
            meta.setFieldU8(sfTransactionResult, TERtoInt(_result.first));
            meta.setFieldU16(sfTransactionType, stx.getTxnType());
            if (_result.first == tesSUCCESS)
                meta.setFieldH256(sfTransactionHash, stx.getTransactionID());

            avi.addBatchExecutionMetaData(std::move(meta));

            std::cout << "tfAllOrNothing: " << (flags & tfAllOrNothing) << "\n";
            std::cout << "tfOnlyOne: " << (flags & tfOnlyOne) << "\n";
            std::cout << "tfUntilFailure: " << (flags & tfUntilFailure) << "\n";
            std::cout << "tfIndependent: " << (flags & tfIndependent) << "\n";
            std::cout << "tfBatchAtomic: " << _result.first << "\n";

            if (_result.first != tesSUCCESS)
            {
                if (flags & tfUntilFailure)
                {
                    actx.discard();
                    result = tesSUCCESS;
                    break;
                }
                if (flags & tfOnlyOne)
                {
                    actx.discard();
                    continue;
                }
            }

            if (_result.first == tesSUCCESS && flags & tfOnlyOne)
            {
                result = tesSUCCESS;
                break;
            }
        }
    }

    auto const sleBase = ctx_.base_.read(keylet::account(account_));
    if (!sleBase)
        return tefINTERNAL;

    auto const sleSrcAcc = sb.peek(keylet::account(account_));
    if (!sleSrcAcc)
        return tefINTERNAL;

    // std::cout << "ACCOUNT BASE SEQ: " << sleBase->getFieldU32(sfSequence) << "\n";
    // std::cout << "ACCOUNT BASE BALANCE: " << sleBase->getFieldAmount(sfBalance) << "\n";
    // std::cout << "ACCOUNT SEQ: " << sleSrcAcc->getFieldU32(sfSequence) << "\n";
    // std::cout << "ACCOUNT BALANCE: " << sleSrcAcc->getFieldAmount(sfBalance) << "\n";

    auto const feePaid = ctx_.tx[sfFee].xrp();
    // auto const& txns = ctx_.tx.getFieldArray(sfRawTransactions);
    sleSrcAcc->setFieldU32(sfSequence, ctx_.tx.getFieldU32(sfSequence) + txns.size() + 1);
    sleSrcAcc->setFieldAmount(sfBalance, sleBase->getFieldAmount(sfBalance).xrp() - feePaid);
    sb.update(sleSrcAcc);
    JLOG(ctx_.journal.trace()) << "Batch: FINAL>>>";
    sb.apply(ctx_.rawView());
    return result;
}

XRPAmount
Batch::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    XRPAmount extraFee{0};
    if (tx.isFieldPresent(sfRawTransactions))
    {
        XRPAmount txFees{0};
        auto const& txns = tx.getFieldArray(sfRawTransactions);
        for (auto const& txn : txns)
        {
            auto const tt = txn.getFieldU16(sfTransactionType);
            auto const txtype = safe_cast<TxType>(tt);
            auto const stx = STTx(txtype, [&txn](STObject& obj) { obj = std::move(txn); });
            txFees += Transactor::calculateBaseFee(view, tx);
        }
        extraFee += txFees;
    }
    return extraFee;
}

}  // namespace ripple
