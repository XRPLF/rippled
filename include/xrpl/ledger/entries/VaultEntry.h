#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/Units.h>

#include <expected>

namespace xrpl {

// Defined in VaultHelpers.h, which includes this header.
enum class WaiveUnrealizedLoss : bool;

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
     * Resolves the Vault's LEVersion, the single point every accounting touch
     * point should call to determine which recognition model (instant
     * interest recognition vs. cash-basis) the Vault uses. Vaults created
     * before featureLendingProtocolV1_1 activated never have sfLEVersion set,
     * which resolves here to VaultVersion::Legacy.
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
     * Adjusts a requested asset change (`delta`) to match the decimal scale of the
     * updated total vault assets. This ensures `sfAssetsTotal`, `sfAssetsAvailable`,
     * and the actual asset transfer change by the exact same representable amount.
     *
     * Rounding strategy:
     * - Debits (withdrawals): Rounds down `|delta|` on the new scale to prevent
     *   paying out more than requested.
     * - Credits (deposits): Floors the resulting total asset balance and returns the
     *   difference from the current total. This prevents crediting the vault with
     *   more assets than the user deposited.
     *
     * Key rules:
     * - The returned magnitude never exceeds `|delta|`.
     * - Returns `tecPRECISION_LOSS` if the change is smaller than 1 ULP of the target scale
     *   (prevents share operations when totals cannot change).
     * - For integer assets (XRP, MPT), rounding is a no-op.
     *
     * @param delta The requested signed change to sfAssetsTotal.
     * @return The rounded, positive magnitude, or `tecPRECISION_LOSS` if the
     *         change is below representable precision.
     */
    [[nodiscard]] std::expected<STAmount, TER>
    clampToAssetsTotalScale(STAmount const& delta) const;

    /**
     * Returns the assets backing outstanding shares for a withdrawal:
     * sfAssetsTotal minus sfLossUnrealized, or sfAssetsTotal alone when the
     * unrealized loss is waived. Used by assetsToSharesWithdraw and
     * sharesToAssetsWithdraw as the numerator of the share/asset exchange
     * rate.
     *
     * @param waive Whether to skip subtracting the unrealized loss.
     * @return The assets backing outstanding shares for a withdrawal.
     */
    [[nodiscard]] Number
    assetsTotalForWithdrawal(WaiveUnrealizedLoss waive) const;

    /**
     * Returns the scale of the vault's sfAssetsTotal for its asset, or
     * Number::kMinExponent - 1 if the entry does not exist.
     *
     * @return The scale of the vault's sfAssetsTotal, or Number::kMinExponent
     * - 1 if the entry does not exist.
     */
    [[nodiscard]] int
    assetsTotalScale() const
    {
        if (!*this)
            return Number::kMinExponent - 1;  // LCOV_EXCL_LINE
        return scale((*this)->at(sfAssetsTotal), (*this)->at(sfAsset));
    }

    /**
     * Computes the minimum required broker cover, rounded consistently.
     * DebtTotal is a broker-level aggregate maintained at vault scale, so the
     * rounding must also use vault scale — never an individual loan's scale.
     *
     * @param debtTotal The broker's total outstanding debt, at vault scale.
     * @param coverRateMinimum The minimum cover rate to apply to debtTotal.
     * @return The minimum required broker cover, rounded up to vault scale.
     */
    [[nodiscard]] Number
    minimumBrokerCover(Number const& debtTotal, TenthBips32 coverRateMinimum) const
    {
        XRPL_ASSERT(
            *this && (*this)->getType() == ltVAULT,
            "xrpl::VaultEntry::minimumBrokerCover : valid Vault sle");
        NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
        return roundToAsset(
            (*this)->at(sfAsset),
            tenthBipsOfValue(debtTotal, coverRateMinimum),
            assetsTotalScale());
    }
};

using VaultEntryR = VaultEntry<ReadView>;
using VaultEntryW = VaultEntry<ApplyView>;

}  // namespace xrpl
