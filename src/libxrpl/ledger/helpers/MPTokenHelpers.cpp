/** @file
 *  MPT-specific ledger helper implementations.
 *
 *  Covers freeze checking, transfer-rate encoding, holding lifecycle,
 *  two-phase authorization, escrow accounting, and supply-overflow safety
 *  for Multi-Purpose Tokens (MPT). Functions here are the MPT counterpart to
 *  `RippleStateHelpers.cpp`; the asset-agnostic `TokenHelpers.cpp` dispatches
 *  to them via `std::visit` on the `Asset` variant.
 *
 *  @see RippleStateHelpers.cpp, TokenHelpers.cpp
 */
#include <xrpl/ledger/helpers/MPTokenHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>

namespace xrpl {

/** Check whether an entire MPT issuance is globally frozen.
 *
 *  Reads the `MPTIssuance` SLE and tests `lsfMPTLocked`. A missing issuance
 *  SLE is treated as unfrozen (returns `false`).
 *
 *  @param view The ledger state to query.
 *  @param mptIssue The MPT issuance to check.
 *  @return `true` if `lsfMPTLocked` is set on the issuance; `false` otherwise.
 */
bool
isGlobalFrozen(ReadView const& view, MPTIssue const& mptIssue)
{
    if (auto const sle = view.read(keylet::mptIssuance(mptIssue.getMptID())))
        return sle->isFlag(lsfMPTLocked);
    return false;
}

/** Check whether a specific account's MPToken holding is individually frozen.
 *
 *  Reads the per-account `MPToken` SLE and tests `lsfMPTLocked`. A missing
 *  `MPToken` SLE (account has no holding) is treated as unfrozen.
 *
 *  @param view The ledger state to query.
 *  @param account The account whose holding is checked.
 *  @param mptIssue The MPT issuance to check against.
 *  @return `true` if the account's `MPToken` carries `lsfMPTLocked`; `false` otherwise.
 */
bool
isIndividualFrozen(ReadView const& view, AccountID const& account, MPTIssue const& mptIssue)
{
    if (auto const sle = view.read(keylet::mptoken(mptIssue.getMptID(), account)))
        return sle->isFlag(lsfMPTLocked);
    return false;
}

/** Check whether an account's access to an MPT issuance is frozen by any tier.
 *
 *  Applies three checks in order: global issuance lock (`isGlobalFrozen`),
 *  per-account holding lock (`isIndividualFrozen`), and vault pseudo-account
 *  freeze (`isVaultPseudoAccountFrozen`). Short-circuits on the first match.
 *
 *  @param view The ledger state to query.
 *  @param account The account to check.
 *  @param mptIssue The MPT issuance to check against.
 *  @param depth Recursion depth passed to `isVaultPseudoAccountFrozen`; guards
 *      against pathological nested-vault configurations.
 *  @return `true` if any freeze tier applies; `false` otherwise.
 */
bool
isFrozen(ReadView const& view, AccountID const& account, MPTIssue const& mptIssue, int depth)
{
    return isGlobalFrozen(view, mptIssue) || isIndividualFrozen(view, account, mptIssue) ||
        isVaultPseudoAccountFrozen(view, account, mptIssue, depth);
}

/** Check whether any account in a set is frozen for an MPT issuance.
 *
 *  Applies all three freeze tiers but deliberately sequences them across
 *  separate passes to minimise cost: global freeze is tested once and
 *  short-circuits immediately; individual freezes are checked for every
 *  account before the more expensive vault pseudo-account recursion begins.
 *
 *  @param view The ledger state to query.
 *  @param accounts The set of accounts to check.
 *  @param mptIssue The MPT issuance to check against.
 *  @param depth Recursion depth guard forwarded to `isVaultPseudoAccountFrozen`.
 *  @return `true` if the global freeze is set, or if any account carries an
 *      individual freeze, or if any account is a frozen vault pseudo-account.
 */
[[nodiscard]] bool
isAnyFrozen(
    ReadView const& view,
    std::initializer_list<AccountID> const& accounts,
    MPTIssue const& mptIssue,
    int depth)
{
    if (isGlobalFrozen(view, mptIssue))
        return true;

    for (auto const& account : accounts)
    {
        if (isIndividualFrozen(view, account, mptIssue))
            return true;
    }

    for (auto const& account : accounts)
    {
        if (isVaultPseudoAccountFrozen(view, account, mptIssue, depth))
            return true;
    }

    return false;
}

/** Convert the `sfTransferFee` field of an MPT issuance to the XRPL `Rate` type.
 *
 *  `sfTransferFee` is a `uint16` in the range 0–50,000 representing 0–50%.
 *  The `Rate` encoding adds it to the `1,000,000,000` parity base using the
 *  formula `1,000,000,000 + (10,000 × fee)`, so a 50% fee becomes
 *  `1,500,000,000`. When `sfTransferFee` is absent, `kPARITY_RATE`
 *  (`1,000,000,000`) is returned, indicating no fee.
 *
 *  @param view The ledger state to query.
 *  @param issuanceID The `MPTokenIssuanceID` of the issuance.
 *  @return The transfer rate as a `Rate` value; `kPARITY_RATE` when no fee
 *      is configured or the issuance SLE is absent.
 */
Rate
transferRate(ReadView const& view, MPTID const& issuanceID)
{
    if (auto const sle = view.read(keylet::mptIssuance(issuanceID));
        sle && sle->isFieldPresent(sfTransferFee))
    {
        auto const fee = sle->getFieldU16(sfTransferFee);
        XRPL_ASSERT(fee <= kMAX_TRANSFER_FEE, "xrpl::transferRate : fee is too large");
        return Rate{1'000'000'000u + (10'000 * fee)};
    }

    return kPARITY_RATE;
}

/** Read-only pre-check: verify that an independent holding can be created.
 *
 *  Validates two preconditions before `addEmptyHolding` mutates the ledger:
 *  the `MPTIssuance` must exist, and it must carry `lsfMPTCanTransfer`.
 *  Tokens without `lsfMPTCanTransfer` can only move directly between the
 *  issuer and counterparties, making independent holdings meaningless.
 *
 *  @param view The ledger state to query.
 *  @param mptIssue The MPT issuance the caller wants to hold.
 *  @return `tesSUCCESS`, `tecOBJECT_NOT_FOUND` if the issuance SLE is
 *      absent, or `tecNO_AUTH` if `lsfMPTCanTransfer` is not set.
 */
