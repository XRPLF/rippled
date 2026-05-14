/** @file
 *  Implements the `MPTokenAuthorize` transactor, which handles all
 *  authorization state transitions for Multi-Purpose Tokens (MPTs).
 *
 *  A single transaction type covers four operations differentiated by the
 *  presence of the optional `sfHolder` field and the `tfMPTUnauthorize` flag:
 *  holder opt-in (create `MPToken` SLE), holder opt-out (delete `MPToken` SLE),
 *  issuer allowlist grant (`lsfMPTAuthorized` set), and issuer allowlist revoke
 *  (`lsfMPTAuthorized` cleared). All ledger mutations are delegated to
 *  `authorizeMPToken()` in `MPTokenHelpers.cpp`.
 */
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
#include <memory>

namespace xrpl {

/** Return `tfMPTokenAuthorizeMask` — the complete set of valid flag bits for
 *  this transaction type.
 *
 *  Called by the `preflight1()` framework before field validation; any flag bit
 *  not present in the mask causes immediate rejection with `temINVALID_FLAG`.
 */
std::uint32_t
MPTokenAuthorize::getFlagsMask(PreflightContext const& ctx)
{
    return tfMPTokenAuthorizeMask;
}

/** Stateless preflight guard — rejects self-authorization attempts.
 *
 *  The only check needed here is that `sfAccount != sfHolder`. An account
 *  naming itself as the holder it wants to authorize would be degenerate: the
 *  holder and issuer paths in `preclaim` both assume these two identities are
 *  distinct, so catching this early avoids undefined branching downstream.
 *  Amendment gating and fee/sequence checks are handled by the surrounding
 *  `invokePreflight<T>()` machinery; this method need not repeat them.
 *
 *  @return `temMALFORMED` if `sfAccount == sfHolder`; `tesSUCCESS` otherwise.
 */
NotTEC
MPTokenAuthorize::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfAccount] == ctx.tx[~sfHolder])
        return temMALFORMED;

    return tesSUCCESS;
}

/** Validate ledger state for all four authorization operations.
 *
 *  Branches on whether `sfHolder` is present in the transaction:
 *
 *  **Holder path** (`sfHolder` absent — submitter is the holder):
 *
 *  The `tfMPTUnauthorize` check is performed before reading the issuance SLE.
 *  This ordering is intentional: when all holders reach a zero balance before
 *  the issuer destroys the issuance, the outstanding `MPToken` objects must
 *  still be cleanable after the issuance is gone. Checking the unauthorize flag
 *  first avoids a spurious `tecOBJECT_NOT_FOUND` in that cleanup scenario.
 *
 *  - With `tfMPTUnauthorize` (delete):
 *    - `MPToken` SLE must exist (`tecOBJECT_NOT_FOUND`).
 *    - `sfMPTAmount` must be zero (`tecHAS_OBLIGATIONS`).
 *    - `sfLockedAmount` (optional) must also be zero (`tecHAS_OBLIGATIONS`);
 *      a zero net balance with non-zero locked amount indicates active escrow
 *      or vault operations that have not yet settled.
 *    - When `featureSingleAssetVault` is active, `lsfMPTLocked` must be clear
 *      on the `MPToken` SLE (`tecNO_PERMISSION`); a vault hold prevents opt-out.
 *
 *  - Without `tfMPTUnauthorize` (create / opt-in):
 *    - `MPTokenIssuance` must exist (`tecOBJECT_NOT_FOUND`).
 *    - Submitter must not be the issuer (`tecNO_PERMISSION`).
 *    - `MPToken` SLE must not already exist (`tecDUPLICATE`).
 *
 *  **Issuer path** (`sfHolder` present — submitter is the issuer):
 *
 *  - Named holder account must exist on the ledger (`tecNO_DST`).
 *  - `MPTokenIssuance` must exist (`tecOBJECT_NOT_FOUND`).
 *  - Submitter must be `sfIssuer` on the issuance (`tecNO_PERMISSION`).
 *  - Issuance must carry `lsfMPTRequireAuth` (`tecNO_AUTH`); managing an
 *    allowlist on a non-auth issuance is meaningless.
 *  - Holder must have already created their `MPToken` SLE (`tecOBJECT_NOT_FOUND`);
 *    the protocol enforces a holder-first, issuer-second handshake.
 *  - Holder must not be a pseudo-account (`tecNO_PERMISSION`); vault and loan
 *    broker pseudo-accounts (identified by `sfVaultID` / `sfLoanBrokerID`) are
 *    implicitly always authorized by the protocol and cannot be managed via this
 *    path. No amendment gate is needed: such accounts only exist when
 *    `featureSingleAssetVault` is already active.
 *
 *  @return `tesSUCCESS`, or one of the `tec`/`tef` error codes described above.
 */
