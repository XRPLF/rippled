#ifndef XRPL_APP_MISC_DELEGATEUTILS_H_INCLUDED
#define XRPL_APP_MISC_DELEGATEUTILS_H_INCLUDED

#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

namespace ripple {

/**
 * Check if the delegate account has permission to execute the transaction.
 * @param delegate The delegate account.
 * @param tx The transaction that the delegate account intends to execute.
 * @return tesSUCCESS if the transaction is allowed, terNO_DELEGATE_PERMISSION
 * if not.
 */
NotTEC
checkTxPermission(std::shared_ptr<SLE const> const& delegate, STTx const& tx);

/**
 * Load the granular permissions granted to the delegate account for the
 * specified transaction type
 * @param delegate The delegate account.
 * @param type Used to determine which granted granular permissions to load,
 * based on the transaction type.
 * @param granularPermissions Granted granular permissions tied to the
 * transaction type.
 */
void
loadGranularPermission(
    std::shared_ptr<SLE const> const& delegate,
    TxType const& type,
    std::unordered_set<GranularPermissionType>& granularPermissions);

}  // namespace ripple

#endif  // XRPL_APP_MISC_DELEGATEUTILS_H_INCLUDED
