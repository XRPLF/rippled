#include <xrpl/tx/transactors/vault/VaultManagerCover.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/ledger/helpers/VaultManagerHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>

namespace xrpl {

namespace {

// The cover the issuer may claw back: the request, or everything above the
// required minimum when the request is zero or larger.
STAmount
clawAmount(SLE const& manager, Asset const& asset, STAmount const& requested)
{
    Number const available = manager[sfCoverAvailable];
    if (requested == beast::kZero || Number{requested} > available)
        return STAmount{asset, available};
    return STAmount{asset, Number{requested}};
}

}  // namespace

std::uint32_t
VaultManagerCover::getFlagsMask(PreflightContext const& ctx)
{
    return tfVaultManagerCoverMask;
}

NotTEC
VaultManagerCover::preflight(PreflightContext const& ctx)
{
    auto const& tx = ctx.tx;
    if (tx[sfVaultID] == beast::kZero)
        return temMALFORMED;

    // Exactly one of deposit, withdraw and clawback.
    auto const flags = tx.getFlags() & tfUniversalMask;
    if (flags == 0 || (flags & (flags - 1)) != 0)
        return temINVALID_FLAG;

    auto const amount = tx[sfAmount];
    if (!isLegalNet(amount))
        return temBAD_AMOUNT;
    if ((flags & tfCoverClawback) != 0)
    {
        // XRP has no issuer to claw it back; zero claws the maximum.
        if (amount.native())
            return temMALFORMED;
        if (amount < beast::kZero)
            return temBAD_AMOUNT;
        return tesSUCCESS;
    }
    if (amount <= beast::kZero)
        return temBAD_AMOUNT;
    return tesSUCCESS;
}

TER
VaultManagerCover::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;
    auto const account = tx[sfAccount];
    auto const amount = tx[sfAmount];
    auto const flags = tx.getFlags() & tfUniversalMask;

    auto const manager = ctx.view.read(keylet::vaultManager(tx[sfVaultID]));
    if (!manager)
        return tecNO_ENTRY;
    auto const vault = ctx.view.read(keylet::vault(tx[sfVaultID]));
    if (!vault)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    Asset const asset = vault->at(sfAsset);
    AccountID const pseudo = manager->at(sfAccount);

    if ((flags & tfCoverClawback) != 0)
    {
        if (asset.native() || asset.getIssuer() != account)
            return tecNO_PERMISSION;
        if (amount.asset() != asset)
            return tecWRONG_ASSET;
        if (clawAmount(*manager, asset, amount) <= beast::kZero)
            return tecINSUFFICIENT_FUNDS;

        auto const issuer = ctx.view.read(keylet::account(account));
        if (!issuer)
            return tefBAD_LEDGER;  // LCOV_EXCL_LINE
        if (asset.holds<Issue>())
        {
            if (!issuer->isFlag(lsfAllowTrustLineClawback) || issuer->isFlag(lsfNoFreeze))
                return tecNO_PERMISSION;
            return tesSUCCESS;
        }
        auto const issuance =
            ctx.view.read(keylet::mptokenIssuance(asset.get<MPTIssue>().getMptID()));
        if (!issuance || !issuance->isFlag(lsfMPTCanClawback))
            return tecNO_PERMISSION;
        return tesSUCCESS;
    }

    if (account != effectiveVaultRoles(ctx.view, manager).manager)
        return tecNO_PERMISSION;
    if (amount.asset() != asset)
        return tecWRONG_ASSET;

    if ((flags & tfCoverDeposit) != 0)
    {
        if (auto const ter = canTransfer(ctx.view, asset, account, pseudo))
            return ter;
        if (auto const ter = checkDepositFreeze(ctx.view, account, pseudo, asset))
            return ter;
        if (auto const ter = requireAuth(ctx.view, asset, account, AuthType::StrongAuth))
            return ter;
        if (accountHolds(
                ctx.view,
                account,
                asset,
                FreezeHandling::ZeroIfFrozen,
                AuthHandling::ZeroIfUnauthorized,
                ctx.j,
                SpendableHandling::FullBalance) < amount)
            return tecINSUFFICIENT_FUNDS;
        return tesSUCCESS;
    }

    if (Number{amount} > manager->at(sfCoverAvailable))
        return tecINSUFFICIENT_FUNDS;
    if (auto const ter = canTransfer(ctx.view, asset, pseudo, account, WaiveMPTCanTransfer::Yes))
        return ter;
    if (auto const ter = requireAuth(ctx.view, asset, account, AuthType::WeakAuth))
        return ter;
    if (!holdingExists(ctx.view, account, asset))
    {
        if (auto const ter = canAddHolding(ctx.view, asset); !isTesSuccess(ter))
            return ter;
    }
    if (auto const ter = checkWithdrawFreeze(ctx.view, pseudo, account, account, asset))
        return ter;
    if (accountHolds(
            ctx.view,
            pseudo,
            asset,
            FreezeHandling::ZeroIfFrozen,
            AuthHandling::ZeroIfUnauthorized,
            ctx.j) < amount)
        return tecINSUFFICIENT_FUNDS;
    return tesSUCCESS;
}

TER
VaultManagerCover::doApply()
{
    auto const& tx = ctx_.tx;
    auto const flags = tx.getFlags() & tfUniversalMask;
    auto manager = view().peek(keylet::vaultManager(tx[sfVaultID]));
    auto const vault = view().read(keylet::vault(tx[sfVaultID]));
    if (!manager || !vault)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    Asset const asset = vault->at(sfAsset);
    AccountID const pseudo = manager->at(sfAccount);
    auto const amount = tx[sfAmount];

    if ((flags & tfCoverDeposit) != 0)
    {
        if (auto const ter =
                accountSend(view(), accountID_, pseudo, amount, j_, {}, WaiveTransferFee::Yes))
            return ter;
        manager->at(sfCoverAvailable) += Number{amount};
        view().update(manager);
        associateAsset(*manager, asset);
        return tesSUCCESS;
    }

    if ((flags & tfCoverWithdraw) != 0)
    {
        manager->at(sfCoverAvailable) -= Number{amount};
        view().update(manager);
        associateAsset(*manager, asset);
        return doWithdraw(
            ctx_.getApplyViewContext(), accountID_, accountID_, pseudo, preFeeBalance_, amount, j_);
    }

    auto const claw = clawAmount(*manager, asset, amount);
    manager->at(sfCoverAvailable) -= Number{claw};
    view().update(manager);
    associateAsset(*manager, asset);
    return accountSend(view(), pseudo, accountID_, claw, j_, {}, WaiveTransferFee::Yes);
}

void
VaultManagerCover::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
}

bool
VaultManagerCover::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
