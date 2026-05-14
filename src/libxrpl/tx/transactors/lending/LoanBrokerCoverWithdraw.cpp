/** @file
 *  Implementation of the `LoanBrokerCoverWithdraw` transactor (XLS-66).
 *
 *  This transactor is the only sanctioned path by which a broker owner can
 *  reclaim idle cover capital from the broker's pseudo-account. It enforces
 *  the minimum cover ratio — `roundUp(tenthBipsOfValue(sfDebtTotal,
 *  sfCoverRateMinimum))` — before permitting any withdrawal, ensuring
 *  outstanding loans remain adequately backed. Asset movement is delegated
 *  to `doWithdraw` in `View.h`, which handles pseudo-account settlement
 *  logic common across vault-adjacent withdrawal operations.
 *
 *  @see LoanBrokerCoverDeposit   symmetric deposit path (increments cover)
 *  @see LoanBrokerCoverClawback  forced cover reclaim by the vault asset issuer
 */
#include <xrpl/tx/transactors/lending/LoanBrokerCoverWithdraw.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <memory>

namespace xrpl {

/** Delegates amendment gating to `checkLendingProtocolDependencies`.
 *
 *  Returns `false` (causing `invokePreflight` to return `temDISABLED`) if
 *  the lending protocol amendment or any of its dependencies is not yet
 *  active on the ledger.
 */
bool
LoanBrokerCoverWithdraw::checkExtraFeatures(PreflightContext const& ctx)
{
    return checkLendingProtocolDependencies(ctx.rules, ctx.tx);
}

/** Stateless field validation — no ledger access.
 *
 *  Rejects structural nonsense before any state is consulted: a zero broker
 *  ID cannot map to a valid SLE, a non-positive amount is never a meaningful
 *  withdrawal, and a present-but-zero `sfDestination` would silently route
 *  funds to a black-hole address.
 */
NotTEC
LoanBrokerCoverWithdraw::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfLoanBrokerID] == beast::kZERO)
        return temINVALID;

    auto const dstAmount = ctx.tx[sfAmount];
    if (dstAmount <= beast::kZERO)
        return temBAD_AMOUNT;

    if (!isLegalNet(dstAmount))
        return temBAD_AMOUNT;

    if (auto const destination = ctx.tx[~sfDestination])
    {
        if (*destination == beast::kZERO)
        {
            return temMALFORMED;
        }
    }

    return tesSUCCESS;
}

/** Read-only ledger checks for a cover withdrawal.
 *
 *  Layers of enforcement (in order):
 *  - Destination must not be a pseudo-account.
 *  - `LoanBroker` SLE must exist and be owned by the submitter.
 *  - The broker's vault must exist (missing vault → `tefBAD_LEDGER`; excluded
 *    from coverage because the broker cannot outlive its vault in a well-formed
 *    ledger).
 *  - `sfAmount` asset must match the vault's `sfAsset`.
 *  - Asset transferability: `canTransfer` from the broker pseudo-account to
 *    the destination.
 *  - Third-party destination: upgrades to `StrongAuth` and calls `canWithdraw`
 *    (checks account existence, deposit-auth, tag requirements, trust limits).
 *    Withdrawing to self requires only `WeakAuth`, so the owner need not
 *    pre-establish a trust relationship with their own account.
 *  - Freeze checks against the pseudo-account (source) and destination, unless
 *    the destination is the asset issuer.
 *  - Minimum cover: `roundUp(tenthBipsOfValue(sfDebtTotal,
 *    sfCoverRateMinimum))` must remain after the withdrawal. Upward rounding
 *    is enforced via `NumberRoundModeGuard` to prevent rounding dust from
 *    under-collateralizing outstanding loans.
 *  - `accountHolds` confirms the pseudo-account's actual balance covers the
 *    requested amount, guarding against any divergence from `sfCoverAvailable`.
 */
