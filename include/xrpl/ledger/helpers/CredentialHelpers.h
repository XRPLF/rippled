/** @file
 *  Central contract for credential and deposit pre-authorization logic.
 *
 *  Included by every fund-transfer transactor (Payment, EscrowFinish,
 *  PaymentChannelClaim, VaultDeposit) that must honor destination-account
 *  access controls.
 *
 *  Functions divide along the preclaim / doApply boundary:
 *  - `xrpl::credentials::*` — read-only checks safe to call from preclaim.
 *  - `xrpl::verifyDepositPreauth` / `xrpl::verifyValidDomain` — mutating
 *    counterparts that must be called from doApply when the corresponding
 *    preclaim function succeeds, so that expired credential objects are
 *    physically deleted from the ledger as a side effect.
 */
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

/** Test whether a credential SLE has passed its expiration time.
 *
 *  Reads `sfExpiration` from @p sleCredential, defaulting to
 *  `std::numeric_limits<uint32_t>::max()` when the field is absent, so
 *  credentials with no expiration field never expire.
 *
 *  @param sleCredential  The credential SLE to inspect.
 *  @param closed         The parent ledger's close time.  Must be a
 *      NetClock epoch value — do not pass wall-clock time.
 *  @return `true` if the credential has expired, `false` otherwise.
 */
bool
checkExpired(SLE const& sleCredential, NetClock::time_point const& closed);

/** Remove a credential SLE and its entries from both owner directories.
 *
 *  A credential is indexed in two owner directories — the issuer's and the
 *  subject's.  Reserve-count accounting depends on acceptance state:
 *  - Before acceptance (`lsfAccepted` unset): only the issuer holds the
 *    reserve; only the issuer's count is decremented.
 *  - After acceptance with distinct accounts: the subject holds the reserve
 *    and its count is decremented.
 *  - When issuer and subject are the same account, only one directory
 *    removal is performed.
 *
 *  @note Paths indicating ledger corruption (missing account SLE, failed
 *      `dirRemove`) are marked `LCOV_EXCL` and are unreachable under normal
 *      operation.
 *
 *  @param view           Mutable ledger view through which the SLE is erased.
 *  @param sleCredential  The credential SLE to delete; must not be null.
 *  @param j              Journal for fatal-level error logging.
 *  @return `tesSUCCESS` on success; `tecNO_ENTRY` if @p sleCredential is
 *      null; `tecINTERNAL` or `tefBAD_LEDGER` on internal directory
 *      inconsistency.
 */
[[nodiscard]] TER
deleteSLE(ApplyView& view, std::shared_ptr<SLE> const& sleCredential, beast::Journal j);

/** Validate the `sfCredentialIDs` field of a transaction at preflight time.
 *
 *  Enforces non-empty, at most `kMAX_CREDENTIALS_ARRAY_SIZE` entries, and no
 *  duplicate hashes.  Returns `tesSUCCESS` immediately when `sfCredentialIDs`
 *  is absent, as credentials are optional for most transaction types.
 *
 *  @param tx  The transaction under preflight validation.
 *  @param j   Journal for trace-level malformed-transaction logging.
 *  @return `tesSUCCESS` if the field is absent or valid; `temMALFORMED` if
 *      the array is empty, too large, or contains duplicates.
 */
NotTEC
checkFields(STTx const& tx, beast::Journal j);

/** Verify that all credentials in a transaction exist, are owned by the
 *  sender, and have been accepted — for use in preclaim only.
 *
 *  Checks each ID in `sfCredentialIDs`: the SLE must exist, its `sfSubject`
 *  must equal @p src, and `lsfAccepted` must be set.  Expiration is
 *  deliberately not checked here; expired credentials are deleted in doApply
 *  by `verifyDepositPreauth` or `verifyValidDomain`.
 *
 *  @note If this returns `tesSUCCESS` in preclaim, the caller must invoke
 *      `verifyDepositPreauth` in doApply to garbage-collect any credentials
 *      that expire before the enclosing transaction applies.
 *
 *  @param tx    The transaction whose `sfCredentialIDs` field is inspected.
 *  @param view  Read-only ledger view for SLE lookups.
 *  @param src   The account that must own every listed credential.
 *  @param j     Journal for trace-level logging.
 *  @return `tesSUCCESS` if `sfCredentialIDs` is absent or all credentials are
 *      valid; `tecBAD_CREDENTIALS` if any credential is missing, belongs to a
 *      different account, or has not been accepted.
 */