[[nodiscard]] TER
canAddHolding(ReadView const& view, MPTIssue const& mptIssue)
{
    auto mptID = mptIssue.getMptID();
    auto issuance = view.read(keylet::mptIssuance(mptID));
    if (!issuance)
    {
        return tecOBJECT_NOT_FOUND;
    }
    if (!issuance->isFlag(lsfMPTCanTransfer))
    {
        return tecNO_AUTH;
    }

    return tesSUCCESS;
}

/** Create a zero-balance `MPToken` holding for `accountID`.
 *
 *  Validates that the issuance exists and is not globally locked, rejects
 *  duplicates, and silently succeeds for the issuer (who never holds a
 *  `MPToken` SLE for their own issuance). Otherwise delegates to
 *  `authorizeMPToken` which enforces the reserve requirement and inserts
 *  the SLE into the owner directory.
 *
 *  @param view The mutable ledger state.
 *  @param accountID The account requesting the holding.
 *  @param priorBalance XRP balance before this transaction; used for the
 *      reserve check inside `authorizeMPToken`.
 *  @param mptIssue The MPT issuance to hold.
 *  @param journal Logging sink.
 *  @return `tesSUCCESS`, `tecDUPLICATE` if a holding already exists,
 *      `tecINSUFFICIENT_RESERVE` if reserves are too low, or `tefINTERNAL`
 *      if the issuance is missing or globally locked (invariant violations).
 */
[[nodiscard]] TER
addEmptyHolding(
    ApplyView& view,
    AccountID const& accountID,
    XRPAmount priorBalance,
    MPTIssue const& mptIssue,
    beast::Journal journal)
{
    auto const& mptID = mptIssue.getMptID();
    auto const mpt = view.peek(keylet::mptIssuance(mptID));
    if (!mpt)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    if (mpt->isFlag(lsfMPTLocked))
        return tefINTERNAL;  // LCOV_EXCL_LINE
    if (view.peek(keylet::mptoken(mptID, accountID)))
        return tecDUPLICATE;
    if (accountID == mptIssue.getIssuer())
        return tesSUCCESS;

    return authorizeMPToken(view, priorBalance, mptID, accountID, journal);
}

/** Core MPToken SLE lifecycle function — create, delete, or toggle authorization.
 *
 *  The `holderID` parameter determines which role `account` plays:
 *  - When `holderID` is `nullopt`, `account` is the holder submitting the
 *    transaction. Without `tfMPTUnauthorize`, a new `MPToken` SLE is created
 *    and linked into the owner directory (after a reserve check). With
 *    `tfMPTUnauthorize`, the existing SLE is removed and owner count decremented.
 *    Reserve is only enforced when `ownerCount >= 2`, mirroring trust-line policy.
 *  - When `holderID` is set, `account` must be the issuance's issuer. The
 *    function toggles `lsfMPTAuthorized` on the holder's existing `MPToken` SLE.
 *
 *  @param view The mutable ledger state.
 *  @param priorBalance XRP balance before this transaction; used only for the
 *      reserve check when creating a new holding (`holderID` is `nullopt` and
 *      `tfMPTUnauthorize` is not set).
 *  @param mptIssuanceID The issuance being authorized or deauthorized.
 *  @param account Submitting account: the holder (when `holderID` is absent)
 *      or the issuer (when `holderID` is set).
 *  @param journal Logging sink.
 *  @param flags Transaction flags; `tfMPTUnauthorize` selects the
 *      delete/deauthorize path.
 *  @param holderID When set, `account` is the issuer and this is the holder
 *      whose `lsfMPTAuthorized` flag is toggled.
 *  @return `tesSUCCESS` or a `tec`/`tef` error code.
 */
[[nodiscard]] TER
authorizeMPToken(
    ApplyView& view,
    XRPAmount const& priorBalance,
    MPTID const& mptIssuanceID,
    AccountID const& account,
    beast::Journal journal,
    std::uint32_t flags,
    std::optional<AccountID> holderID)
{
    auto const sleAcct = view.peek(keylet::account(account));
    if (!sleAcct)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (!holderID)
    {
        if ((flags & tfMPTUnauthorize) != 0u)
        {
            auto const mptokenKey = keylet::mptoken(mptIssuanceID, account);
            auto const sleMpt = view.peek(mptokenKey);
            if (!sleMpt || (*sleMpt)[sfMPTAmount] != 0 ||
                (view.rules().enabled(fixSecurity3_1_3) &&
                 (*sleMpt)[~sfLockedAmount].valueOr(0) != 0))
                return tecINTERNAL;  // LCOV_EXCL_LINE

            if (!view.dirRemove(
                    keylet::ownerDir(account), (*sleMpt)[sfOwnerNode], sleMpt->key(), false))
                return tecINTERNAL;  // LCOV_EXCL_LINE

            adjustOwnerCount(view, sleAcct, -1, journal);

            view.erase(sleMpt);
            return tesSUCCESS;
        }

        // Reserve is only enforced when the account already owns more than two
        // objects; the first two items are free (same policy as trust lines).
        std::uint32_t const uOwnerCount = sleAcct->getFieldU32(sfOwnerCount);
        XRPAmount const reserveCreate(
            (uOwnerCount < 2) ? XRPAmount(beast::kZERO)
                              : view.fees().accountReserve(uOwnerCount + 1));

        if (priorBalance < reserveCreate)
            return tecINSUFFICIENT_RESERVE;

        auto const mpt = view.read(keylet::mptIssuance(mptIssuanceID));
        if (!mpt || mpt->getAccountID(sfIssuer) == account)
        {
            // LCOV_EXCL_START
            UNREACHABLE("xrpl::authorizeMPToken : invalid issuance or issuers token");
            if (view.rules().enabled(featureLendingProtocol))
                return tecINTERNAL;
            // LCOV_EXCL_STOP
        }

        auto const mptokenKey = keylet::mptoken(mptIssuanceID, account);
        auto mptoken = std::make_shared<SLE>(mptokenKey);
        if (auto ter = dirLink(view, account, mptoken))
            return ter;  // LCOV_EXCL_LINE

        (*mptoken)[sfAccount] = account;
        (*mptoken)[sfMPTokenIssuanceID] = mptIssuanceID;
        (*mptoken)[sfFlags] = 0;
        view.insert(mptoken);

        adjustOwnerCount(view, sleAcct, 1, journal);

        return tesSUCCESS;
    }

    auto const sleMptIssuance = view.read(keylet::mptIssuance(mptIssuanceID));
    if (!sleMptIssuance)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (account != (*sleMptIssuance)[sfIssuer])
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const sleMpt = view.peek(keylet::mptoken(mptIssuanceID, *holderID));
    if (!sleMpt)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    std::uint32_t const flagsIn = sleMpt->getFieldU32(sfFlags);
    std::uint32_t flagsOut = flagsIn;

    if ((flags & tfMPTUnauthorize) != 0u)
        flagsOut &= ~lsfMPTAuthorized;
    else
        flagsOut |= lsfMPTAuthorized;

    if (flagsIn != flagsOut)
        sleMpt->setFieldU32(sfFlags, flagsOut);

    view.update(sleMpt);
    return tesSUCCESS;
}