TER
LoanBrokerCoverWithdraw::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;

    auto const account = tx[sfAccount];
    auto const brokerID = tx[sfLoanBrokerID];
    auto const amount = tx[sfAmount];

    auto const dstAcct = tx[~sfDestination].value_or(account);

    if (isPseudoAccount(ctx.view, dstAcct))
    {
        JLOG(ctx.j.warn()) << "Trying to withdraw into a pseudo-account.";
        return tecPSEUDO_ACCOUNT;
    }
    auto const sleBroker = ctx.view.read(keylet::loanbroker(brokerID));
    if (!sleBroker)
    {
        JLOG(ctx.j.warn()) << "LoanBroker does not exist.";
        return tecNO_ENTRY;
    }
    if (account != sleBroker->at(sfOwner))
    {
        JLOG(ctx.j.warn()) << "Account is not the owner of the LoanBroker.";
        return tecNO_PERMISSION;
    }
    auto const vault = ctx.view.read(keylet::vault(sleBroker->at(sfVaultID)));
    if (!vault)
    {
        // LCOV_EXCL_START
        JLOG(ctx.j.fatal()) << "Vault is missing for Broker " << brokerID;
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    auto const vaultAsset = vault->at(sfAsset);
    if (amount.asset() != vaultAsset)
        return tecWRONG_ASSET;

    // The broker's pseudo-account is the source of funds, not the submitter.
    auto const pseudoAccountID = sleBroker->at(sfAccount);
    if (auto const ret = canTransfer(ctx.view, vaultAsset, pseudoAccountID, dstAcct))
        return ret;

    // Withdrawal to a 3rd party destination is functionally a transfer:
    // enforce full consent checks and require StrongAuth (existing trust line
    // or MPToken).
    AuthType authType = AuthType::WeakAuth;
    if (account != dstAcct)
    {
        if (auto const ret = canWithdraw(ctx.view, tx))
            return ret;

        authType = AuthType::StrongAuth;
    }

    if (auto const ter = requireAuth(ctx.view, vaultAsset, dstAcct, authType))
        return ter;

    // Freeze checks are skipped when sending directly to the asset issuer.
    if (dstAcct != vaultAsset.getIssuer())
    {
        if (auto const ret = checkFrozen(ctx.view, pseudoAccountID, vaultAsset))
            return ret;
        if (auto const ret = checkDeepFrozen(ctx.view, dstAcct, vaultAsset))
            return ret;
    }

    auto const coverAvail = sleBroker->at(sfCoverAvailable);
    auto const currentDebtTotal = sleBroker->at(sfDebtTotal);
    auto const minimumCover = [&]() {
        // Always round the minimum required up — applies to both
        // `tenthBipsOfValue` and `roundToAsset` — so fractional dust cannot
        // be used to erode the collateral buffer below the required ratio.
        NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
        return roundToAsset(
            vaultAsset,
            tenthBipsOfValue(currentDebtTotal, TenthBips32(sleBroker->at(sfCoverRateMinimum))),
            scale(currentDebtTotal, vaultAsset));
    }();
    if (coverAvail < amount)
        return tecINSUFFICIENT_FUNDS;
    if ((coverAvail - amount) < minimumCover)
        return tecINSUFFICIENT_FUNDS;

    if (accountHolds(
            ctx.view,
            pseudoAccountID,
            vaultAsset,
            FreezeHandling::ZeroIfFrozen,
            AuthHandling::ZeroIfUnauthorized,
            ctx.j) < amount)
        return tecINSUFFICIENT_FUNDS;

    return tesSUCCESS;
}

/** Executes the cover withdrawal against the mutable ledger view.
 *
 *  Decrements `sfCoverAvailable` on the `LoanBroker` SLE, then calls
 *  `associateAsset` to re-round all `STNumber` fields to asset precision
 *  (required after any numeric mutation on an SLE that holds asset-denominated
 *  values). The actual token movement from the broker pseudo-account to the
 *  destination is performed by `doWithdraw`.
 *
 *  @note `associateAsset` must be the last call before `doWithdraw`; failing
 *      to call it produces silent precision corruption in the serialized SLE.
 *  @note The broker and vault peek/read failures return `tecINTERNAL` and are
 *      excluded from coverage — preclaim guarantees both objects exist.
 */
TER
LoanBrokerCoverWithdraw::doApply()
{
    auto const& tx = ctx_.tx;

    auto const brokerID = tx[sfLoanBrokerID];
    auto const amount = tx[sfAmount];
    auto const dstAcct = tx[~sfDestination].value_or(account_);

    auto broker = view().peek(keylet::loanbroker(brokerID));
    if (!broker)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const vault = view().read(keylet::vault(broker->at(sfVaultID)));
    if (!vault)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const vaultAsset = vault->at(sfAsset);

    auto const brokerPseudoID = *broker->at(sfAccount);

    broker->at(sfCoverAvailable) -= amount;
    view().update(broker);

    associateAsset(*broker, vaultAsset);

    return doWithdraw(view(), tx, account_, dstAcct, brokerPseudoID, preFeeBalance_, amount, j_);
}

/** No-op: no per-entry invariants are defined for this transaction type yet. */
void
LoanBrokerCoverWithdraw::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

/** No-op: no post-conditions are defined for this transaction type yet.
 *
 *  @return `true` unconditionally.
 */
bool
LoanBrokerCoverWithdraw::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

//------------------------------------------------------------------------------

}  // namespace xrpl
