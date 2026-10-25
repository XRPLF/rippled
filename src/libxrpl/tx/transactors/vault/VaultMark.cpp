#include <xrpl/tx/transactors/vault/VaultMark.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/VaultManagerHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>

namespace xrpl {

NotTEC
VaultMark::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfVaultID] == beast::kZero)
        return temMALFORMED;
    if (ctx.tx[sfMarkPrice] <= Number{})
        return temMALFORMED;
    return tesSUCCESS;
}

TER
VaultMark::preclaim(PreclaimContext const& ctx)
{
    auto const vaultID = ctx.tx[sfVaultID];
    auto const manager = ctx.view.read(keylet::vaultManager(vaultID));
    if (!manager || !ctx.view.exists(keylet::vault(vaultID)))
        return tecNO_ENTRY;
    if (ctx.tx[sfAccount] != effectiveVaultRoles(ctx.view, manager).valuer)
        return tecNO_PERMISSION;
    if (!ctx.view.exists(keylet::vaultPosition(vaultID, ctx.tx[sfAsset])))
        return tecNO_ENTRY;
    return tesSUCCESS;
}

TER
VaultMark::doApply()
{
    auto const vaultID = ctx_.tx[sfVaultID];
    auto vault = view().peek(keylet::vault(vaultID));
    auto manager = view().peek(keylet::vaultManager(vaultID));
    auto position = view().peek(keylet::vaultPosition(vaultID, ctx_.tx[sfAsset]));
    if (!vault || !manager || !position)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    if (auto const ter = applyDuePendingUpdate(view(), manager, vault, j_); !isTesSuccess(ter))
        return ter;

    auto const now = view().parentCloseTime().time_since_epoch().count();
    Number const previous = position->at(sfMarkPrice);
    Number const next = ctx_.tx[sfMarkPrice];

    // The mark is written either way. A move beyond the limit halts dealing
    // for the period instead, so nobody deals against a price that just
    // moved violently, and the price stays honest.
    Number const move = (next > previous ? next - previous : previous - next) *
        Number{kTenthBipsPerUnity.value()} / previous;
    if (move > Number{manager->at(sfMarkMoveLimit)})
    {
        std::uint32_t const until = now + manager->at(sfMarkHaltPeriod);
        if (until > vault->at(sfMarkHaltUntil))
        {
            vault->at(sfMarkHaltUntil) = until;
            view().update(vault);
        }
    }

    position->at(sfMarkPrice) = next;
    position->at(sfLastMarkTime) = now;
    view().update(position);
    return tesSUCCESS;
}

void
VaultMark::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
}

bool
VaultMark::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
