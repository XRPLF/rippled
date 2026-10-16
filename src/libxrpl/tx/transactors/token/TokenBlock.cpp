#include <xrpl/tx/transactors/token/TokenBlock.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/transactors/token/TokenPreauth.h>

#include <cstdint>

namespace xrpl {

std::uint32_t
TokenBlock::getFlagsMask(PreflightContext const&)
{
    return tfTokenBlockMask;
}

NotTEC
TokenBlock::preflight(PreflightContext const& ctx)
{
    auto const holder = ctx.tx[sfHolder];
    if (!holder)
        return temINVALID_ACCOUNT_ID;

    if (holder == ctx.tx[sfAccount])
        return temMALFORMED;

    return tesSUCCESS;
}

TER
TokenBlock::preclaim(PreclaimContext const& ctx)
{
    auto const account = ctx.tx[sfAccount];
    auto const holder = ctx.tx[sfHolder];
    auto const issuanceID = ctx.tx[sfMPTokenIssuanceID];

    // As for TokenPreauth, the issuer is read from the issuance ID, so a block
    // can still be removed after the issuance is destroyed.
    if (MPTIssue{issuanceID}.getIssuer() != account)
        return tecNO_PERMISSION;

    bool const blocked = ctx.view.exists(keylet::tokenBlock(holder, issuanceID));

    if (ctx.tx.isFlag(tfUnblock))
        return blocked ? TER{tesSUCCESS} : TER{tecNO_ENTRY};

    if (!ctx.view.exists(keylet::mptokenIssuance(issuanceID)))
        return tecOBJECT_NOT_FOUND;

    // Pseudo-accounts (AMM, Vault, LoanBroker) hold tokens on behalf of their
    // participants and can not be blocked.
    if (auto const sleHolder = ctx.view.read(keylet::account(holder));
        sleHolder && isPseudoAccount(sleHolder))
        return tecPSEUDO_ACCOUNT;

    // The holder account need not exist yet: a sanctioned address must be
    // blockable before it is ever funded, otherwise it could be funded and
    // opt in within the same ledger.
    if (blocked)
        return tecDUPLICATE;

    return tesSUCCESS;
}

TER
TokenBlock::doApply()
{
    auto const holder = ctx_.tx[sfHolder];
    auto const issuanceID = ctx_.tx[sfMPTokenIssuanceID];
    auto const blockKeylet = keylet::tokenBlock(holder, issuanceID);

    // Unblocking does not re-authorize an existing MPToken; that stays a
    // separate, explicit issuer transaction.
    if (ctx_.tx.isFlag(tfUnblock))
        return TokenPreauth::removeFromLedger(view(), view().peek(blockKeylet), j_);

    // A block replaces a pre-authorization. Removing it first keeps the
    // issuer's owner count, and so its reserve, unchanged.
    if (auto const slePreauth = view().peek(keylet::tokenPreauth(holder, issuanceID)))
    {
        if (auto const ter = TokenPreauth::removeFromLedger(view(), slePreauth, j_);
            !isTesSuccess(ter))
            return ter;  // LCOV_EXCL_LINE
    }

    return TokenPreauth::createEntry(ctx_, preFeeBalance_, blockKeylet, j_);
}

void
TokenBlock::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
    // Checked for every transaction by the ValidTokenPreauth invariant.
}

bool
TokenBlock::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
