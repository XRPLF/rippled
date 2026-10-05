#include <xrpl/ledger/entries/VaultEntry.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>

#include <utility>

namespace xrpl {

template <typename ViewT>
VaultVersion
VaultEntry<ViewT>::version() const
{
    XRPL_ASSERT(
        *this && (*this)->getType() == ltVAULT, "xrpl::VaultEntry::version : valid Vault sle");
    if (!(*this)->isFieldPresent(sfLEVersion))
        return VaultVersion::Legacy;

    auto const version = (*this)->at(sfLEVersion);
    if (version > std::to_underlying(VaultVersion::CashBasis))
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::VaultEntry::version : invalid vault version");
        return VaultVersion::Legacy;
        // LCOV_EXCL_STOP
    }
    return static_cast<VaultVersion>(version);
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

template class VaultEntry<ReadView>;
template class VaultEntry<ApplyView>;

}  // namespace xrpl