/** Delete a zero-balance `MPToken` holding.
 *
 *  Deletion mirror of `addEmptyHolding`. Succeeds immediately if the caller
 *  is the issuer and no `MPToken` SLE exists (the normal case). If an SLE is
 *  found for the issuer it is deleted defensively — MPT accounting does not
 *  create such a situation, but the guard prevents ledger imbalance. Rejects
 *  with `tecHAS_OBLIGATIONS` if `sfMPTAmount` or (under `fixSecurity3_1_3`)
 *  `sfLockedAmount` is non-zero, because deleting a token with a live balance
 *  would corrupt `sfOutstandingAmount` on the issuance. On all other paths,
 *  delegates to `authorizeMPToken` with `tfMPTUnauthorize` to remove the SLE
 *  and decrement the owner count without duplicating that logic here.
 *
 *  @param view The mutable ledger state.
 *  @param accountID The account whose holding is being removed.
 *  @param mptIssue The MPT issuance.
 *  @param journal Logging sink.
 *  @return `tesSUCCESS`, `tecOBJECT_NOT_FOUND` if no holding exists (non-issuer),
 *      or `tecHAS_OBLIGATIONS` if the holding still carries a balance.
 */
[[nodiscard]] TER
removeEmptyHolding(
    ApplyView& view,
    AccountID const& accountID,
    MPTIssue const& mptIssue,
    beast::Journal journal)
{
    bool const accountIsIssuer = accountID == mptIssue.getIssuer();
    auto const& mptID = mptIssue.getMptID();
    auto const mptoken = view.peek(keylet::mptoken(mptID, accountID));
    if (!mptoken)
        return accountIsIssuer ? (TER)tesSUCCESS : (TER)tecOBJECT_NOT_FOUND;
    if (mptoken->at(sfMPTAmount) != 0 ||
        (view.rules().enabled(fixSecurity3_1_3) && (*mptoken)[~sfLockedAmount].valueOr(0) != 0))
        return tecHAS_OBLIGATIONS;

    return authorizeMPToken(
        view,
        {},  // priorBalance
        mptID,
        accountID,
        journal,
        tfMPTUnauthorize  // flags
    );
}

/** Preclaim (read-only) authorization check for an MPT holding.
 *
 *  Issuers are always authorized. Vault and `LoanBroker` pseudo-accounts are
 *  implicitly authorized (under `featureSingleAssetVault`). When the issuance
 *  carries `sfDomainID`, credential-based authorization via
 *  `credentials::validDomain` takes precedence over `lsfMPTAuthorized` — a
 *  passing domain check succeeds even if no `MPToken` SLE exists, and even if
 *  the SLE's `lsfMPTAuthorized` flag is not set.
 *
 *  When the issuance belongs to a vault pseudo-account, the function recurses
 *  into the vault's underlying asset's `requireAuth` (bounded by `depth`
 *  vs. `kMAX_ASSET_CHECK_DEPTH`). This recursion is purely defensive; the
 *  ledger does not currently permit nested-vault MPT configurations.
 *
 *  `WeakAuth` intentionally allows a missing `MPToken` SLE (used in MPToken V2
 *  flows where the token is created on demand during apply).
 *
 *  @param view The ledger state to query (read-only; called in preclaim).
 *  @param mptIssue The MPT issuance being accessed.
 *  @param account The account requesting access.
 *  @param authType Controls leniency toward missing `MPToken` SLEs.
 *  @param depth Current recursion depth; guards against theoretical infinite
 *      recursion through nested vault configurations.
 *  @return `tesSUCCESS` if authorized, `tecOBJECT_NOT_FOUND` if the issuance
 *      is absent, or `tecNO_AUTH` / `tecEXPIRED` on authorization failure.
 */
[[nodiscard]] TER
requireAuth(
    ReadView const& view,
    MPTIssue const& mptIssue,
    AccountID const& account,
    AuthType authType,
    int depth)
{
    auto const mptID = keylet::mptIssuance(mptIssue.getMptID());
    auto const sleIssuance = view.read(mptID);
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    auto const mptIssuer = sleIssuance->getAccountID(sfIssuer);

    if (mptIssuer == account)  // Issuer won't have MPToken
        return tesSUCCESS;

    bool const featureSAVEnabled = view.rules().enabled(featureSingleAssetVault);

    if (featureSAVEnabled)
    {
        if (depth >= kMAX_ASSET_CHECK_DEPTH)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        auto const sleIssuer = view.read(keylet::account(mptIssuer));
        if (!sleIssuer)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        if (sleIssuer->isFieldPresent(sfVaultID))
        {
            auto const sleVault = view.read(keylet::vault(sleIssuer->getFieldH256(sfVaultID)));
            if (!sleVault)
                return tefINTERNAL;  // LCOV_EXCL_LINE

            auto const asset = sleVault->at(sfAsset);
            if (auto const err = asset.visit(
                    [&](Issue const& issue) { return requireAuth(view, issue, account, authType); },
                    [&](MPTIssue const& issue) {
                        return requireAuth(view, issue, account, authType, depth + 1);
                    });
                !isTesSuccess(err))
                return err;
        }
    }

    auto const mptokenID = keylet::mptoken(mptID.key, account);
    auto const sleToken = view.read(mptokenID);

    if (!sleToken && (authType == AuthType::StrongAuth || authType == AuthType::Legacy))
        return tecNO_AUTH;

    // Note, this check is not amendment-gated because DomainID will be always
    // empty **unless** writing to it has been enabled by an amendment
    auto const maybeDomainID = sleIssuance->at(~sfDomainID);
    if (maybeDomainID)
    {
        XRPL_ASSERT(
            sleIssuance->getFieldU32(sfFlags) & lsfMPTRequireAuth,
            "xrpl::requireAuth : issuance requires authorization");
        // ter = tefINTERNAL | tecOBJECT_NOT_FOUND | tecNO_AUTH | tecEXPIRED
        auto const ter = credentials::validDomain(view, *maybeDomainID, account);
        if (isTesSuccess(ter))
        {
            return ter;  // Note: sleToken might be null
        }
        if (!sleToken)
        {
            return ter;
        }
        // We ignore error from validDomain if we found sleToken, as it could
        // belong to someone who is explicitly authorized e.g. a vault owner.
    }

    if (featureSAVEnabled)
    {
        if (isPseudoAccount(view, account, {&sfVaultID, &sfLoanBrokerID}))
            return tesSUCCESS;
    }

    if (sleIssuance->isFlag(lsfMPTRequireAuth) &&
        (!sleToken || !sleToken->isFlag(lsfMPTAuthorized)))
        return tecNO_AUTH;

    return tesSUCCESS;  // Note: sleToken might be null
}

