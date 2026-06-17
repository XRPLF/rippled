#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

namespace xrpl {
namespace credentials {

/**
 * @brief Check whether a credential has expired.
 *
 * @param sleCredential Credential ledger entry to inspect.
 * @param closed Parent close time to compare against sfExpiration.
 * @return true if sfExpiration is before closed; otherwise false.
 */
bool
checkExpired(SLE const& sleCredential, NetClock::time_point const& closed);

/**
 * @brief Remove a Credential ledger object from the view.
 *
 * @param view Mutable ledger view.
 * @param sleCredential Credential ledger entry to delete.
 * @param j Journal for diagnostics.
 * @return tesSUCCESS on success, or an error code if ledger cleanup fails.
 */
[[nodiscard]] TER
deleteSLE(ApplyView& view, SLE::ref sleCredential, beast::Journal j);

/**
 * @brief Check amendment and parameter rules for sfCredentialIDs.
 *
 * @param tx Transaction to validate.
 * @param j Journal for diagnostics.
 * @return tesSUCCESS if sfCredentialIDs is absent or well-formed; otherwise a
 *         malformed transaction code.
 */
NotTEC
checkFields(STTx const& tx, beast::Journal j);

/**
 * @brief Check whether the transaction's provided credentials are valid.
 *
 * Use this only from preclaim because it does not remove expired credentials.
 * If this is called from preclaim, verifyDepositPreauth() must also be called
 * from doApply().
 *
 * @param tx Transaction containing optional sfCredentialIDs.
 * @param view Read-only ledger view.
 * @param src Source account that must own the credentials.
 * @param j Journal for diagnostics.
 * @return tesSUCCESS if credentials are valid, otherwise a credential error.
 */
TER
valid(STTx const& tx, ReadView const& view, AccountID const& src, beast::Journal j);

/**
 * @brief Check whether an account has an accepted credential for a domain.
 *
 * If this is called from preclaim and returns tecEXPIRED, verifyValidDomain()
 * should be called from doApply() so expired credentials are deleted.
 *
 * @param view Read-only ledger view.
 * @param domainID PermissionedDomain ledger entry ID.
 * @param subject Account whose credentials are checked.
 * @return tesSUCCESS if a matching accepted credential exists, tecEXPIRED if
 *         only expired credentials match, or an authorization/object error.
 */
TER
validDomain(ReadView const& view, uint256 domainID, AccountID const& subject);

/**
 * @brief Check DepositPreauth authorization using credential IDs.
 *
 * @param view Read-only ledger view.
 * @param ctx Credential IDs supplied by the transaction.
 * @param dst Destination account requiring deposit authorization.
 * @return tesSUCCESS if credentials match a DepositPreauth object, otherwise
 *         an authorization or internal error.
 */
TER
authorizedDepositPreauth(ReadView const& view, STVector256 const& ctx, AccountID const& dst);

/**
 * @brief Sort credential issuer/type pairs.
 *
 * @param credentials Credential descriptors from a transaction.
 * @return Sorted issuer/type pairs, or an empty set if duplicates exist.
 */
std::set<std::pair<AccountID, Slice>>
makeSorted(STArray const& credentials);

/**
 * @brief Check a credentials array for DepositPreauth or PermissionedDomainSet.
 *
 * @param credentials Credential descriptors to validate.
 * @param maxSize Maximum allowed array size.
 * @param j Journal for diagnostics.
 * @return tesSUCCESS if the array is well-formed, otherwise a malformed
 *         transaction code.
 */
NotTEC
checkArray(STArray const& credentials, unsigned maxSize, beast::Journal j);

}  // namespace credentials

/**
 * @brief Verify an account has a credential for a domain and delete expired
 * credentials encountered while checking.
 *
 * @param view Mutable ledger view.
 * @param account Account whose credentials are checked.
 * @param domainID PermissionedDomain ledger entry ID.
 * @param j Journal for diagnostics.
 * @return tesSUCCESS if a matching accepted credential exists, tecEXPIRED if
 *         expired credentials were removed but no valid credential remains, or
 *         an authorization/object error.
 */
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

/**
 * @brief Remove expired credentials, then check DepositPreauth authorization.
 *
 * @param tx Transaction containing optional credential IDs.
 * @param view Mutable ledger view.
 * @param src Source account.
 * @param dst Destination account.
 * @param sleDst Destination AccountRoot, if it exists.
 * @param j Journal for diagnostics.
 * @return tesSUCCESS if the deposit is authorized, otherwise an expiration,
 *         authorization, or deletion error.
 */
TER
verifyDepositPreauth(
    STTx const& tx,
    ApplyView& view,
    AccountID const& src,
    AccountID const& dst,
    SLE::const_ref sleDst,
    beast::Journal j);

}  // namespace xrpl
