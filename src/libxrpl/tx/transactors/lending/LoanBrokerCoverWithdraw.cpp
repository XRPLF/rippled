#include <xrpl/tx/transactors/lending/LoanBrokerCoverWithdraw.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/Feature.h>
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

namespace xrpl {

bool
LoanBrokerCoverWithdraw::checkExtraFeatures(PreflightContext const& ctx)
{
    return checkLendingProtocolDependencies(ctx.rules, ctx.tx);
}

NotTEC
LoanBrokerCoverWithdraw::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfLoanBrokerID] == beast::kZero)
        return temINVALID;

    auto const dstAmount = ctx.tx[sfAmount];
    if (dstAmount <= beast::kZero)
        return temBAD_AMOUNT;

    if (!isLegalNet(dstAmount))
        return temBAD_AMOUNT;

    if (auto const destination = ctx.tx[~sfDestination])
    {
        if (*destination == beast::kZero)
        {
            return temMALFORMED;
        }
    }

    return tesSUCCESS;
}

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

    // Helper handles both IOU and MPT correctly without explicit branching.
    if (auto const ret = canApplyToBrokerCover(
            ctx.view, sleBroker, vaultAsset, amount, ctx.j, "LoanBrokerCoverWithdraw"))
        return ret;

    // The broker's pseudo-account is the source of funds.
    auto const pseudoAccountID = sleBroker->at(sfAccount);
    // Post-fixCleanup3_2_0: cover withdraw is a recovery path that bypasses
    // the lsfMPTCanTransfer flag check, so an issuer cannot trap a broker's
    // first-loss capital. Other transferability checks (IOU NoRipple, freeze,
    // requireAuth) still apply.
    auto const waive = ctx.view.rules().enabled(fixCleanup3_2_0) ? WaiveMPTCanTransfer::Yes
                                                                 : WaiveMPTCanTransfer::No;
    if (auto const ret = canTransfer(ctx.view, vaultAsset, pseudoAccountID, dstAcct, waive))
        return ret;

    // Withdrawal to a 3rd party destination account is essentially a transfer.
    // Enforce all the usual asset transfer checks.
    AuthType authType = AuthType::WeakAuth;
    if (account != dstAcct)
    {
        if (auto const ret = canWithdraw(ctx.view, tx))
            return ret;

        // The destination account must have consented to receive the asset by
        // creating a RippleState or MPToken
        authType = AuthType::StrongAuth;
    }

    // Destination MPToken must exist (if asset is an MPT)
    if (auto const ter = requireAuth(ctx.view, vaultAsset, dstAcct, authType))
        return ter;

    // Check for freezes, unless sending directly to the issuer
    if (dstAcct != vaultAsset.getIssuer())
    {
        // Cannot send a frozen Asset
        if (auto const ret = checkFrozen(ctx.view, pseudoAccountID, vaultAsset))
            return ret;
        // Destination account cannot receive if asset is deep frozen
        if (auto const ret = checkDeepFrozen(ctx.view, dstAcct, vaultAsset))
            return ret;
    }

    auto const coverAvail = sleBroker->at(sfCoverAvailable);
    // Cover Rate is in 1/10 bips units
    auto const currentDebtTotal = sleBroker->at(sfDebtTotal);
    auto const minimumCover = [&]() {
        if (ctx.view.rules().enabled(fixCleanup3_2_0))
        {
            return minimumBrokerCover(
                currentDebtTotal, TenthBips32{sleBroker->at(sfCoverRateMinimum)}, vault);
        }

        // Always round the minimum required up.
        // Applies to `tenthBipsOfValue` as well as `roundToAsset`.
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

TER
LoanBrokerCoverWithdraw::doApply()
{
    auto const& tx = ctx_.tx;

    auto const brokerID = tx[sfLoanBrokerID];
    auto const amount = tx[sfAmount];
    auto const dstAcct = tx[~sfDestination].value_or(accountID_);

    auto broker = view().peek(keylet::loanbroker(brokerID));
    if (!broker)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const vault = view().read(keylet::vault(broker->at(sfVaultID)));
    if (!vault)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const vaultAsset = vault->at(sfAsset);

    auto const brokerPseudoID = *broker->at(sfAccount);

    // Decrease the LoanBroker's CoverAvailable by Amount
    broker->at(sfCoverAvailable) -= amount;
    view().update(broker);

    associateAsset(*broker, vaultAsset);

    return doWithdraw(view(), tx, accountID_, dstAcct, brokerPseudoID, preFeeBalance_, amount, j_);
}

void
LoanBrokerCoverWithdraw::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

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
