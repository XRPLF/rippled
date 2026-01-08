#include <xrpld/app/tx/detail/LoanBrokerSet.h>
//
#include <xrpld/app/misc/LendingHelpers.h>

#include <xrpl/protocol/STTakesAsset.h>

namespace ripple {

bool
LoanBrokerSet::checkExtraFeatures(PreflightContext const& ctx)
{
    return checkLendingProtocolDependencies(ctx);
}

NotTEC
LoanBrokerSet::preflight(PreflightContext const& ctx)
{
    using namespace Lending;

    auto const& tx = ctx.tx;
    if (auto const data = tx[~sfData]; data && !data->empty() &&
        !validDataLength(tx[~sfData], maxDataPayloadLength))
        return temINVALID;
    if (!validNumericRange(tx[~sfManagementFeeRate], maxManagementFeeRate))
        return temINVALID;
    if (!validNumericRange(tx[~sfCoverRateMinimum], maxCoverRate))
        return temINVALID;
    if (!validNumericRange(tx[~sfCoverRateLiquidation], maxCoverRate))
        return temINVALID;
    if (!validNumericRange(
            tx[~sfDebtMaximum], Number(maxMPTokenAmount), Number(0)))
        return temINVALID;

    if (tx.isFieldPresent(sfLoanBrokerID))
    {
        // Fixed fields can not be specified if we're modifying an existing
        // LoanBroker Object
        if (tx.isFieldPresent(sfManagementFeeRate) ||
            tx.isFieldPresent(sfCoverRateMinimum) ||
            tx.isFieldPresent(sfCoverRateLiquidation))
            return temINVALID;

        if (tx[sfLoanBrokerID] == beast::zero)
            return temINVALID;
    }

    if (auto const vaultID = tx.at(~sfVaultID))
    {
        if (*vaultID == beast::zero)
            return temINVALID;
    }

    {
        auto const minimumZero = tx[~sfCoverRateMinimum].value_or(0) == 0;
        auto const liquidationZero =
            tx[~sfCoverRateLiquidation].value_or(0) == 0;
        // Both must be zero or non-zero.
        if (minimumZero != liquidationZero)
        {
            return temINVALID;
        }
    }

    return tesSUCCESS;
}

std::vector<OptionaledField<STNumber>> const&
LoanBrokerSet::getValueFields()
{
    static std::vector<OptionaledField<STNumber>> const valueFields{
        ~sfDebtMaximum};

    return valueFields;
}

TER
LoanBrokerSet::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;

    auto const account = tx[sfAccount];
    auto const vaultID = tx[sfVaultID];

    auto const sleVault = ctx.view.read(keylet::vault(vaultID));
    if (!sleVault)
    {
        JLOG(ctx.j.warn()) << "Vault does not exist.";
        return tecNO_ENTRY;
    }
    Asset const asset = sleVault->at(sfAsset);

    if (account != sleVault->at(sfOwner))
    {
        JLOG(ctx.j.warn()) << "Account is not the owner of the Vault.";
        return tecNO_PERMISSION;
    }

    if (auto const brokerID = tx[~sfLoanBrokerID])
    {
        // Updating an existing Broker

        auto const sleBroker = ctx.view.read(keylet::loanbroker(*brokerID));
        if (!sleBroker)
        {
            JLOG(ctx.j.warn()) << "LoanBroker does not exist.";
            return tecNO_ENTRY;
        }
        if (vaultID != sleBroker->at(sfVaultID))
        {
            JLOG(ctx.j.warn())
                << "Can not change VaultID on an existing LoanBroker.";
            return tecNO_PERMISSION;
        }
        if (account != sleBroker->at(sfOwner))
        {
            JLOG(ctx.j.warn()) << "Account is not the owner of the LoanBroker.";
            return tecNO_PERMISSION;
        }

        if (auto const debtMax = tx[~sfDebtMaximum])
        {
            // Can't reduce the debt maximum below the current total debt
            auto const currentDebtTotal = sleBroker->at(sfDebtTotal);
            if (*debtMax != 0 && *debtMax < currentDebtTotal)
            {
                JLOG(ctx.j.warn())
                    << "Cannot reduce DebtMaximum below current DebtTotal.";
                return tecLIMIT_EXCEEDED;
            }
        }
    }
    else
    {
        if (auto const ter = canAddHolding(ctx.view, asset))
            return ter;

        if (auto const ter = checkFrozen(
                ctx.view, sleVault->at(sfAccount), sleVault->at(sfAsset)))
        {
            JLOG(ctx.j.warn()) << "Vault pseudo-account is frozen.";
            return ter;
        }
    }

