#include <xrpl/tx/transactors/nft/NFTokenModify.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/helpers/NFTokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/nft.h>
#include <xrpl/tx/Transactor.h>

#include <memory>

namespace xrpl {

NotTEC
NFTokenModify::preflight(PreflightContext const& ctx)
{
    if (auto owner = ctx.tx[~sfOwner]; owner == ctx.tx[sfAccount])
        return temMALFORMED;

    if (auto uri = ctx.tx[~sfURI])
    {
        if (uri->empty() || uri->length() > kMaxTokenUriLength)
            return temMALFORMED;
    }

    return tesSUCCESS;
}

TER
NFTokenModify::preclaim(PreclaimContext const& ctx)
{
    AccountID const account = ctx.tx[sfAccount];
    AccountID const owner = ctx.tx[ctx.tx.isFieldPresent(sfOwner) ? sfOwner : sfAccount];

    if (!nft::findToken(ctx.view, owner, ctx.tx[sfNFTokenID]))
        return tecNO_ENTRY;

    // Check if the NFT is mutable
    if ((nft::getFlags(ctx.tx[sfNFTokenID]) & nft::kFlagMutable) == 0)
        return tecNO_PERMISSION;

    // Verify permissions for the issuer
    if (AccountID const issuer = nft::getIssuer(ctx.tx[sfNFTokenID]); issuer != account)
    {
        auto const sle = ctx.view.read(keylet::account(issuer));
        if (!sle)
            return tecINTERNAL;  // LCOV_EXCL_LINE
        if (auto const minter = (*sle)[~sfNFTokenMinter]; minter != account)
            return tecNO_PERMISSION;
    }

    return tesSUCCESS;
}

TER
NFTokenModify::doApply()
{
    uint256 const nftokenID = ctx_.tx[sfNFTokenID];
    AccountID const owner = ctx_.tx[ctx_.tx.isFieldPresent(sfOwner) ? sfOwner : sfAccount];

    return nft::changeTokenURI(view(), owner, nftokenID, ctx_.tx[~sfURI]);
}

void
NFTokenModify::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
NFTokenModify::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
