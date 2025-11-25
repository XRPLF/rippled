#include <xrpld/app/tx/detail/LoanBrokerCoverWithdraw.h>
//
#include <xrpld/app/misc/LendingHelpers.h>
#include <xrpld/app/tx/detail/Payment.h>

#include <xrpl/ledger/CredentialHelpers.h>

namespace ripple {

bool
LoanBrokerCoverWithdraw::checkExtraFeatures(PreflightContext const& ctx)
{
    return checkLendingProtocolDependencies(ctx);
}

NotTEC
LoanBrokerCoverWithdraw::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfLoanBrokerID] == beast::zero)
        return temINVALID;

    auto const dstAmount = ctx.tx[sfAmount];
    if (dstAmount <= beast::zero)
        return temBAD_AMOUNT;

    if (!isLegalNet(dstAmount))
        return temBAD_AMOUNT;

    if (auto const destination = ctx.tx[~sfDestination])
    {
        if (*destination == beast::zero)
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

    // The broker's pseudo-account is the source of funds.
    auto const pseudoAccountID = sleBroker->at(sfAccount);
    // Cannot transfer a non-transferable Asset
    if (auto const ret =
            canTransfer(ctx.view, vaultAsset, pseudoAccountID, dstAcct))
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
        // Always round the minimum required up.
        // Applies to `tenthBipsOfValue` as well as `roundToAsset`.
        NumberRoundModeGuard mg(Number::upward);
        return roundToAsset(
            vaultAsset,
            tenthBipsOfValue(
                currentDebtTotal,
                TenthBips32(sleBroker->at(sfCoverRateMinimum))),
            currentDebtTotal.exponent());
    }();
    if (coverAvail < amount)
        return tecINSUFFICIENT_FUNDS;
    if ((coverAvail - amount) < minimumCover)
        return tecINSUFFICIENT_FUNDS;

    if (accountHolds(
            ctx.view,
            pseudoAccountID,
            vaultAsset,
            FreezeHandling::fhZERO_IF_FROZEN,
            AuthHandling::ahZERO_IF_UNAUTHORIZED,
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
    auto const dstAcct = tx[~sfDestination].value_or(account_);

    auto broker = view().peek(keylet::loanbroker(brokerID));
    if (!broker)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const brokerPseudoID = *broker->at(sfAccount);

    // Decrease the LoanBroker's CoverAvailable by Amount
    broker->at(sfCoverAvailable) -= amount;
    view().update(broker);

    return doWithdraw(
        view(),
        tx,
        account_,
        dstAcct,
        brokerPseudoID,
        mPriorBalance,
        amount,
        j_);
}

//------------------------------------------------------------------------------

}  // namespace ripple
