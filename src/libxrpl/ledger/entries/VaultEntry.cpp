#include <xrpl/ledger/entries/VaultEntry.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>

namespace xrpl {

template <typename ViewT>
VaultVersion
VaultEntry<ViewT>::version() const
{
    XRPL_ASSERT(
        *this && (**this).getType() == ltVAULT, "xrpl::VaultEntry::version : valid Vault sle");
    return decodeVaultVersion((**this).at(~sfLEVersion));
}

template class VaultEntry<ReadView>;
template class VaultEntry<ApplyView>;

}  // namespace xrpl