/** Apply-phase (mutating) MPT authorization enforcement.
 *
 *  Called from `doApply` after `requireAuth` passed in preclaim. Handles the
 *  case preclaim cannot: when a domain-authorized account lacks a `MPToken`
 *  SLE, this function materializes it on the fly via `authorizeMPToken`.
 *  `verifyValidDomain` is called here (not in preclaim) so that any expired
 *  credential objects are deleted as a side effect.
 *
 *  The implementation is a complete case analysis on
 *  `(authorizedByDomain, sleToken != nullptr)`. Each branch carries an
 *  `XRPL_ASSERT` documenting the expected invariant; an `UNREACHABLE` guard
 *  on the final else branch confirms that all cases are covered.
 *
 *  @param view The mutable ledger state.
 *  @param mptIssuanceID The issuance being accessed.
 *  @param account The account being authorized; must not be the issuer.
 *  @param priorBalance XRP balance; forwarded to `authorizeMPToken` when a
 *      new `MPToken` SLE must be created for a domain-authorized account.
 *  @param j Logging sink.
 *  @return `tesSUCCESS` if the account is authorized; `tecNO_AUTH` or
 *      `tecEXPIRED` if not; `tefINTERNAL` on invariant violations.
 */
[[nodiscard]] TER
enforceMPTokenAuthorization(
    ApplyView& view,
    MPTID const& mptIssuanceID,
    AccountID const& account,
    XRPAmount const& priorBalance,  // for MPToken authorization
    beast::Journal j)
{
    auto const sleIssuance = view.read(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    XRPL_ASSERT(
        sleIssuance->isFlag(lsfMPTRequireAuth),
        "xrpl::enforceMPTokenAuthorization : authorization required");

    if (account == sleIssuance->at(sfIssuer))
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const keylet = keylet::mptoken(mptIssuanceID, account);
    auto const sleToken = view.read(keylet);  //  NOTE: might be null
    auto const maybeDomainID = sleIssuance->at(~sfDomainID);
    bool expired = false;
    bool const authorizedByDomain = [&]() -> bool {
        // NOTE: defensive here, should be checked in preclaim
        if (!maybeDomainID.has_value())
            return false;  // LCOV_EXCL_LINE

        auto const ter = verifyValidDomain(view, account, *maybeDomainID, j);
        if (isTesSuccess(ter))
            return true;
        if (ter == tecEXPIRED)
            expired = true;
        return false;
    }();

    if (!authorizedByDomain && sleToken == nullptr)
    {
        // Could not find MPToken and won't create one, could be either of:
        //
        // 1. Field sfDomainID not set in MPTokenIssuance or
        // 2. Account has no matching and accepted credentials or
        // 3. Account has all expired credentials (deleted in verifyValidDomain)
        //
        // Either way, return tecNO_AUTH and there is nothing else to do
        return expired ? tecEXPIRED : tecNO_AUTH;
    }
    if (!authorizedByDomain && maybeDomainID.has_value())
    {
        // Found an MPToken but the account is not authorized and we expect
        // it to have been authorized by the domain. This could be because the
        // credentials used to create the MPToken have expired or been deleted.
        return expired ? tecEXPIRED : tecNO_AUTH;
    }
    if (!authorizedByDomain)
    {
        // We found an MPToken, but sfDomainID is not set, so this is a classic
        // MPToken which requires authorization by the token issuer.
        XRPL_ASSERT(
            sleToken != nullptr && !maybeDomainID.has_value(),
            "xrpl::enforceMPTokenAuthorization : found MPToken");
        if (sleToken->isFlag(lsfMPTAuthorized))
            return tesSUCCESS;

        return tecNO_AUTH;
    }
    if (authorizedByDomain && sleToken != nullptr)
    {
        // Found an MPToken, authorized by the domain. Ignore authorization flag
        // lsfMPTAuthorized because it is meaningless. Return tesSUCCESS
        XRPL_ASSERT(
            maybeDomainID.has_value(),
            "xrpl::enforceMPTokenAuthorization : found MPToken for domain");
        return tesSUCCESS;
    }
    if (authorizedByDomain)
    {
        // Could not find MPToken but there should be one because we are
        // authorized by domain. Proceed to create it, then return tesSUCCESS
        XRPL_ASSERT(
            maybeDomainID.has_value() && sleToken == nullptr,
            "xrpl::enforceMPTokenAuthorization : new MPToken for domain");
        if (auto const err = authorizeMPToken(
                view,
                priorBalance,   // priorBalance
                mptIssuanceID,  // mptIssuanceID
                account,        // account
                j);
            !isTesSuccess(err))
            return err;

        return tesSUCCESS;
    }

    // LCOV_EXCL_START
    UNREACHABLE("xrpl::enforceMPTokenAuthorization : condition list is incomplete");
    return tefINTERNAL;
    // LCOV_EXCL_STOP
}

/** Check whether a transfer between two accounts is permitted by the issuance.
 *
 *  When `lsfMPTCanTransfer` is absent, third-party transfers are blocked;
 *  however, transfers where either `from` or `to` is the issuer are always
 *  allowed regardless of the flag. This mirrors the IOU trust-line policy
 *  that lets issuers send and receive their own tokens unconditionally.
 *
 *  @param view The ledger state to query.
 *  @param mptIssue The MPT issuance involved in the transfer.
 *  @param from The sending account.
 *  @param to The receiving account.
 *  @return `tesSUCCESS` if the transfer is permitted, `tecOBJECT_NOT_FOUND`
 *      if the issuance SLE is absent, or `tecNO_AUTH` if `lsfMPTCanTransfer`
 *      is unset and neither endpoint is the issuer.
 */
TER
canTransfer(
    ReadView const& view,
    MPTIssue const& mptIssue,
    AccountID const& from,
    AccountID const& to)
{
    auto const mptID = keylet::mptIssuance(mptIssue.getMptID());
    auto const sleIssuance = view.read(mptID);
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    if (!sleIssuance->isFlag(lsfMPTCanTransfer))
    {
        if (from != (*sleIssuance)[sfIssuer] && to != (*sleIssuance)[sfIssuer])
            return TER{tecNO_AUTH};
    }
    return tesSUCCESS;
}

/** Check whether an asset may be traded on the DEX.
 *
 *  Dispatches via `asset.visit`: XRP/IOU assets always succeed; for MPT,
 *  reads the issuance SLE and checks `lsfMPTCanTrade`.
 *
 *  @param view The ledger state to query.
 *  @param asset The asset to check; non-MPT assets always pass.
 *  @return `tesSUCCESS` if trading is permitted, `tecOBJECT_NOT_FOUND` if
 *      the MPT issuance SLE is absent, or `tecNO_PERMISSION` if
 *      `lsfMPTCanTrade` is not set.
 */
TER
canTrade(ReadView const& view, Asset const& asset)
{
    return asset.visit(
        [&](Issue const&) -> TER { return tesSUCCESS; },
        [&](MPTIssue const& mptIssue) -> TER {
            auto const sleIssuance = view.read(keylet::mptIssuance(mptIssue.getMptID()));
            if (!sleIssuance)
                return tecOBJECT_NOT_FOUND;
            if (!sleIssuance->isFlag(lsfMPTCanTrade))
                return tecNO_PERMISSION;
            return tesSUCCESS;
        });
}

/** Move MPT funds from a holder's spendable balance into escrow.
 *
 *  Decrements `sfMPTAmount` and increments `sfLockedAmount` on the sender's
 *  `MPToken` SLE, then increments `sfLockedAmount` on the `MPTIssuance` SLE.
 *  `sfOutstandingAmount` on the issuance is deliberately left unchanged —
 *  tokens in escrow remain in circulation until the escrow completes.
 *  All arithmetic is guarded by `canSubtract`/`canAdd` checks; failures are
 *  marked `LCOV_EXCL_LINE` because they indicate pre-condition violations.
 *
 *  @param view The mutable ledger state.
 *  @param sender The account placing tokens in escrow; must not be the issuer.
 *  @param amount The MPT amount to lock; must be a valid `MPTIssue` amount.
 *  @param j Logging sink.
 *  @return `tesSUCCESS`, or a `tec`/`tef` error if the issuance or `MPToken`
 *      SLE is missing, the sender is the issuer, or an arithmetic guard fires.
 */
TER
lockEscrowMPT(ApplyView& view, AccountID const& sender, STAmount const& amount, beast::Journal j)
{
    auto const mptIssue = amount.get<MPTIssue>();
    auto const mptID = keylet::mptIssuance(mptIssue.getMptID());
    auto sleIssuance = view.peek(mptID);
    if (!sleIssuance)
    {  // LCOV_EXCL_START
        JLOG(j.error()) << "lockEscrowMPT: MPT issuance not found for " << mptIssue.getMptID();
        return tecOBJECT_NOT_FOUND;
    }  // LCOV_EXCL_STOP

    if (amount.getIssuer() == sender)
    {  // LCOV_EXCL_START
        JLOG(j.error()) << "lockEscrowMPT: sender is the issuer, cannot lock MPTs.";
        return tecINTERNAL;
    }  // LCOV_EXCL_STOP

    {
        auto const mptokenID = keylet::mptoken(mptID.key, sender);
        auto sle = view.peek(mptokenID);
        if (!sle)
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "lockEscrowMPT: MPToken not found for " << sender;
            return tecOBJECT_NOT_FOUND;
        }  // LCOV_EXCL_STOP

        auto const amt = sle->getFieldU64(sfMPTAmount);
        auto const pay = amount.mpt().value();

        if (!canSubtract(STAmount(mptIssue, amt), STAmount(mptIssue, pay)))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "lockEscrowMPT: insufficient MPTAmount for " << to_string(sender)
                            << ": " << amt << " < " << pay;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        (*sle)[sfMPTAmount] = amt - pay;

        uint64_t const locked = (*sle)[~sfLockedAmount].valueOr(0);

        if (!canAdd(STAmount(mptIssue, locked), STAmount(mptIssue, pay)))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "lockEscrowMPT: overflow on locked amount for " << to_string(sender)
                            << ": " << locked << " + " << pay;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        if (sle->isFieldPresent(sfLockedAmount))
        {
            (*sle)[sfLockedAmount] += pay;
        }
        else
        {
            sle->setFieldU64(sfLockedAmount, pay);
        }

        view.update(sle);
    }

    {
        uint64_t const issuanceEscrowed = (*sleIssuance)[~sfLockedAmount].valueOr(0);
        auto const pay = amount.mpt().value();

        if (!canAdd(STAmount(mptIssue, issuanceEscrowed), STAmount(mptIssue, pay)))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "lockEscrowMPT: overflow on issuance "
                               "locked amount for "
                            << mptIssue.getMptID() << ": " << issuanceEscrowed << " + " << pay;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        if (sleIssuance->isFieldPresent(sfLockedAmount))
        {
            (*sleIssuance)[sfLockedAmount] += pay;
        }
        else
        {
            sleIssuance->setFieldU64(sfLockedAmount, pay);
        }

        view.update(sleIssuance);
    }
    return tesSUCCESS;
}