TER
valid(STTx const& tx, ReadView const& view, AccountID const& src, beast::Journal j);

/** Check whether @p subject holds a live, accepted credential for a
 *  permissioned domain — for use in preclaim only.
 *
 *  Reads the `PermissionedDomain` SLE, iterates its `sfAcceptedCredentials`
 *  array, and looks up the corresponding credential SLE for @p subject.
 *  A credential qualifies when it exists, has not expired, and carries
 *  `lsfAccepted`.
 *
 *  Because a `ReadView` is immutable, expired credentials cannot be deleted
 *  here.  The function returns `tecEXPIRED` when all matching credentials
 *  are expired — signaling the caller that the condition may resolve in
 *  doApply where `verifyValidDomain` will physically remove them.
 *
 *  @note If this returns `tecEXPIRED` in preclaim, the caller must invoke
 *      `verifyValidDomain` in doApply so that expired objects are
 *      garbage-collected even if the transaction ultimately fails.
 *
 *  @param view      Read-only ledger view.
 *  @param domainID  Key of the `PermissionedDomain` SLE to check against.
 *  @param subject   Account that must hold a qualifying credential.
 *  @return `tesSUCCESS` if a live accepted credential exists; `tecEXPIRED`
 *      if only expired credentials were found; `tecNO_AUTH` if no matching
 *      credential exists; `tecOBJECT_NOT_FOUND` if the domain does not exist.
 */
TER
validDomain(ReadView const& view, uint256 domainID, AccountID const& subject);

/** Check whether a set of credential IDs matches a credential-set
 *  `DepositPreauth` entry for the destination account.
 *
 *  Builds a sorted `std::set<std::pair<AccountID, Slice>>` of
 *  `(issuer, credentialType)` pairs from @p credIDs and tests for the
 *  existence of the corresponding `keylet::depositPreauth(dst, sorted)`.
 *  The sorted representation matches the canonical key used at
 *  `DepositPreauth` creation time.
 *
 *  @note Credential existence is assumed to have been confirmed in preclaim.
 *      A missing SLE here indicates an internal consistency error.
 *  @note `Slice` members in the internal sorted set are non-owning views
 *      into SLE storage.  A `lifeExtender` vector keeps the SLEs alive for
 *      the duration of the lookup.
 *
 *  @param view     Read-only ledger view for SLE and keylet lookups.
 *  @param credIDs  The `sfCredentialIDs` vector from the transaction.
 *  @param dst      The destination account whose `DepositPreauth` is checked.
 *  @return `tesSUCCESS` if a matching `DepositPreauth` object exists;
 *      `tecNO_PERMISSION` if none exists; `tefINTERNAL` if a credential SLE
 *      is unexpectedly missing or a duplicate pair is encountered.
 */
TER
authorizedDepositPreauth(ReadView const& view, STVector256 const& ctx, AccountID const& dst);

/** Build a sorted `(issuer, credentialType)` set from a credentials array.
 *
 *  Produces the canonical representation used to key `DepositPreauth`
 *  objects.  Each element of @p credentials must carry `sfIssuer` and
 *  `sfCredentialType`.
 *
 *  @param credentials  An `STArray` of credential pairs, as stored in a
 *      `DepositPreauth` or `PermissionedDomainSet` transaction.
 *  @return A sorted set of `(AccountID, Slice)` pairs; an empty set if any
 *      duplicate `(issuer, credentialType)` pair is detected.
 */
std::set<std::pair<AccountID, Slice>>
makeSorted(STArray const& credentials);

