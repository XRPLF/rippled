#include <xrpl/tx/transactors/voting/BallotDelete.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <memory>

namespace xrpl {

/**
 * Seconds after CloseTime during which only finalization may delete a ballot.
 */
static constexpr std::uint32_t kBallotFinalizeGrace = 86400;

NotTEC
BallotDelete::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfBallotID] == beast::kZero)
        return temMALFORMED;
    return tesSUCCESS;
}

TER
BallotDelete::preclaim(PreclaimContext const& ctx)
{
    auto const account = ctx.tx[sfAccount];
    if (!ctx.view.exists(keylet::account(account)))
        return terNO_ACCOUNT;

    auto const ballotID = ctx.tx[sfBallotID];
    auto const now = ctx.view.parentCloseTime().time_since_epoch().count();

    // A voter deletes their own BallotVote once the ballot has closed.
    if (ctx.view.exists(keylet::ballotVote(ballotID, account)))
    {
        if (auto const sleBallot = ctx.view.read(keylet::ballot(ballotID));
            sleBallot && now < (*sleBallot)[sfCloseTime])
            return tecTOO_SOON;
        return tesSUCCESS;
    }

    // Otherwise the creator deletes the ballot itself.
    auto const sleBallot = ctx.view.read(keylet::ballot(ballotID));
    if (!sleBallot)
        return tecNO_ENTRY;

    if (sleBallot->getAccountID(sfOwner) != account)
        return tecNO_PERMISSION;

    // Deletable once finalized. An unfinalized ballot is deletable only after a
    // grace period past CloseTime, so the creator cannot delete a ballot the
    // moment voting ends and deny everyone the published result.
    if (!sleBallot->isFlag(lsfBallotFinalized) &&
        now < (*sleBallot)[sfCloseTime] + kBallotFinalizeGrace)
        return tecTOO_SOON;

    return tesSUCCESS;
}

TER
BallotDelete::doApply()
{
    auto const ballotID = ctx_.tx[sfBallotID];

    auto const sleOwner = view().peek(keylet::account(accountID_));
    if (!sleOwner)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const deleteObject = [&](std::shared_ptr<SLE> const& sle) -> TER {
        std::uint64_t const page = (*sle)[sfOwnerNode];
        if (!view().dirRemove(keylet::ownerDir(accountID_), page, sle->key(), false))
            return tefBAD_LEDGER;  // LCOV_EXCL_LINE
        decreaseOwnerCountForObject(view(), sleOwner, sle, 1, j_);
        view().erase(sle);
        return tesSUCCESS;
    };

    // Voter deleting their own cast record.
    if (auto sleVote = view().peek(keylet::ballotVote(ballotID, accountID_)))
        return deleteObject(sleVote);

    // Creator deleting the ballot.
    auto sleBallot = view().peek(keylet::ballot(ballotID));
    if (!sleBallot)
        return tecINTERNAL;  // LCOV_EXCL_LINE
    return deleteObject(sleBallot);
}

void
BallotDelete::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
}

bool
BallotDelete::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
