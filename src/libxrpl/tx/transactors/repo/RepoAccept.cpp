#include <xrpl/tx/transactors/repo/RepoAccept.h>

#include <xrpl/basics/Log.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/RepoHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>

namespace xrpl {

std::uint32_t
RepoAccept::getFlagsMask(PreflightContext const& ctx)
{
    return tfUniversalMask;
}

NotTEC
RepoAccept::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
RepoAccept::preclaim(PreclaimContext const& ctx)
{
    auto const sleRepo = ctx.view.read(keylet::repo(ctx.tx[sfRepoID]));
    if (!sleRepo)
        return tecNO_ENTRY;

    if (repo::isActive(sleRepo))
        return tecREPO_ACTIVE;

    // Only the buyer named at create may accept.
    if ((*sleRepo)[sfCounterparty] != ctx.tx[sfAccount])
        return tecNO_PERMISSION;

    if (ctx.view.parentCloseTime().time_since_epoch().count() >= (*sleRepo)[sfExpiration])
        return tecEXPIRED;

    return tesSUCCESS;
}

TER
RepoAccept::doApply()
{
    auto const sleRepo = view().peek(keylet::repo(ctx_.tx[sfRepoID]));
    if (!sleRepo)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    STAmount const price = (*sleRepo)[sfPurchasePrice];
    AccountID const seller = (*sleRepo)[sfAccount];

    // The cash leg. A failure here fails the accept, leaving the offer pending.
    if (auto const ter = accountSend(view(), accountID_, seller, price, j_); !isTesSuccess(ter))
        return ter;

    // Interest runs from when the cash actually moved.
    (*sleRepo)[sfStartDate] = view().parentCloseTime().time_since_epoch().count();
    view().update(sleRepo);

    return tesSUCCESS;
}

void
RepoAccept::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
}

bool
RepoAccept::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
