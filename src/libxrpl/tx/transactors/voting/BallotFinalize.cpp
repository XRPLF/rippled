#include <xrpl/tx/transactors/voting/BallotFinalize.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <memory>
#include <utility>

namespace xrpl {

NotTEC
BallotFinalize::preflight(PreflightContext const& ctx)
{
    auto const& results = ctx.tx.getFieldArray(sfResults);
    if (results.empty() || results.size() > 8)
        return temMALFORMED;

    for (auto const& entry : results)
    {
        if (!entry.isFieldPresent(sfBallotWeight))
            return temMALFORMED;
    }

    // One decryption-correctness proof per option.
    if (ctx.tx[sfZKProof].empty() || ctx.tx[sfZKProof].size() % kEcClawbackProofLength != 0)
        return temMALFORMED;

    if (ctx.tx[sfZKProof].size() / kEcClawbackProofLength != results.size())
        return temMALFORMED;

    return tesSUCCESS;
}

XRPAmount
BallotFinalize::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    return Transactor::calculateBaseFee(view, tx, kConfidentialFeeMultiplier);
}

TER
BallotFinalize::preclaim(PreclaimContext const& ctx)
{
    auto const account = ctx.tx[sfAccount];
    if (!ctx.view.exists(keylet::account(account)))
        return terNO_ACCOUNT;

    auto const ballotID = ctx.tx[sfBallotID];
    auto const sleBallot = ctx.view.read(keylet::ballot(ballotID));
    if (!sleBallot)
        return tecNO_ENTRY;

    if (sleBallot->isFlag(lsfBallotFinalized))
        return tecDUPLICATE;

    // Finalization is only valid once the voting window has closed.
    if (ctx.view.parentCloseTime().time_since_epoch().count() < (*sleBallot)[sfCloseTime])
        return tecTOO_SOON;

    std::uint8_t const n = (*sleBallot)[sfOptionCount];
    auto const& results = ctx.tx.getFieldArray(sfResults);
    if (results.size() != n)
        return tecBALLOT_BAD_OPTIONS;

    Slice const proof = ctx.tx[sfZKProof];
    if (proof.size() != static_cast<std::size_t>(n) * kEcClawbackProofLength)
        return tecBALLOT_BAD_OPTIONS;

    STArray const& tally = sleBallot->getFieldArray(sfEncryptedTally);
    if (tally.size() != n)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    Slice const tallyKey = (*sleBallot)[sfTallyPublicKey];
    auto const ctxHash =
        getBallotFinalizeContextHash(account, ballotID, ctx.tx.getSeqProxy().value());

    // Each proof shows tally[i] decrypts to results[i] under the tally key.
    for (std::uint8_t i = 0; i < n; ++i)
    {
        Slice const optionProof{
            proof.data() + (i * kEcClawbackProofLength), kEcClawbackProofLength};
        if (auto const ter = verifyClawbackProof(
                results[i][sfBallotWeight],
                optionProof,
                tallyKey,
                tally[i][sfEncryptedVote],
                ctxHash);
            !isTesSuccess(ter))
        {
            return ter;
        }
    }

    return tesSUCCESS;
}

TER
BallotFinalize::doApply()
{
    auto sleBallot = view().peek(keylet::ballot(ctx_.tx[sfBallotID]));
    if (!sleBallot)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    STArray results;
    for (auto const& entry : ctx_.tx.getFieldArray(sfResults))
    {
        STObject o = STObject::makeInnerObject(sfBallotResult);
        o.setFieldU64(sfBallotWeight, entry[sfBallotWeight]);
        results.push_back(std::move(o));
    }
    sleBallot->setFieldArray(sfResults, results);
    (*sleBallot)[sfFlags] = (*sleBallot)[sfFlags] | lsfBallotFinalized;

    view().update(sleBallot);
    return tesSUCCESS;
}

void
BallotFinalize::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
}

bool
BallotFinalize::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
