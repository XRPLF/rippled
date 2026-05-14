#include <xrpl/ledger/helpers/CredentialHelpers.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/digest.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <set>
#include <unordered_set>
#include <utility>
#include <vector>

namespace xrpl {
namespace credentials {

/** Test whether a credential SLE has passed its expiration time.
 *
 *  Reads `sfExpiration` from @p sleCredential, defaulting to
 *  `std::numeric_limits<uint32_t>::max()` when the field is absent, so
 *  credentials with no expiration field never expire.  The comparison is
 *  against `closed.time_since_epoch().count()`, matching the NetClock
 *  epoch used by `view.header().parentCloseTime`.
 *
 *  @param sleCredential The credential SLE to inspect.
 *  @param closed        The parent ledger's close time (deterministic across
 *      all validators; do not pass wall-clock time).
 *  @return `true` if the credential has expired, `false` otherwise.
 */
bool
checkExpired(SLE const& sleCredential, NetClock::time_point const& closed)
{
    std::uint32_t const exp =
        sleCredential[~sfExpiration].value_or(std::numeric_limits<std::uint32_t>::max());
    std::uint32_t const now = closed.time_since_epoch().count();
    return now > exp;
}

/** Delete all expired credentials from a vector of credential keys.
 *
 *  Iterates @p arr and, for each key whose SLE exists and has expired (per
 *  `checkExpired`), calls `deleteSLE` to remove the SLE from the ledger and
 *  both owner directories.  Deletion happens unconditionally — even if the
 *  enclosing transaction ultimately fails — acting as passive garbage
 *  collection for stale credential objects.
 *
 *  Existence and ownership of each credential are assumed to have been
 *  verified in preclaim; only expiration is re-checked here.
 *
 *  Under `fixSecurity3_1_3`, any `deleteSLE` failure is propagated as an
 *  error; prior to that amendment, deletion errors are silently ignored and
 *  the loop continues.
 *
 *  @param view  Mutable ledger view; credential SLEs are erased through it.
 *  @param arr   Vector of credential keylet hashes to inspect.
 *  @param j     Journal for trace/error logging.
 *  @return `Expected<bool, TER>`: holds `true` if any expired credential was
 *      found and deleted, `false` if none were expired, or an unexpected `TER`
 *      error if deletion failed under `fixSecurity3_1_3`.
 */
[[nodiscard]]
static Expected<bool, TER>
removeExpired(ApplyView& view, STVector256 const& arr, beast::Journal const j)
{
    auto const closeTime = view.header().parentCloseTime;
    bool foundExpired = false;

    for (auto const& h : arr)
    {
        // Credentials already checked in preclaim. Look only for expired here.
        auto const k = keylet::credential(h);
        auto const sleCred = view.peek(k);

        if (sleCred && checkExpired(*sleCred, closeTime))
        {
            JLOG(j.trace()) << "Credentials are expired. Cred: " << sleCred->getText();
            // delete expired credentials even if the transaction failed
            auto const err = deleteSLE(view, sleCred, j);
            if (view.rules().enabled(fixSecurity3_1_3) && !isTesSuccess(err))
                return Unexpected(err);
            foundExpired = true;
        }
    }

    return foundExpired;
}

/** Remove a credential SLE and its entries from both owner directories.
 *
 *  A credential is indexed in two owner directories — the issuer's and the
 *  subject's — using the `sfIssuerNode` and `sfSubjectNode` page offsets
 *  stored in the SLE.  Both entries must be removed and owner-count reserves
 *  adjusted correctly.
 *
 *  Reserve accounting follows credential acceptance state:
 *  - Before acceptance (`lsfAccepted` unset): only the issuer holds the
 *    reserve, so `adjustOwnerCount(-1)` is applied to the issuer only.
 *  - After acceptance: the subject holds the reserve, so adjustment shifts
 *    to the subject side.
 *  - When issuer and subject are the same account, only one directory
 *    removal is performed.
 *
 *  Paths that indicate ledger corruption (missing account SLE or failed
 *  `dirRemove`) are wrapped in `LCOV_EXCL_START`/`STOP` and are considered
 *  unreachable under normal operation.
 *
 *  @param view            Mutable ledger view through which the SLE is erased.
 *  @param sleCredential   The credential SLE to delete; must not be null.
 *  @param j               Journal for fatal-level error logging.
 *  @return `tesSUCCESS` on success, `tecNO_ENTRY` if `sleCredential` is null,
 *      `tecINTERNAL` or `tefBAD_LEDGER` on internal directory inconsistency.
 */
TER
deleteSLE(ApplyView& view, std::shared_ptr<SLE> const& sleCredential, beast::Journal j)
{
    if (!sleCredential)
        return tecNO_ENTRY;

    auto delSLE = [&view, &sleCredential, j](
                      AccountID const& account, SField const& node, bool isOwner) -> TER {
        auto const sleAccount = view.peek(keylet::account(account));
        if (!sleAccount)
        {
            // LCOV_EXCL_START
            JLOG(j.fatal()) << "Internal error: can't retrieve Owner account.";
            return tecINTERNAL;
            // LCOV_EXCL_STOP
        }

        std::uint64_t const page = sleCredential->getFieldU64(node);
        if (!view.dirRemove(keylet::ownerDir(account), page, sleCredential->key(), false))
        {
            // LCOV_EXCL_START
            JLOG(j.fatal()) << "Unable to delete Credential from owner.";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }

        if (isOwner)
            adjustOwnerCount(view, sleAccount, -1, j);

        return tesSUCCESS;
    };

    auto const issuer = sleCredential->getAccountID(sfIssuer);
    auto const subject = sleCredential->getAccountID(sfSubject);
    bool const accepted = (sleCredential->getFlags() & lsfAccepted) != 0u;

    auto err = delSLE(issuer, sfIssuerNode, !accepted || (subject == issuer));
    if (!isTesSuccess(err))
        return err;

    if (subject != issuer)
    {
        err = delSLE(subject, sfSubjectNode, accepted);
        if (!isTesSuccess(err))
            return err;
    }

    view.erase(sleCredential);

    return tesSUCCESS;
}

/** Validate the `sfCredentialIDs` field of a transaction at preflight time.
 *
 *  Enforces three constraints on the `STVector256` of credential object hashes:
 *  - Non-empty
 *  - At most `kMAX_CREDENTIALS_ARRAY_SIZE` (8) entries
 *  - No duplicate hashes
 *
 *  If `sfCredentialIDs` is absent the function returns `tesSUCCESS` immediately,
 *  as credentials are optional for most transaction types.
 *
 *  @param tx  The transaction under preflight validation.
 *  @param j   Journal for trace-level malformed-transaction logging.
 *  @return `tesSUCCESS` if the field is absent or valid; `temMALFORMED` if the
 *      array is empty, too large, or contains duplicates.
 */
NotTEC
checkFields(STTx const& tx, beast::Journal j)
{
    if (!tx.isFieldPresent(sfCredentialIDs))
        return tesSUCCESS;

    auto const& credentials = tx.getFieldV256(sfCredentialIDs);
    if (credentials.empty() || (credentials.size() > kMAX_CREDENTIALS_ARRAY_SIZE))
    {
        JLOG(j.trace()) << "Malformed transaction: Credentials array size is invalid: "
                        << credentials.size();
        return temMALFORMED;
    }

    std::unordered_set<uint256> duplicates;
    for (auto const& cred : credentials)
    {
        auto [it, ins] = duplicates.insert(cred);
        if (!ins)
        {
            JLOG(j.trace()) << "Malformed transaction: duplicates in credentials.";
            return temMALFORMED;
        }
    }

    return tesSUCCESS;
}

/** Verify that all credentials in a transaction exist, belong to the sender,
 *  and have been accepted — for use in preclaim only.
 *
 *  Checks each credential ID in `sfCredentialIDs` against the ledger:
 *  the SLE must exist, its `sfSubject` must equal @p src, and `lsfAccepted`
 *  must be set.  Expiration is deliberately **not** checked here; expired
 *  credentials are deleted in doApply by `verifyDepositPreauth` or
 *  `verifyValidDomain`.
 *
 *  @note Call `verifyDepositPreauth` in doApply when this function succeeds in
 *      preclaim — that pairing ensures expired credentials are garbage-collected
 *      even for transactions that ultimately fail.
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
valid(STTx const& tx, ReadView const& view, AccountID const& src, beast::Journal j)
{
    if (!tx.isFieldPresent(sfCredentialIDs))
        return tesSUCCESS;

    auto const& credIDs(tx.getFieldV256(sfCredentialIDs));
    for (auto const& h : credIDs)
    {
        auto const sleCred = view.read(keylet::credential(h));
        if (!sleCred)
        {
            JLOG(j.trace()) << "Credential doesn't exist. Cred: " << h;
            return tecBAD_CREDENTIALS;
        }

        if (sleCred->getAccountID(sfSubject) != src)
        {
            JLOG(j.trace()) << "Credential doesn't belong to the source account. Cred: " << h;
            return tecBAD_CREDENTIALS;
        }

        if ((sleCred->getFlags() & lsfAccepted) == 0u)
        {
            JLOG(j.trace()) << "Credential isn't accepted. Cred: " << h;
            return tecBAD_CREDENTIALS;
        }
    }

    return tesSUCCESS;
}

/** Check whether @p subject holds a live, accepted credential for a domain —
 *  for use in preclaim only.
 *
 *  Reads the `PermissionedDomain` SLE, iterates its `sfAcceptedCredentials`
 *  array, and looks up each corresponding credential SLE for @p subject.
 *  A credential qualifies when it exists, has not expired, and has `lsfAccepted`
 *  set.  If at least one qualifying credential is found, `tesSUCCESS` is
 *  returned immediately.
 *
 *  Expired credentials cannot be deleted from a `ReadView`, so this function
 *  tracks them and returns `tecEXPIRED` when all encountered credentials are
 *  expired (allowing the caller to suppress that specific code and proceed to
 *  doApply where `verifyValidDomain` will physically remove them).
 *
 *  @note If this returns `tecEXPIRED` in preclaim, the caller must invoke
 *      `verifyValidDomain` in doApply to garbage-collect the expired objects,
 *      even if the transaction ultimately fails.
 *
 *  @param view      Read-only ledger view.
 *  @param domainID  Key of the `PermissionedDomain` SLE to check against.
 *  @param subject   Account that must hold a qualifying credential.
 *  @return `tesSUCCESS` if a live accepted credential exists; `tecEXPIRED` if
 *      only expired credentials were found; `tecNO_AUTH` if no matching
 *      credential exists; `tecOBJECT_NOT_FOUND` if the domain does not exist.
 */
TER
validDomain(ReadView const& view, uint256 domainID, AccountID const& subject)
{
    // Note, permissioned domain objects can be deleted at any time
    auto const slePD = view.read(keylet::permissionedDomain(domainID));
    if (!slePD)
        return tecOBJECT_NOT_FOUND;

    auto const closeTime = view.header().parentCloseTime;
    bool foundExpired = false;
    for (auto const& h : slePD->getFieldArray(sfAcceptedCredentials))
    {
        auto const issuer = h.getAccountID(sfIssuer);
        auto const type = h.getFieldVL(sfCredentialType);
        auto const keyletCredential = keylet::credential(subject, issuer, makeSlice(type));
        auto const sleCredential = view.read(keyletCredential);

        if (sleCredential)
        {
            if (checkExpired(*sleCredential, closeTime))
            {
                foundExpired = true;
                continue;
            }
            if ((sleCredential->getFlags() & lsfAccepted) != 0u)
            {
                return tesSUCCESS;
            }

            continue;
        }
    }

    return foundExpired ? tecEXPIRED : tecNO_AUTH;
}

/** Check whether a set of credential IDs matches a `DepositPreauth` entry for
 *  the destination account.
 *
 *  Builds a sorted `std::set<std::pair<AccountID, Slice>>` of
 *  `(issuer, credentialType)` pairs from the submitted credential IDs, then
 *  tests for the existence of `keylet::depositPreauth(dst, sorted)` in the
 *  ledger.  The sorted representation matches the canonical key used at
 *  `DepositPreauth` creation time.
 *
 *  The `lifeExtender` vector keeps every `shared_ptr<SLE const>` alive for the
 *  duration of the function.  `Slice` members in @p sorted are non-owning views
 *  into SLE storage; releasing the shared pointers early would dangle them.
 *
 *  @note Credential existence has already been confirmed in preclaim; a missing
 *      SLE here indicates an internal consistency error and returns `tefINTERNAL`.
 *
 *  @param view     Read-only ledger view used for SLE and keylet lookups.
 *  @param credIDs  The `sfCredentialIDs` vector from the transaction.
 *  @param dst      The destination account whose `DepositPreauth` is checked.
 *  @return `tesSUCCESS` if a matching `DepositPreauth` object exists;
 *      `tecNO_PERMISSION` if none exists; `tefINTERNAL` if a credential SLE
 *      is unexpectedly missing or a duplicate pair is encountered.
 */
TER
authorizedDepositPreauth(ReadView const& view, STVector256 const& credIDs, AccountID const& dst)
{
    std::set<std::pair<AccountID, Slice>> sorted;
    std::vector<std::shared_ptr<SLE const>> lifeExtender;
    lifeExtender.reserve(credIDs.size());
    for (auto const& h : credIDs)
    {
        auto sleCred = view.read(keylet::credential(h));
        if (!sleCred)            // already checked in preclaim
            return tefINTERNAL;  // LCOV_EXCL_LINE

        auto [it, ins] = sorted.emplace((*sleCred)[sfIssuer], (*sleCred)[sfCredentialType]);
        if (!ins)
            return tefINTERNAL;  // LCOV_EXCL_LINE
        lifeExtender.push_back(std::move(sleCred));
    }

    if (!view.exists(keylet::depositPreauth(dst, sorted)))
        return tecNO_PERMISSION;

    return tesSUCCESS;
}

/** Build a sorted `(issuer, credentialType)` set from a credentials array.
 *
 *  Produces the canonical representation used to key `DepositPreauth` objects.
 *  Each element of @p credentials must carry `sfIssuer` and `sfCredentialType`.
 *
 *  @param credentials  The `STArray` of credential pairs, as stored in a
 *      `DepositPreauth` or `PermissionedDomainSet` transaction.
 *  @return A sorted set of `(AccountID, Slice)` pairs; returns an empty set if
 *      any duplicate `(issuer, credentialType)` pair is detected.
 */
std::set<std::pair<AccountID, Slice>>
makeSorted(STArray const& credentials)
{
    std::set<std::pair<AccountID, Slice>> out;
    for (auto const& cred : credentials)
    {
        auto [it, ins] = out.emplace(cred[sfIssuer], cred[sfCredentialType]);
        if (!ins)
            return {};
    }
    return out;
}

/** Validate a credential array in `DepositPreauth` or `PermissionedDomainSet`
 *  transactions at preflight time.
 *
 *  Credentials in these transactions are `(issuer, credentialType)` pairs
 *  rather than object hashes.  This function enforces:
 *  - Non-empty array and size at most @p maxSize (returns `temARRAY_EMPTY` /
 *    `temARRAY_TOO_LARGE` respectively).
 *  - Valid issuer `AccountID` for each element.
 *  - `sfCredentialType` length in `[1, kMAX_CREDENTIAL_TYPE_LENGTH]` bytes.
 *  - No logical duplicates, detected via `sha512Half(issuer, credentialType)`
 *    to catch pairs that are semantically identical regardless of encoding.
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
checkArray(STArray const& credentials, unsigned maxSize, beast::Journal j)
{
    if (credentials.empty() || (credentials.size() > maxSize))
    {
        JLOG(j.trace()) << "Malformed transaction: "
                           "Invalid credentials size: "
                        << credentials.size();
        return credentials.empty() ? temARRAY_EMPTY : temARRAY_TOO_LARGE;
    }

    std::unordered_set<uint256> duplicates;
    for (auto const& credential : credentials)
    {
        auto const& issuer = credential[sfIssuer];
        if (!issuer)
        {
            JLOG(j.trace()) << "Malformed transaction: "
                               "Issuer account is invalid: "
                            << to_string(issuer);
            return temINVALID_ACCOUNT_ID;
        }

        auto const ct = credential[sfCredentialType];
        if (ct.empty() || (ct.size() > kMAX_CREDENTIAL_TYPE_LENGTH))
        {
            JLOG(j.trace()) << "Malformed transaction: "
                               "Invalid credentialType size: "
                            << ct.size();
            return temMALFORMED;
        }

        auto [it, ins] = duplicates.insert(sha512Half(issuer, ct));
        if (!ins)
        {
            JLOG(j.trace()) << "Malformed transaction: "
                               "duplicates in credentials.";
            return temMALFORMED;
        }
    }

    return tesSUCCESS;
}

}  // namespace credentials

/** Verify domain-credential authorization and delete any expired credentials.
 *
 *  The doApply counterpart to `credentials::validDomain`.  Collects all
 *  credential SLEs for @p account that match the `sfAcceptedCredentials` list
 *  of the `PermissionedDomain` at @p domainID, calls `credentials::removeExpired`
 *  to physically delete any that have passed their expiration time, then
 *  re-checks whether at least one live, accepted credential remains.
 *
 *  The two-pass design — collect first, then expire, then re-validate —
 *  ensures expired objects are garbage-collected even when the surrounding
 *  transaction ultimately fails.
 *
 *  @param view      Mutable ledger view; expired credential SLEs are erased.
 *  @param account   Account whose credentials are being verified.
 *  @param domainID  Key of the `PermissionedDomain` SLE.
 *  @param j         Journal for trace/error logging.
 *  @return `tesSUCCESS` if a live accepted credential for the domain exists;
 *      `tecEXPIRED` if only expired credentials were found; `tecNO_PERMISSION`
 *      if no matching credential exists; `tecOBJECT_NOT_FOUND` if the domain
 *      SLE is missing; or a `TER` error propagated from `removeExpired` under
 *      `fixSecurity3_1_3`.
 */
TER
verifyValidDomain(ApplyView& view, AccountID const& account, uint256 domainID, beast::Journal j)
{
    auto const slePD = view.read(keylet::permissionedDomain(domainID));
    if (!slePD)
        return tecOBJECT_NOT_FOUND;

    STVector256 credentials;
    for (auto const& h : slePD->getFieldArray(sfAcceptedCredentials))
    {
        auto const issuer = h.getAccountID(sfIssuer);
        auto const type = h.getFieldVL(sfCredentialType);
        auto const keyletCredential = keylet::credential(account, issuer, makeSlice(type));
        if (view.exists(keyletCredential))
            credentials.pushBack(keyletCredential.key);
    }

    auto const foundExpired = credentials::removeExpired(view, credentials, j);
    if (!foundExpired.has_value())
        return foundExpired.error();

    for (auto const& h : credentials)
    {
        auto sleCredential = view.read(keylet::credential(h));
        if (!sleCredential)
            continue;  // expired, i.e. deleted in credentials::removeExpired

        if ((sleCredential->getFlags() & lsfAccepted) != 0u)
            return tesSUCCESS;
    }

    return *foundExpired ? tecEXPIRED : tecNO_PERMISSION;
}

/** Enforce deposit pre-authorization in doApply, deleting expired credentials.
 *
 *  Called by the Payment, EscrowFinish, and PaymentChannelClaim transactors
 *  when the destination account has `lsfDepositAuth` set.  Authorization
 *  succeeds when any of the following hold:
 *  - `src == dst` (self-payment always allowed)
 *  - `keylet::depositPreauth(dst, src)` exists (account-level pre-auth)
 *  - A credential-set `DepositPreauth` object exists for the credentials
 *    submitted in the transaction (via `credentials::authorizedDepositPreauth`)
 *
 *  If `sfCredentialIDs` is present, `credentials::removeExpired` is called
 *  unconditionally before the authorization tests — expired credentials are
 *  deleted as a side effect even if the transaction ultimately fails.  If any
 *  credential was expired, the function returns `tecEXPIRED` immediately
 *  without proceeding to the authorization checks.
 *
 *  @param tx      The transaction under doApply; may carry `sfCredentialIDs`.
 *  @param view    Mutable ledger view; expired credential SLEs may be erased.
 *  @param src     The sending account.
 *  @param dst     The destination account.
 *  @param sleDst  The destination account's SLE; used to test `lsfDepositAuth`.
 *      If null, `lsfDepositAuth` is treated as unset and the function returns
 *      `tesSUCCESS`.
 *  @param j       Journal for trace/error logging.
 *  @return `tesSUCCESS` if authorized or `lsfDepositAuth` is not set;
 *      `tecEXPIRED` if submitted credentials have expired; `tecNO_PERMISSION`
 *      if no matching pre-authorization exists; or a propagated error from
 *      `removeExpired` or `authorizedDepositPreauth`.
 */
TER
verifyDepositPreauth(
    STTx const& tx,
    ApplyView& view,
    AccountID const& src,
    AccountID const& dst,
    std::shared_ptr<SLE const> const& sleDst,
    beast::Journal j)
{
    bool const credentialsPresent = tx.isFieldPresent(sfCredentialIDs);

    if (credentialsPresent)
    {
        auto const foundExpired =
            credentials::removeExpired(view, tx.getFieldV256(sfCredentialIDs), j);
        if (!foundExpired.has_value())
            return foundExpired.error();
        if (*foundExpired)
            return tecEXPIRED;
    }

    if (sleDst && ((sleDst->getFlags() & lsfDepositAuth) != 0u))
    {
        if (src != dst)
        {
            if (!view.exists(keylet::depositPreauth(dst, src)))
            {
                return !credentialsPresent ? tecNO_PERMISSION
                                           : credentials::authorizedDepositPreauth(
                                                 view, tx.getFieldV256(sfCredentialIDs), dst);
            }
        }
    }

    return tesSUCCESS;
}

}  // namespace xrpl