/** Release MPT funds from escrow and credit the recipient.
 *
 *  Always decrements `sfLockedAmount` on both the sender's `MPToken` SLE and
 *  the `MPTIssuance` SLE by `grossAmount`. The recipient path then diverges:
 *  - Receiver is the issuer: `sfOutstandingAmount` on the issuance is
 *    decremented by `netAmount` — tokens return to the issuer and retire.
 *  - Receiver is a third party: `sfMPTAmount` on the receiver's `MPToken` is
 *    incremented by `netAmount`.
 *  When `fixTokenEscrowV1` is enabled, `grossAmount` may exceed `netAmount`;
 *  the difference (the transfer fee) is additionally subtracted from
 *  `sfOutstandingAmount` because those tokens effectively retire as fee income.
 *  All arithmetic is guarded by `canSubtract`/`canAdd`; failures are
 *  `LCOV_EXCL_LINE` and indicate invariant violations.
 *
 *  @param view The mutable ledger state.
 *  @param sender The escrow grantor; must not be the issuer.
 *  @param receiver The escrow grantee (may be the issuer).
 *  @param netAmount The net MPT amount credited to the receiver after fees.
 *  @param grossAmount The gross MPT amount unlocked from escrow (>= netAmount).
 *  @param j Logging sink.
 *  @return `tesSUCCESS`, or a `tec`/`tef` error on missing SLEs or
 *      arithmetic guard failure.
 */
