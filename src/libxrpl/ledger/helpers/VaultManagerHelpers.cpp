#include <xrpl/ledger/helpers/VaultManagerHelpers.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

namespace xrpl {

TER
checkVaultRoles(ReadView const& view, SLE::const_ref vault, VaultRoles const& roles)
{
    if (roles.manager == roles.valuer || roles.manager == roles.guardian ||
        roles.valuer == roles.guardian)
        return tecROLE_CONFLICT;

    AccountID const owner = vault->at(sfOwner);
    if (roles.valuer == owner || roles.guardian == owner)
        return tecROLE_CONFLICT;

    if (roles.manager == owner && vaultHasLoanBroker(view, vault))
        return tecROLE_CONFLICT;

    return tesSUCCESS;
}

bool
vaultHasLoanBroker(ReadView const& view, SLE::const_ref vault)
{
    auto const root = keylet::ownerDir(vault->at(sfAccount)).key;
    SLE::const_pointer page;
    unsigned int index = 0;
    uint256 entry;
    for (bool more = cdirFirst(view, root, page, index, entry); more;
         more = cdirNext(view, root, page, index, entry))
    {
        if (auto const sle = view.read(keylet::unchecked(entry));
            sle && sle->getType() == ltLOAN_BROKER)
            return true;
    }
    return false;
}

}  // namespace xrpl
