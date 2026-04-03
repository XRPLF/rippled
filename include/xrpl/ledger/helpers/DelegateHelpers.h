#pragma once

#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

namespace xrpl {

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
std::unordered_set<GranularPermissionType>
getGranularPermission(std::shared_ptr<SLE const> const& delegate, TxType const& type);

/**
 * This function extracts the granular permissions for the given transaction type and
 * then enforces the granular sandbox, defined in permissions.macro. Coupling the retrieval and
 * validation steps ensures the sandbox check cannot be accidentally bypassed by the caller.
 *
 * @param delegate The delegate ledger object.
 * @param tx The transaction to validate.
 * @return The set of held granular permissions if the sandbox check passes; Returns std::nullopt if
 * no relevant granular permissions are held, or if the transaction violates the sandbox.
 */
std::optional<std::unordered_set<GranularPermissionType>>
checkGranularPermission(std::shared_ptr<SLE const> const& delegate, STTx const& tx);

}  // namespace xrpl