TER
MPTokenAuthorize::preclaim(PreclaimContext const& ctx)
{
    auto const accountID = ctx.tx[sfAccount];
    auto const holderID = ctx.tx[~sfHolder];

    if (!holderID)
    {
        std::shared_ptr<SLE const> const sleMpt =
            ctx.view.read(keylet::mptoken(ctx.tx[sfMPTokenIssuanceID], accountID));

        // Check for unauthorize before fetching the MPTIssuance object: a
        // holder may delete a zero-balance MPToken even after the issuance has
        // been destroyed (post-destruction cleanup path).
        if ((ctx.tx.getFlags() & tfMPTUnauthorize) != 0u)
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

            // A zero net balance with non-zero locked amount indicates tokens
            // still held in escrow or vault; must settle before opt-out.
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

        auto const sleMptIssuance = ctx.view.read(keylet::mptIssuance(ctx.tx[sfMPTokenIssuanceID]));

        if (!sleMptIssuance)
            return tecOBJECT_NOT_FOUND;

        if (accountID == (*sleMptIssuance)[sfIssuer])
            return tecNO_PERMISSION;

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

    std::uint32_t const mptIssuanceFlags = sleMptIssuance->getFieldU32(sfFlags);

    if (accountID != (*sleMptIssuance)[sfIssuer])
        return tecNO_PERMISSION;

    // Allowlist management only applies when the issuance requires auth;
    // granting/revoking on an open issuance is meaningless.
    if ((mptIssuanceFlags & lsfMPTRequireAuth) == 0u)
        return tecNO_AUTH;

    // The holder must opt in before the issuer can authorize them
    // (holder-first, issuer-second handshake).
    if (!ctx.view.exists(keylet::mptoken(ctx.tx[sfMPTokenIssuanceID], *holderID)))
        return tecOBJECT_NOT_FOUND;

    // Can't unauthorize the pseudo-accounts because they are implicitly
    // always authorized. No need to amendment gate since Vault and LoanBroker
    // can only be created if the Vault amendment is enabled.
    if (isPseudoAccount(ctx.view, *holderID, {&sfVaultID, &sfLoanBrokerID}))
        return tecNO_PERMISSION;

    return tesSUCCESS;
}

/** Apply the authorization change to the mutable ledger view.
 *
 *  Delegates entirely to `authorizeMPToken()`, passing `preFeeBalance_` — the
 *  XRP balance captured by the base `Transactor` class before the transaction
 *  fee was deducted — as the reserve baseline for new `MPToken` SLE creation.
 *  All ledger mutations (SLE create/delete, owner directory linking, flag
 *  toggling) happen inside that helper. Because `preclaim` has already
 *  established all preconditions, any SLE-lookup failure inside
 *  `authorizeMPToken` is treated as `tecINTERNAL` and annotated
 *  `LCOV_EXCL_LINE`.
 *
 *  @return `tesSUCCESS`, `tecINSUFFICIENT_RESERVE` when a new `MPToken` SLE
 *      cannot be created due to insufficient XRP reserve, or a `tef` code on
 *      internal invariant violations in `authorizeMPToken`.
 */
TER
MPTokenAuthorize::doApply()
{
    auto const& tx = ctx_.tx;
    return authorizeMPToken(
        ctx_.view(),
        preFeeBalance_,
        tx[sfMPTokenIssuanceID],
        account_,
        ctx_.journal,
        tx.getFlags(),
        tx[~sfHolder]);
}

/** No-op stub satisfying the `Transactor` pure-virtual interface.
 *
 *  No transaction-specific invariants are defined for `MPTokenAuthorize` yet.
 *  Global invariants in `InvariantCheck.cpp` (e.g., `ValidMPTIssuance`,
 *  `ValidMPTPayment`) cover the relevant MPT ledger state; this method is
 *  reserved for any future per-transactor checks.
 */
void
MPTokenAuthorize::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

/** No-op stub satisfying the `Transactor` pure-virtual interface.
 *
 *  Always returns `true`. No transaction-specific finalization invariants are
 *  defined for `MPTokenAuthorize`; the global checker suite handles MPT
 *  issuance and holder-count accounting. This method is the extension point
 *  for future per-transactor post-apply assertions.
 *
 *  @return Always `true`.
 */
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
