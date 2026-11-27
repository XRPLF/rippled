#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>

#include <expected>

namespace xrpl {

template <typename ViewT>
class VaultEntry : public SLEBase<ViewT, ltVAULT>
{
public:
    using Base = SLEBase<ViewT, ltVAULT>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit VaultEntry(
        AccountID const& owner,
        SeqProxy const& seq,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::vault(owner, seq), view, j)
    {
    }

    explicit VaultEntry(
        UInt256 const& vaultID,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::vault(vaultID), view, j)
    {
    }

    /**
     * Resolves the Vault's LEVersion.
     *
     * LEVersion is the single point every accounting and rounding helper
     * should call to decide which protocol a Vault follows. It is written
     * at VaultCreate and is not updated afterwards, so a Vault created
     * under an older amendment keeps that behaviour after later amendments
     * activate. Absent sfLEVersion resolves to VaultVersion::Legacy.
     *
     * @return The Vault's LEVersion, or VaultVersion::Legacy if the field is
     * absent.
     */
    [[nodiscard]] VaultVersion
    version() const;

    /**
     * Resolves the Vault's VaultKind. Returns VaultKind::ClosedEnded when
     * sfVaultKind is present and equal to that value; anything else
     * (including an absent field or an unrecognised value) is treated as
     * VaultKind::OpenEnded.
     *
     * @return The Vault's VaultKind.
     */
    [[nodiscard]] VaultKind
    kind() const;

    /**
     * Returns the current lifecycle phase of the vault. Open-ended vaults are
     * always NoPhase. For closed-ended vaults the phase is derived from the
     * parent close time of the entry's view and the vault's immutable
     * SubscriptionDate and RedemptionDate.
     *
     * @return The vault's current VaultPhase.
     */
    [[nodiscard]] VaultPhase
    phase() const;

    /**
     * Adjusts a requested asset change (delta) to match the decimal scale of the
     * updated total vault assets. This ensures sfAssetsTotal, sfAssetsAvailable,
     * and the actual asset transfer change by the exact same representable amount.
     *
     * Rounding strategy:
     * - Legacy/CashBasis debits (withdrawals): rounds down the magnitude of delta
     *   on the new scale to prevent paying out more than requested.
     * - Legacy/CashBasis credits: floors the resulting total asset balance and
     *   returns the difference from the current total.
     * - FixedPrecision credits only (VaultDeposit): rounds the delta toward zero
     *   at the scale getAssetsTotal (AssetsAvailable + AssetsDeployed, derived,
     *   not the stored AssetsTotal cache) would have after applying delta.
     *   FixedPrecision outflows (VaultWithdraw, VaultClawback) never call this;
     *   they use clampVaultOutflow instead.
     *
     * Key rules:
     * - The returned magnitude never exceeds the magnitude of delta.
     * - Returns tecPRECISION_LOSS if the change is smaller than 1 ULP of the target scale
     *   (prevents share operations when totals cannot change).
     * - For integer assets (XRP, MPT), rounding is a no-op.
     *
     * @param delta The requested signed change to sfAssetsTotal.
     * @return The rounded, positive magnitude, or tecPRECISION_LOSS if the
     *         change is below representable precision.
     */
    [[nodiscard]] std::expected<STAmount, TER>
    clampToAssetsTotalScale(STAmount const& delta) const;
};

using VaultEntryR = VaultEntry<ReadView>;
using VaultEntryW = VaultEntry<ApplyView>;

}  // namespace xrpl