TER
unlockEscrowMPT(
    ApplyView& view,
    AccountID const& sender,
    AccountID const& receiver,
    STAmount const& netAmount,
    STAmount const& grossAmount,
    beast::Journal j)
{
    if (!view.rules().enabled(fixTokenEscrowV1))
    {
        XRPL_ASSERT(netAmount == grossAmount, "xrpl::unlockEscrowMPT : netAmount == grossAmount");
    }

    auto const& issuer = netAmount.getIssuer();
    auto const& mptIssue = netAmount.get<MPTIssue>();
    auto const mptID = keylet::mptIssuance(mptIssue.getMptID());
    auto sleIssuance = view.peek(mptID);
    if (!sleIssuance)
    {  // LCOV_EXCL_START
        JLOG(j.error()) << "unlockEscrowMPT: MPT issuance not found for " << mptIssue.getMptID();
        return tecOBJECT_NOT_FOUND;
    }  // LCOV_EXCL_STOP

    {
        if (!sleIssuance->isFieldPresent(sfLockedAmount))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "unlockEscrowMPT: no locked amount in issuance for "
                            << mptIssue.getMptID();
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        auto const locked = sleIssuance->getFieldU64(sfLockedAmount);
        auto const redeem = grossAmount.mpt().value();

        if (!canSubtract(STAmount(mptIssue, locked), STAmount(mptIssue, redeem)))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "unlockEscrowMPT: insufficient locked amount for "
                            << mptIssue.getMptID() << ": " << locked << " < " << redeem;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        auto const newLocked = locked - redeem;
        if (newLocked == 0)
        {
            sleIssuance->makeFieldAbsent(sfLockedAmount);
        }
        else
        {
            sleIssuance->setFieldU64(sfLockedAmount, newLocked);
        }
        view.update(sleIssuance);
    }

    if (issuer != receiver)
    {
        auto const mptokenID = keylet::mptoken(mptID.key, receiver);
        auto sle = view.peek(mptokenID);
        if (!sle)
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "unlockEscrowMPT: MPToken not found for " << receiver;
            return tecOBJECT_NOT_FOUND;
        }  // LCOV_EXCL_STOP

        auto current = sle->getFieldU64(sfMPTAmount);
        auto delta = netAmount.mpt().value();

        if (!canAdd(STAmount(mptIssue, current), STAmount(mptIssue, delta)))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "unlockEscrowMPT: overflow on MPTAmount for " << to_string(receiver)
                            << ": " << current << " + " << delta;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        (*sle)[sfMPTAmount] += delta;
        view.update(sle);
    }
    else
    {
        auto const outstanding = sleIssuance->getFieldU64(sfOutstandingAmount);
        auto const redeem = netAmount.mpt().value();

        if (!canSubtract(STAmount(mptIssue, outstanding), STAmount(mptIssue, redeem)))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "unlockEscrowMPT: insufficient outstanding amount for "
                            << mptIssue.getMptID() << ": " << outstanding << " < " << redeem;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        sleIssuance->setFieldU64(sfOutstandingAmount, outstanding - redeem);
        view.update(sleIssuance);
    }

    if (issuer == sender)
    {  // LCOV_EXCL_START
        JLOG(j.error()) << "unlockEscrowMPT: sender is the issuer, "
                           "cannot unlock MPTs.";
        return tecINTERNAL;
    }  // LCOV_EXCL_STOP
    auto const mptokenID = keylet::mptoken(mptID.key, sender);
    auto sle = view.peek(mptokenID);
    if (!sle)
    {  // LCOV_EXCL_START
        JLOG(j.error()) << "unlockEscrowMPT: MPToken not found for " << sender;
        return tecOBJECT_NOT_FOUND;
    }  // LCOV_EXCL_STOP

    if (!sle->isFieldPresent(sfLockedAmount))
    {  // LCOV_EXCL_START
        JLOG(j.error()) << "unlockEscrowMPT: no locked amount in MPToken for " << to_string(sender);
        return tecINTERNAL;
    }  // LCOV_EXCL_STOP

    auto const locked = sle->getFieldU64(sfLockedAmount);
    auto const delta = grossAmount.mpt().value();

    if (!canSubtract(STAmount(mptIssue, locked), STAmount(mptIssue, delta)))
    {  // LCOV_EXCL_START
        JLOG(j.error()) << "unlockEscrowMPT: insufficient locked amount for " << to_string(sender)
                        << ": " << locked << " < " << delta;
        return tecINTERNAL;
    }  // LCOV_EXCL_STOP

    auto const newLocked = locked - delta;
    if (newLocked == 0)
    {
        sle->makeFieldAbsent(sfLockedAmount);
    }
    else
    {
        sle->setFieldU64(sfLockedAmount, newLocked);
    }
    view.update(sle);

    // Note: The gross amount is the amount that was locked, the net
    // amount is the amount that is being unlocked. The difference is the fee
    // that was charged for the transfer. If this difference is greater than
    // zero, we need to update the outstanding amount.
    auto const diff = grossAmount.mpt().value() - netAmount.mpt().value();
    if (diff != 0)
    {
        auto const outstanding = sleIssuance->getFieldU64(sfOutstandingAmount);
        if (!canSubtract(STAmount(mptIssue, outstanding), STAmount(mptIssue, diff)))
        {  // LCOV_EXCL_START
            JLOG(j.error()) << "unlockEscrowMPT: insufficient outstanding amount for "
                            << mptIssue.getMptID() << ": " << outstanding << " < " << diff;
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        sleIssuance->setFieldU64(sfOutstandingAmount, outstanding - diff);
        view.update(sleIssuance);
    }
    return tesSUCCESS;
}

