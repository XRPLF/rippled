#include <xrpl/tx/transactors/token/MPTokenAuthorize.h>

#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
namespace xrpl {

std::uint32_t
MPTokenAuthorize::getFlagsMask(PreflightContext const& ctx)
{
    return tfMPTokenAuthorizeMask;
}

NotTEC
MPTokenAuthorize::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfAccount] == ctx.tx[~sfHolder])
        return temMALFORMED;

    return tesSUCCESS;
}

TER
MPTokenAuthorize::preclaim(PreclaimContext const& ctx)
{
    auto const accountID = ctx.tx[sfAccount];
    auto const holderID = ctx.tx[~sfHolder];

    // if non-issuer account submits this tx, then they are trying either:
    // 1. Unauthorize/delete MPToken
    // 2. Use/create MPToken
    //
    // Note: `accountID` is holder's account
    //       `holderID` is NOT used
    if (!holderID)
    {
        SLE::const_pointer const sleMpt =
            ctx.view.read(keylet::mptoken(ctx.tx[sfMPTokenIssuanceID], accountID));

        // There is an edge case where all holders have zero balance, issuance
        // is legally destroyed, then outstanding MPT(s) are deleted afterwards.
        // Thus, there is no need to check for the existence of the issuance if
        // the MPT is being deleted with a zero balance. Check for unauthorize
        // before fetching the MPTIssuance object.

        // if holder wants to delete/unauthorize a mpt
        if (ctx.tx.isFlag(tfMPTUnauthorize))
        {
            if (!sleMpt)
                return tecOBJECT_NOT_FOUND;

            if ((*sleMpt)[sfMPTAmount] != 0)
            {
                auto const sleMptIssuance =
                    ctx.view.read(keylet::mptIssuance(ctx.tx[sfMPTokenIssuanceID]));
                if (!sleMptIssuance)
                    return tefINTERNAL;  // LCOV_EXCL_LINE

                return tecHAS_OBLIGATIONS;
            }

            if ((*sleMpt)[~sfLockedAmount].value_or(0) != 0)
            {
                auto const sleMptIssuance =
                    ctx.view.read(keylet::mptIssuance(ctx.tx[sfMPTokenIssuanceID]));
                if (!sleMptIssuance)
                    return tefINTERNAL;  // LCOV_EXCL_LINE

                return tecHAS_OBLIGATIONS;
            }
            if (ctx.view.rules().enabled(featureSingleAssetVault) && sleMpt->isFlag(lsfMPTLocked))
                return tecNO_PERMISSION;

            return tesSUCCESS;
        }

        // Now test when the holder wants to hold/create/authorize a new MPT
        auto const sleMptIssuance = ctx.view.read(keylet::mptIssuance(ctx.tx[sfMPTokenIssuanceID]));

        if (!sleMptIssuance)
            return tecOBJECT_NOT_FOUND;

        if (accountID == (*sleMptIssuance)[sfIssuer])
            return tecNO_PERMISSION;

        // if holder wants to use and create a mpt
        if (sleMpt)
            return tecDUPLICATE;

        return tesSUCCESS;
    }

    auto const sleHolder = ctx.view.read(keylet::account(*holderID));
    if (!sleHolder)
        return tecNO_DST;

    auto const sleMptIssuance = ctx.view.read(keylet::mptIssuance(ctx.tx[sfMPTokenIssuanceID]));
    if (!sleMptIssuance)
        return tecOBJECT_NOT_FOUND;

    // If tx is submitted by issuer, they would either try to do the following
    // for allowlisting:
    // 1. authorize an account
    // 2. unauthorize an account
    //
    // Note: `accountID` is issuer's account
    //       `holderID` is holder's account
    if (accountID != (*sleMptIssuance)[sfIssuer])
        return tecNO_PERMISSION;

    // If tx is submitted by issuer, it only applies for MPT with
    // lsfMPTRequireAuth set
    if (!sleMptIssuance->isFlag(lsfMPTRequireAuth))
        return tecNO_AUTH;

    // The holder must create the MPT before the issuer can authorize it.
    if (!ctx.view.exists(keylet::mptoken(ctx.tx[sfMPTokenIssuanceID], *holderID)))
        return tecOBJECT_NOT_FOUND;

    // Can't unauthorize the pseudo-accounts because they are implicitly
    // always authorized. No need to amendment gate since Vault and LoanBroker
    // can only be created if the Vault amendment is enabled; AMM with MPToken asset
    // can only be created if MPTokensV2 is enabled.
    if (isPseudoAccount(ctx.view, *holderID, {&sfVaultID, &sfLoanBrokerID, &sfAMMID}))
        return tecNO_PERMISSION;

    return tesSUCCESS;
}

TER
MPTokenAuthorize::doApply()
{
    auto const& tx = ctx_.tx;
    return authorizeMPToken(
        ctx_.view(),
        preFeeBalance_,
        tx[sfMPTokenIssuanceID],
        accountID_,
        ctx_.journal,
        tx.getFlags(),
        tx[~sfHolder]);
}

void
MPTokenAuthorize::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
MPTokenAuthorize::finalizeInvariants(
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