    // Check that relevant values can be represented as the vault asset
    // type. This is mostly only relevant for integral (non-IOU) types
    for (auto const& field : getValueFields())
    {
        if (auto const value = tx[field];
            value && STAmount{asset, *value} != *value)
        {
            JLOG(ctx.j.warn()) << field.f->getName() << " (" << *value
                               << ") can not be represented as a(n) "
                               << to_string(asset) << ".";
            return tecPRECISION_LOSS;
        }
    }

    return tesSUCCESS;
}

TER
LoanBrokerSet::doApply()
{
    auto const& tx = ctx_.tx;
    auto& view = ctx_.view();

    if (auto const brokerID = tx[~sfLoanBrokerID])
    {
        // Modify an existing LoanBroker
        auto broker = view.peek(keylet::loanbroker(*brokerID));
        if (!broker)
        {
            // This should be impossible
            // LCOV_EXCL_START
            JLOG(j_.fatal()) << "LoanBroker does not exist.";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }

        auto const vault = view.read(keylet::vault(broker->at(sfVaultID)));
        if (!vault)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        auto const vaultAsset = vault->at(sfAsset);

        if (auto const data = tx[~sfData])
            broker->at(sfData) = *data;
        if (auto const debtMax = tx[~sfDebtMaximum])
            broker->at(sfDebtMaximum) = *debtMax;

        view.update(broker);

        associateAsset(*broker, vaultAsset);
    }
    else
    {
        // Create a new LoanBroker pointing back to the given Vault
        auto const vaultID = tx[sfVaultID];
        auto const sleVault = view.read(keylet::vault(vaultID));
        if (!sleVault)
        {
            // This should be impossible
            // LCOV_EXCL_START
            JLOG(j_.fatal()) << "Vault does not exist.";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }
        auto const vaultPseudoID = sleVault->at(sfAccount);
        auto const vaultAsset = sleVault->at(sfAsset);
        auto const sequence = tx.getSeqValue();

        auto owner = view.peek(keylet::account(account_));
        if (!owner)
        {
            // This should be impossible
            // LCOV_EXCL_START
            JLOG(j_.fatal()) << "Account does not exist.";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }
        auto broker =
            std::make_shared<SLE>(keylet::loanbroker(account_, sequence));

        if (auto const ter = dirLink(view, account_, broker))
            return ter;  // LCOV_EXCL_LINE
        if (auto const ter = dirLink(view, vaultPseudoID, broker, sfVaultNode))
            return ter;  // LCOV_EXCL_LINE

        // Increases the owner count by two: one for the LoanBroker object, and
        // one for the pseudo-account.
        adjustOwnerCount(view, owner, 2, j_);
        auto const ownerCount = owner->at(sfOwnerCount);
        if (mPriorBalance < view.fees().accountReserve(ownerCount))
            return tecINSUFFICIENT_RESERVE;

        auto maybePseudo =
            createPseudoAccount(view, broker->key(), sfLoanBrokerID);
        if (!maybePseudo)
            return maybePseudo.error();  // LCOV_EXCL_LINE
        auto& pseudo = *maybePseudo;
        auto pseudoId = pseudo->at(sfAccount);

        if (auto ter = addEmptyHolding(
                view, pseudoId, mPriorBalance, sleVault->at(sfAsset), j_))
            return ter;

        // Initialize data fields:
        broker->at(sfSequence) = sequence;
        broker->at(sfVaultID) = vaultID;
        broker->at(sfOwner) = account_;
        broker->at(sfAccount) = pseudoId;
        // The LoanSequence indexes loans created by this broker, starting at 1
        broker->at(sfLoanSequence) = 1;
        if (auto const data = tx[~sfData])
            broker->at(sfData) = *data;
        if (auto const rate = tx[~sfManagementFeeRate])
            broker->at(sfManagementFeeRate) = *rate;
        if (auto const debtMax = tx[~sfDebtMaximum])
            broker->at(sfDebtMaximum) = *debtMax;
        if (auto const coverMin = tx[~sfCoverRateMinimum])
            broker->at(sfCoverRateMinimum) = *coverMin;
        if (auto const coverLiq = tx[~sfCoverRateLiquidation])
            broker->at(sfCoverRateLiquidation) = *coverLiq;

        view.insert(broker);

        associateAsset(*broker, vaultAsset);
    }

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

}  // namespace ripple
