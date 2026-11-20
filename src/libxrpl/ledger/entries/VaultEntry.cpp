#include <xrpl/ledger/entries/VaultEntry.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>

#include <expected>
#include <utility>

namespace xrpl {

template <typename ViewT>
VaultVersion
VaultEntry<ViewT>::version() const
{
    XRPL_ASSERT(
        *this && (**this).getType() == ltVAULT, "xrpl::VaultEntry::version : valid Vault sle");
    return decodeVaultVersion((**this).at(~sfLEVersion));
}

template <typename ViewT>
VaultKind
VaultEntry<ViewT>::kind() const
{
    XRPL_ASSERT(*this && (*this)->getType() == ltVAULT, "xrpl::VaultEntry::kind : valid Vault sle");
    auto const vaultKind = (*this)->at(~sfVaultKind);
    if (vaultKind && *vaultKind == std::to_underlying(VaultKind::ClosedEnded))
        return VaultKind::ClosedEnded;
    return VaultKind::OpenEnded;
}

template <typename ViewT>
VaultPhase
VaultEntry<ViewT>::phase() const
{
    XRPL_ASSERT(
        *this && (*this)->getType() == ltVAULT, "xrpl::VaultEntry::phase : valid Vault sle");
    return getVaultPhase(
        this->readView(),
        (**this)[~sfVaultKind],
        (**this)[~sfSubscriptionDate],
        (**this)[~sfRedemptionDate]);
}

template <typename ViewT>
std::expected<STAmount, TER>
VaultEntry<ViewT>::clampToAssetsTotalScale(STAmount const& delta) const
{
    return xrpl::clampToAssetsTotalScale(this->rawSle(), delta);
}

template class VaultEntry<ReadView>;
template class VaultEntry<ApplyView>;

}  // namespace xrpl