/** Validate a credential array in `DepositPreauth` or
 *  `PermissionedDomainSet` transactions at preflight time.
 *
 *  Credentials in these transactions are `(issuer, credentialType)` pairs
 *  rather than object hashes.  Enforces: non-empty; at most @p maxSize
 *  entries; valid issuer `AccountID`; `sfCredentialType` length in
 *  `[1, kMAX_CREDENTIAL_TYPE_LENGTH]` bytes; and no logical duplicates
 *  (detected via `sha512Half(issuer, credentialType)`).
 *
 *  @param credentials  The `STArray` of credential pairs to validate.
 *  @param maxSize      Maximum permitted array length (caller-supplied per
 *      transaction type).
 *  @param j            Journal for trace-level malformed-transaction logging.
 *  @return `tesSUCCESS` if all entries are valid; `temARRAY_EMPTY`,
 *      `temARRAY_TOO_LARGE`, `temINVALID_ACCOUNT_ID`, or `temMALFORMED`
 *      on the first constraint violation found.
 */
NotTEC
checkArray(STArray const& credentials, unsigned maxSize, beast::Journal j);

}  // namespace credentials

/** Enforce domain-credential authorization in doApply, deleting expired
 *  credentials as a side effect.
 *
 *  The doApply counterpart to `credentials::validDomain`.  Collects all
 *  credential SLEs for @p account that match the `sfAcceptedCredentials`
 *  list of the `PermissionedDomain` at @p domainID, calls
 *  `credentials::removeExpired` to physically delete any that have expired,
 *  then re-checks whether at least one live, accepted credential remains.
 *
 *  The two-pass design (collect → expire → re-validate) ensures expired
 *  objects are garbage-collected even when the surrounding transaction
 *  ultimately fails.
 *
 *  @param view      Mutable ledger view; expired credential SLEs are erased.
 *  @param account   Account whose credentials are being verified.
 *  @param domainID  Key of the `PermissionedDomain` SLE.
 *  @param j         Journal for trace/error logging.
 *  @return `tesSUCCESS` if a live accepted credential for the domain exists;
 *      `tecEXPIRED` if only expired credentials were found; `tecNO_PERMISSION`
 *      if no matching credential exists; `tecOBJECT_NOT_FOUND` if the domain
 *      SLE is missing; or a propagated `TER` error from `removeExpired` under
 *      `fixCleanup3_1_3`.
 */
TER
verifyValidDomain(ApplyView& view, AccountID const& account, uint256 domainID, beast::Journal j);

/** Enforce deposit pre-authorization in doApply, deleting expired credentials
 *  as a side effect.
 *
 *  Called by Payment, EscrowFinish, and PaymentChannelClaim when the
 *  destination account has `lsfDepositAuth` set.  Authorization succeeds
 *  when any of the following hold:
 *  - `src == dst` (self-payments are always allowed).
 *  - `keylet::depositPreauth(dst, src)` exists (account-level pre-auth).
 *  - A credential-set `DepositPreauth` object exists for the credentials
 *    submitted via `sfCredentialIDs` (via `credentials::authorizedDepositPreauth`).
 *
 *  If `sfCredentialIDs` is present, `credentials::removeExpired` is called
 *  unconditionally before the authorization tests.  If any credential was
 *  expired, `tecEXPIRED` is returned immediately without attempting
 *  authorization.
 *
 *  @param tx      The transaction under doApply; may carry `sfCredentialIDs`.
 *  @param view    Mutable ledger view; expired credential SLEs may be erased.
 *  @param src     The sending account.
 *  @param dst     The destination account.
 *  @param sleDst  The destination account's SLE, used to test `lsfDepositAuth`.
 *      If null, `lsfDepositAuth` is treated as unset and the function returns
 *      `tesSUCCESS`.
 *  @param j       Journal for trace/error logging.
 *  @return `tesSUCCESS` if authorized or `lsfDepositAuth` is not set;
 *      `tecEXPIRED` if submitted credentials have expired;
 *      `tecNO_PERMISSION` if no matching pre-authorization exists; or a
 *      propagated error from `removeExpired` or `authorizedDepositPreauth`.
 */
TER
verifyDepositPreauth(
    STTx const& tx,
    ApplyView& view,
    AccountID const& src,
    AccountID const& dst,
    std::shared_ptr<SLE const> const& sleDst,
    beast::Journal j);

}  // namespace xrpl
