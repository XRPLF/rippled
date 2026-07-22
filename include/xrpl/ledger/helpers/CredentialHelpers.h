#pragma once

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/TER.h>

#include <memory>
#include <set>
#include <utility>

namespace xrpl {
namespace credentials {

// These function will be used by the code that use DepositPreauth / Credentials
// (and any future pre-authorization modes) as part of authorization (all the
// transfer funds transactions)

// Check if credential sfExpiration field has passed ledger's parentCloseTime
bool
checkExpired(SLE const& sleCredential, NetClock::time_point const& closed);

// Actually remove a credentials object from the ledger
[[nodiscard]] TER
deleteSLE(ApplyView& view, SLE::ref sleCredential, beast::Journal j);

// Amendment and parameters checks for sfCredentialIDs field
NotTEC
checkFields(STTx const& tx, beast::Journal j);

// Accessing the ledger to check if provided credentials are valid. Do not use
// in doApply (only in preclaim) since it does not remove expired credentials.
// If you call it in preclaim, you also must call verifyDepositPreauth in
// doApply
TER
valid(STTx const& tx, ReadView const& view, AccountID const& src, beast::Journal j);

// Check if subject has any credential matching the given domain. If you call it
// in preclaim and it returns tecEXPIRED, you should call verifyValidDomain in
// doApply. This will ensure that expired credentials are deleted.
TER
validDomain(ReadView const& view, uint256 domainID, AccountID const& subject);

// This function is only called when we are about to return tecNO_PERMISSION
// because all the checks for the DepositPreauth authorization failed.
TER
authorizedDepositPreauth(ReadView const& view, STVector256 const& ctx, AccountID const& dst);

// Sort credentials array, return empty set if there are duplicates
std::set<std::pair<AccountID, Slice>>
makeSorted(STArray const& credentials);

// Check credentials array passed to DepositPreauth/PermissionedDomainSet
// transactions
NotTEC
checkArray(STArray const& credentials, unsigned maxSize, beast::Journal j);

}  // namespace credentials

// Check expired credentials and for credentials matching DomainID of the ledger
// object
TER
verifyValidDomain(ApplyView& view, AccountID const& account, uint256 domainID, beast::Journal j);

/**
 * @brief Check whether src is authorized to deposit to dst.
 *
 * @param tx Transaction containing optional credential IDs.
 * @param view Read-only ledger view.
 * @param src Source account.
 * @param dst Destination account.
 * @param sleDst Destination AccountRoot, if it exists.
 * @param j Journal for diagnostics.
 * @return tesSUCCESS if the deposit is allowed, otherwise an authorization
 *         error.
 */
TER
checkDepositPreauth(
    STTx const& tx,
    ReadView const& view,
    AccountID const& src,
    AccountID const& dst,
    std::shared_ptr<SLE const> const& sleDst,
    beast::Journal j);

/**
 * @brief Remove expired credentials referenced by the transaction.
 *
 * @param tx Transaction containing optional sfCredentialIDs.
 * @param view Mutable ledger view.
 * @param j Journal for diagnostics.
 * @return tesSUCCESS if no referenced credentials expired, tecEXPIRED if any
 *         were removed, or an error from credential deletion.
 */
TER
cleanupExpiredCredentials(STTx const& tx, ApplyView& view, beast::Journal j);

// Check expired credentials and for existing DepositPreauth ledger object
TER
verifyDepositPreauth(
    STTx const& tx,
    ApplyView& view,
    AccountID const& src,
    AccountID const& dst,
    SLE::const_ref sleDst,
    beast::Journal j);

}  // namespace xrpl