/** Low-level primitive: insert a new `MPToken` SLE and link it into the owner directory.
 *
 *  Inserts the SLE unconditionally; does not check for duplicates, enforce
 *  reserves, or verify issuance validity. Callers must perform those checks
 *  before invoking this function. Called by `checkCreateMPT` and
 *  `authorizeMPToken`.
 *
 *  @param view The mutable ledger state.
 *  @param mptIssuanceID The issuance the token belongs to.
 *  @param account The account that will own the `MPToken`.
 *  @param flags Initial `sfFlags` value for the new SLE.
 *  @return `tesSUCCESS`, or `tecDIR_FULL` if the owner directory is full.
 */
TER
createMPToken(
    ApplyView& view,
    MPTID const& mptIssuanceID,
    AccountID const& account,
    std::uint32_t const flags)
{
    auto const mptokenKey = keylet::mptoken(mptIssuanceID, account);

    auto const ownerNode =
        view.dirInsert(keylet::ownerDir(account), mptokenKey, describeOwnerDir(account));

    if (!ownerNode)
        return tecDIR_FULL;  // LCOV_EXCL_LINE

    auto mptoken = std::make_shared<SLE>(mptokenKey);
    (*mptoken)[sfAccount] = account;
    (*mptoken)[sfMPTokenIssuanceID] = mptIssuanceID;
    (*mptoken)[sfFlags] = flags;
    (*mptoken)[sfOwnerNode] = *ownerNode;

    view.insert(mptoken);

    return tesSUCCESS;
}

/** Idempotently ensure a `MPToken` holding exists for `holder`.
 *
 *  Succeeds immediately if `holder` is the issuer or if the `MPToken` SLE
 *  already exists. Otherwise calls `createMPToken` and increments the owner
 *  count. Suitable for apply-phase callers that need to auto-create a holding
 *  without performing the full reserve and issuance validity checks that
 *  `addEmptyHolding` performs.
 *
 *  @param view The mutable ledger state.
 *  @param mptIssue The MPT issuance the holder will hold.
 *  @param holder The account to receive the holding.
 *  @param j Logging sink.
 *  @return `tesSUCCESS`, `tecDIR_FULL` if the owner directory is full, or
 *      `tecINTERNAL` if the holder account SLE is missing.
 */
TER
checkCreateMPT(
    xrpl::ApplyView& view,
    xrpl::MPTIssue const& mptIssue,
    xrpl::AccountID const& holder,
    beast::Journal j)
{
    if (mptIssue.getIssuer() == holder)
        return tesSUCCESS;

    auto const mptIssuanceID = keylet::mptIssuance(mptIssue.getMptID());
    auto const mptokenID = keylet::mptoken(mptIssuanceID.key, holder);
    if (!view.exists(mptokenID))
    {
        if (auto const err = createMPToken(view, mptIssue.getMptID(), holder, 0);
            !isTesSuccess(err))
        {
            return err;
        }
        auto const sleAcct = view.peek(keylet::account(holder));
        if (!sleAcct)
        {
            return tecINTERNAL;
        }
        adjustOwnerCount(view, sleAcct, 1, j);
    }
    return tesSUCCESS;
}

/** Return the configured supply cap for an MPT issuance.
 *
 *  Returns `sfMaximumAmount` when present, or `kMAX_MP_TOKEN_AMOUNT` (2^63−1)
 *  when the field is absent, representing an uncapped issuance. The result is
 *  always non-negative and fits in a `std::int64_t`.
 *
 *  @param sleIssuance The `MPTIssuance` SLE to query.
 *  @return The maximum allowed outstanding amount.
 */
std::int64_t
maxMPTAmount(SLE const& sleIssuance)
{
    return sleIssuance[~sfMaximumAmount].value_or(kMAX_MP_TOKEN_AMOUNT);
}

/** Compute remaining issuance headroom from a pre-read SLE.
 *
 *  Returns `maxMPTAmount(sleIssuance) - sfOutstandingAmount`. May transiently
 *  be negative when the payment engine allows `OutstandingAmount` to exceed
 *  `MaximumAmount` under `AllowMPTOverflow::Yes`.
 *
 *  @param sleIssuance The `MPTIssuance` SLE to query.
 *  @return Headroom as a signed 64-bit integer; may be negative.
 */
std::int64_t
availableMPTAmount(SLE const& sleIssuance)
{
    auto const max = maxMPTAmount(sleIssuance);
    auto const outstanding = sleIssuance[sfOutstandingAmount];
    return max - outstanding;
}

/** Compute remaining issuance headroom by reading the SLE from the view.
 *
 *  Convenience overload that performs the SLE lookup. Unlike the SLE-taking
 *  overload, throws `std::runtime_error` if the issuance SLE is missing —
 *  a missing issuance at this call site indicates ledger consistency failure
 *  rather than a user error, so throwing is appropriate.
 *
 *  @param view The ledger state to query.
 *  @param mptID The `MPTID` of the issuance.
 *  @return Headroom as a signed 64-bit integer; may be negative.
 *  @throws std::runtime_error if the `MPTIssuance` SLE is absent.
 */
std::int64_t
availableMPTAmount(ReadView const& view, MPTID const& mptID)
{
    auto const sle = view.read(keylet::mptIssuance(mptID));
    if (!sle)
        Throw<std::runtime_error>(transHuman(tecINTERNAL));
    return availableMPTAmount(*sle);
}

/** Determine whether crediting `sendAmount` would overflow the outstanding supply.
 *
 *  Two distinct overflow thresholds are used:
 *  - `AllowMPTOverflow::No` (direct send): checks `outstandingAmount + sendAmount`
 *    against `maximumAmount`. Used by `directSendNoFee` where a strict cap is
 *    enforced.
 *  - `AllowMPTOverflow::Yes` (payment engine): uses `UINT64_MAX` as the ceiling
 *    to allow transient in-flight values that exceed `maximumAmount` while
 *    routing is in progress. The caller is responsible for validating the
 *    final settled amount.
 *
 *  @param sendAmount The proposed additional issuance; must be non-negative.
 *  @param outstandingAmount Current `sfOutstandingAmount` from the issuance SLE.
 *  @param maximumAmount The configured cap (`sfMaximumAmount` or `kMAX_MP_TOKEN_AMOUNT`).
 *  @param allowOverflow Selects which ceiling to apply.
 *  @return `true` if adding `sendAmount` would exceed the applicable limit.
 */
bool
isMPTOverflow(
    std::int64_t sendAmount,
    std::uint64_t outstandingAmount,
    std::int64_t maximumAmount,
    AllowMPTOverflow allowOverflow)
{
    std::uint64_t const limit = (allowOverflow == AllowMPTOverflow::Yes)
        ? std::numeric_limits<std::uint64_t>::max()
        : maximumAmount;
    return (sendAmount > maximumAmount || outstandingAmount > (limit - sendAmount));
}

/** Determine how much of an MPT an issuer may sell via their own offer.
 *
 *  Reads the available headroom from the issuance SLE and passes it through
 *  the `ReadView::balanceHookSelfIssueMPT` hook, which in a `PaymentSandbox`
 *  context deducts `selfDebit` to prevent double-counting tokens already
 *  committed by in-flight issuer sell steps.
 *
 *  Returns a zero `STAmount` for the issue if the issuance SLE is absent.
 *
 *  @param view The ledger state (or sandbox) to query.
 *  @param issue The MPT issuance.
 *  @return The spendable issuer balance after the hook adjustment.
 */
STAmount
issuerFundsToSelfIssue(ReadView const& view, MPTIssue const& issue)
{
    STAmount amount{issue};

    auto const sle = view.read(keylet::mptIssuance(issue));
    if (!sle)
        return amount;
    auto const available = availableMPTAmount(*sle);
    return view.balanceHookSelfIssueMPT(issue, available);
}

/** Record that the issuer has sold `amount` of their own MPT in the current step.
 *
 *  Delegates to `ApplyView::issuerSelfDebitHookMPT`, which in a
 *  `PaymentSandbox` context updates the `selfDebit` field in `DeferredCredits`
 *  so that `issuerFundsToSelfIssue` correctly limits subsequent issuer sell
 *  steps within the same payment.
 *
 *  @param view The mutable apply view (typically a `PaymentSandbox`).
 *  @param issue The MPT issuance.
 *  @param amount The amount the issuer just sold.
 */
void
issuerSelfDebitHookMPT(ApplyView& view, MPTIssue const& issue, std::uint64_t amount)
{
    auto const available = availableMPTAmount(view, issue);
    view.issuerSelfDebitHookMPT(issue, amount, available);
}

/** Layered permission check for MPT use in DEX and payment transactions.
 *
 *  Non-MPT assets (XRP, IOU) pass immediately. For MPT assets the following
 *  conditions must all hold:
 *  1. The issuer account exists.
 *  2. The `MPTIssuance` SLE exists.
 *  3. The issuance is not globally locked (`lsfMPTLocked`).
 *  4. The issuance has `lsfMPTCanTrade` set.
 *  5. For non-issuers: `lsfMPTCanTransfer` is set and the account's `MPToken`
 *     (if present) is not individually locked.
 *
 *  A missing `MPToken` for a non-issuer returns `tesSUCCESS` intentionally —
 *  certain transaction types auto-create the holding during apply; those
 *  transactors perform their own check for a missing `MPToken` when it matters.
 *
 *  @param view The ledger state to query.
 *  @param txType The transaction type; must be one of the MPT-allowed types.
 *  @param asset The asset being used; non-MPT assets always pass.
 *  @param accountID The account whose permissions are checked.
 *  @return `tesSUCCESS` if permitted; `tecLOCKED`, `tecNO_PERMISSION`,
 *      `tecNO_ISSUER`, `tecOBJECT_NOT_FOUND`, or `tefINTERNAL` otherwise.
 */
static TER
checkMPTAllowed(ReadView const& view, TxType txType, Asset const& asset, AccountID const& accountID)
{
    if (!asset.holds<MPTIssue>())
        return tesSUCCESS;

    auto const& issuanceID = asset.get<MPTIssue>().getMptID();
    auto const validTx = txType == ttAMM_CREATE || txType == ttAMM_DEPOSIT ||
        txType == ttAMM_WITHDRAW || txType == ttOFFER_CREATE || txType == ttCHECK_CREATE ||
        txType == ttCHECK_CASH || txType == ttPAYMENT;
    XRPL_ASSERT(validTx, "xrpl::checkMPTAllowed : all MPT tx or DEX");
    if (!validTx)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const& issuer = asset.getIssuer();
    if (!view.exists(keylet::account(issuer)))
        return tecNO_ISSUER;  // LCOV_EXCL_LINE

    auto const issuanceKey = keylet::mptIssuance(issuanceID);
    auto const issuanceSle = view.read(issuanceKey);
    if (!issuanceSle)
        return tecOBJECT_NOT_FOUND;  // LCOV_EXCL_LINE

    auto const flags = issuanceSle->getFlags();

    if ((flags & lsfMPTLocked) != 0u)
        return tecLOCKED;  // LCOV_EXCL_LINE
    if ((flags & lsfMPTCanTrade) == 0)
        return tecNO_PERMISSION;

    if (accountID != issuer)
    {
        if ((flags & lsfMPTCanTransfer) == 0)
            return tecNO_PERMISSION;

        auto const mptSle = view.read(keylet::mptoken(issuanceKey.key, accountID));
        // Allow a missing MPToken to succeed: some transaction types create the
        // MPToken during apply and perform their own missing-token check later.
        if (!mptSle)
            return tesSUCCESS;

        if (mptSle->isFlag(lsfMPTLocked))
            return tecLOCKED;
    }

    return tesSUCCESS;
}

/** Public wrapper around `checkMPTAllowed` for non-payment transaction types.
 *
 *  Asserts that the caller is not passing `ttPAYMENT` — payment paths use a
 *  separate permission check (`isDEXAllowed`) and must not call this function
 *  directly.
 *
 *  @param view The ledger state to query.
 *  @param txType The transaction type; must not be `ttPAYMENT`.
 *  @param asset The asset being used.
 *  @param accountID The account whose permissions are checked.
 *  @return The result of `checkMPTAllowed`.
 */
TER
checkMPTTxAllowed(
    ReadView const& view,
    TxType txType,
    Asset const& asset,
    AccountID const& accountID)
{
    XRPL_ASSERT(txType != ttPAYMENT, "xrpl::checkMPTTxAllowed : not payment");
    return checkMPTAllowed(view, txType, asset, accountID);
}

}  // namespace xrpl
