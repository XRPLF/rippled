/** @file
 *  Monolithic implementation of all eight cross-chain bridge transaction types.
 *
 *  Provides `preflight`, `preclaim`, and `doApply` for:
 *  `XChainCreateBridge`, `BridgeModify`, `XChainClaim`, `XChainCommit`,
 *  `XChainCreateClaimID`, `XChainAddClaimAttestation`,
 *  `XChainAddAccountCreateAttestation`, and `XChainCreateAccountCommit`.
 *
 *  The bridge protocol connects a *locking chain* and an *issuing chain*
 *  without an exchange rate: assets locked on one side are represented 1-to-1
 *  as wrapped tokens on the other.  A quorum of witness servers attests to
 *  cross-chain events; `claimHelper` and `onNewAttestations` implement the
 *  shared quorum mechanics; `finalizeClaimHelper` handles atomic fund transfer
 *  and reward distribution via a nested `PaymentSandbox`.
 *
 *  See `docs/bridge/spec.md` for the full protocol specification.
 */
#include <xrpl/tx/transactors/bridge/XChainBridge.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/PaymentSandbox.h>
#include <xrpl/ledger/RawView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XChainAttestations.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/SignerEntries.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/paths/Flow.h>
#include <xrpl/tx/paths/detail/Steps.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace xrpl {

/*
   Bridges connect two independent ledgers: a "locking chain" and an "issuing
   chain". An asset can be moved from the locking chain to the issuing chain by
   putting it into trust on the locking chain, and issuing a "wrapped asset"
   that represents the locked asset on the issuing chain.

   Note that a bridge is not an exchange. There is no exchange rate: one wrapped
   asset on the issuing chain always represents one asset in trust on the
   locking chain. The bridge also does not exchange an asset on the locking
   chain for an asset on the issuing chain.

   A good model for thinking about bridges is a box that contains an infinite
   number of "wrapped tokens". When a token from the locking chain
   (locking-chain-token) is put into the box, a wrapped token is taken out of
   the box and put onto the issuing chain (issuing-chain-token). No one can use
   the locking-chain-token while it remains in the box. When an
   issuing-chain-token is returned to the box, one locking-chain-token is taken
   out of the box and put back onto the locking chain.

   This requires a way to put assets into trust on one chain (put a
   locking-chain-token into the box). A regular XRP account is used for this.
   This account is called a "door account". Much in the same way that a door is
   used to go from one room to another, a door account is used to move from one
   chain to another. This account will be jointly controlled by a set of witness
   servers by using the ledger's multi-signature support. The master key will be
   disabled. These witness servers are trusted in the sense that if a quorum of
   them collude, they can steal the funds put into trust.

   This also requires a way to prove that assets were put into the box - either
   a locking-chain-token on the locking chain or returning an
   issuing-chain-token on the issuing chain. A set of servers called "witness
   servers" fill this role. These servers watch the ledger for these
   transactions, and attest that the given events happened on the different
   chains by signing messages with the event information.

   There needs to be a way to prevent the attestations from the witness
   servers from being used more than once. "Claim ids" fill this role. A claim
   id must be acquired on the destination chain before the asset is "put into
   the box" on the source chain. This claim id has a unique id, and once it is
   destroyed it can never exist again (it's a simple counter). The attestations
   reference this claim id, and are accumulated on the claim id. Once a quorum
   is reached, funds can move. Once the funds move, the claim id is destroyed.

   Finally, a claim id requires that the sender has an account on the
   destination chain. For some chains, this can be a problem - especially if
   the wrapped asset represents XRP, and XRP is needed to create an account.
   There's a bootstrap problem. To address this, there is a special transaction
   used to create accounts. This transaction does not require a claim id.

   See the document "docs/bridge/spec.md" for a full description of how
   bridges and their transactions work.
*/

namespace {

/** Verify that a public key is authorised to sign on behalf of a signer account.
 *
 *  Three cases are handled: if the account does not exist on the ledger the
 *  public key must derive to `attestationSignerAccount` (i.e. it must be the
 *  account's hypothetical master key); if the account exists and the key
 *  derives to that account ID, the master key must not be disabled; if the key
 *  does not derive to the account ID, it must match the account's
 *  `sfRegularKey`.
 *
 *  Called both during `preclaim` and again inside `onNewAttestations` as a
 *  defensive guard against future refactoring removing the `preclaim` check.
 *
 *  @param view           Read-only ledger view used to check account state.
 *  @param signersList    Map of authorised signer account IDs to their weights.
 *  @param attestationSignerAccount  The account ID claimed by the attestation.
 *  @param pk             Public key that was used to sign the attestation.
 *  @param j              Journal for diagnostic logging.
 *  @return `tesSUCCESS` if the key is valid for the account, or
 *      `tecNO_PERMISSION` / `tecXCHAIN_BAD_PUBLIC_KEY_ACCOUNT_PAIR` on failure.
 */
TER
checkAttestationPublicKey(
    ReadView const& view,
    std::unordered_map<AccountID, std::uint32_t> const& signersList,
    AccountID const& attestationSignerAccount,
    PublicKey const& pk,
    beast::Journal j)
{
    if (!signersList.contains(attestationSignerAccount))
    {
        return tecNO_PERMISSION;
    }

    AccountID const accountFromPK = calcAccountID(pk);

    if (auto const sleAttestationSigningAccount =
            view.read(keylet::account(attestationSignerAccount)))
    {
        if (accountFromPK == attestationSignerAccount)
        {
            // master key
            if ((sleAttestationSigningAccount->getFieldU32(sfFlags) & lsfDisableMaster) != 0u)
            {
                JLOG(j.trace()) << "Attempt to add an attestation with "
                                   "disabled master key.";
                return tecXCHAIN_BAD_PUBLIC_KEY_ACCOUNT_PAIR;
            }
        }
        else
        {
            // regular key
            if (std::optional<AccountID> const regularKey =
                    (*sleAttestationSigningAccount)[~sfRegularKey];
                regularKey != accountFromPK)
            {
                if (!regularKey)
                {
                    JLOG(j.trace()) << "Attempt to add an attestation with "
                                       "account present and non-present regular key.";
                }
                else
                {
                    JLOG(j.trace()) << "Attempt to add an attestation with "
                                       "account present and mismatched "
                                       "regular key/public key.";
                }
                return tecXCHAIN_BAD_PUBLIC_KEY_ACCOUNT_PAIR;
            }
        }
    }
    else
    {
        // account does not exist.
        if (calcAccountID(pk) != attestationSignerAccount)
        {
            JLOG(j.trace()) << "Attempt to add an attestation with non-existant account "
                               "and mismatched pk/account pair.";
            return tecXCHAIN_BAD_PUBLIC_KEY_ACCOUNT_PAIR;
        }
    }

    return tesSUCCESS;
}

/** Whether the destination field in attestations must be matched.
 *
 *  Used to distinguish automatic settlement triggered by `addAttestation`
 *  (`Check` — destination must match the attested value) from an explicit
 *  `XChainClaim` transaction (`Ignore` — any quorum for the amount suffices).
 */
enum class CheckDst { Check, Ignore };

/** Check whether attestations reach quorum for the given match fields.
 *
 *  Stale attestations (signer key disabled, regular key rotated, or signer
 *  removed from the signer list) are stripped before counting weight.  If the
 *  accumulated weight meets or exceeds `quorum`, the reward accounts for the
 *  qualifying attestations are returned.
 *
 *  @param attestations  Mutable attestation collection; stale entries are
 *      erased in-place.
 *  @param view          Read-only ledger view for key-validity checks.
 *  @param toMatch       Fields that each attestation must match (amount, chain,
 *      and optionally destination).
 *  @param checkDst      Whether the destination field must agree with
 *      `toMatch.dst`.  Use `Check` for automatic settlement, `Ignore` for
 *      explicit claims.
 *  @param quorum        Minimum total weight required.
 *  @param signersList   Map from signer account ID to weight.
 *  @param j             Journal for diagnostic logging.
 *  @return Reward account list on quorum success, or `tecXCHAIN_CLAIM_NO_QUORUM`
 *      wrapped in `Unexpected` if quorum is not reached.
 */
template <class TAttestation>
Expected<std::vector<AccountID>, TER>
claimHelper(
    XChainAttestationsBase<TAttestation>& attestations,
    ReadView const& view,
    typename TAttestation::MatchFields const& toMatch,
    CheckDst checkDst,
    std::uint32_t quorum,
    std::unordered_map<AccountID, std::uint32_t> const& signersList,
    beast::Journal j)
{
    // Remove attestations that are not valid signers. They may be no longer
    // part of the signers list, or their master key may have been disabled,
    // or their regular key may have changed
    attestations.eraseIf([&](auto const& a) {
        return checkAttestationPublicKey(view, signersList, a.keyAccount, a.publicKey, j) !=
            tesSUCCESS;
    });

    // Check if we have quorum for the amount specified on the new claimAtt
    std::vector<AccountID> rewardAccounts;
    rewardAccounts.reserve(attestations.size());
    std::uint32_t weight = 0;
    for (auto const& a : attestations)
    {
        auto const matchR = a.match(toMatch);
        // The dest must match if claimHelper is being run as a result of an add
        // attestation transaction. The dst does not need to match if the
        // claimHelper is being run using an explicit claim transaction.
        using enum AttestationMatch;
        if (matchR == NonDstMismatch || (checkDst == CheckDst::Check && matchR != Match))
            continue;
        auto i = signersList.find(a.keyAccount);
        if (i == signersList.end())
        {
            // LCOV_EXCL_START
            UNREACHABLE("xrpl::claimHelper : invalid inputs");  // should have already
                                                                // been checked
            continue;
            // LCOV_EXCL_STOP
        }
        weight += i->second;
        rewardAccounts.push_back(a.rewardAccount);
    }

    if (weight >= quorum)
        return rewardAccounts;

    return Unexpected(tecXCHAIN_CLAIM_NO_QUORUM);
}

/** Result of handling a new attestation event via `onNewAttestations`. */
struct OnNewAttestationResult
{
    /** Reward accounts if quorum was reached for the newly submitted
     *  attestation's parameters; `std::nullopt` otherwise.
     *
     *  A `nullopt` here means no quorum (or no reward distribution needed),
     *  whereas an empty vector means quorum was reached but no reward is owed.
     */
    std::optional<std::vector<AccountID>> rewardAccounts;

    /** True if the attestation collection changed in any way
     *  (entry added, replaced, or removed) during this call. */
    bool changed{false};
};

/** Handle one or more new attestation events.
 *
 *  Adds or replaces each incoming attestation in the collection after
 *  re-validating the public key (defensive redundancy with the preclaim check).
 *  Attestations whose key is no longer valid are silently skipped.  After all
 *  updates, `claimHelper` is called with `CheckDst::Check` to determine whether
 *  the newly submitted attestations push the collection over quorum.
 *
 *  @note Called `onNewAttestations` rather than `add` because it orchestrates
 *      key validation, collection updates, and a quorum check — the attestation
 *      may not be added at all if its key is invalid.
 *
 *  @param attestations  Mutable attestation collection to update.
 *  @param view          Read-only ledger view for key-validity checks.
 *  @param attBegin      Pointer to the first incoming attestation.
 *  @param attEnd        One-past-the-end pointer for the incoming attestations.
 *  @param quorum        Minimum total weight required.
 *  @param signersList   Map from signer account ID to weight.
 *  @param j             Journal for diagnostic logging.
 *  @return `OnNewAttestationResult` with optional reward accounts (set only if
 *      quorum is now reached for the submitted attestation's parameters) and a
 *      flag indicating whether the collection changed.
 */
template <class TAttestation>
[[nodiscard]] OnNewAttestationResult
onNewAttestations(
    XChainAttestationsBase<TAttestation>& attestations,
    ReadView const& view,
    typename TAttestation::TSignedAttestation const* attBegin,
    typename TAttestation::TSignedAttestation const* attEnd,
    std::uint32_t quorum,
    std::unordered_map<AccountID, std::uint32_t> const& signersList,
    beast::Journal j)
{
    bool changed = false;
    for (auto att = attBegin; att != attEnd; ++att)
    {
        auto const ter = checkAttestationPublicKey(
            view, signersList, att->attestationSignerAccount, att->publicKey, j);
        if (!isTesSuccess(ter))
        {
            // The checkAttestationPublicKey is not strictly necessary here (it
            // should be checked in a preclaim step), but it would be bad to let
            // this slip through if that changes, and the check is relatively
            // cheap, so we check again
            continue;
        }

        auto const& claimSigningAccount = att->attestationSignerAccount;
        if (auto i = std::find_if(
                attestations.begin(),
                attestations.end(),
                [&](auto const& a) { return a.keyAccount == claimSigningAccount; });
            i != attestations.end())
        {
            // existing attestation
            // replace old attestation with new attestation
            *i = TAttestation{*att};
            changed = true;
        }
        else
        {
            attestations.emplaceBack(*att);
            changed = true;
        }
    }

    auto r = claimHelper(
        attestations,
        view,
        typename TAttestation::MatchFields{*attBegin},
        CheckDst::Check,
        quorum,
        signersList,
        j);

    if (!r.has_value())
        return {.rewardAccounts = std::nullopt, .changed = changed};

    return {std::move(r.value()), changed};
};

/** Check quorum for an explicit `XChainClaim` transaction.
 *
 *  Delegates to `claimHelper` with `CheckDst::Ignore` — the destination
 *  recorded in each attestation is not checked because the claimant has
 *  explicitly chosen where to direct the funds.
 *
 *  @param attestations       Mutable attestation collection; stale entries are
 *      purged by `claimHelper`.
 *  @param view               Read-only ledger view.
 *  @param sendingAmount      Amount that was committed on the source chain.
 *  @param wasLockingChainSend  `true` if the commit occurred on the locking
 *      chain.
 *  @param quorum             Minimum weight required.
 *  @param signersList        Map from signer account ID to weight.
 *  @param j                  Journal for diagnostic logging.
 *  @return Reward account list on success, or `tecXCHAIN_CLAIM_NO_QUORUM`
 *      wrapped in `Unexpected`.
 */
Expected<std::vector<AccountID>, TER>
onClaim(
    XChainClaimAttestations& attestations,
    ReadView const& view,
    STAmount const& sendingAmount,
    bool wasLockingChainSend,
    std::uint32_t quorum,
    std::unordered_map<AccountID, std::uint32_t> const& signersList,
    beast::Journal j)
{
    XChainClaimAttestation::MatchFields const toMatch{
        sendingAmount, wasLockingChainSend, std::nullopt};
    return claimHelper(attestations, view, toMatch, CheckDst::Ignore, quorum, signersList, j);
}

/** Whether `transferHelper` may create the destination account on the fly. */
enum class CanCreateDstPolicy { No, Yes };

/** Deposit-auth bypass policy for `transferHelper`.
 *
 *  `DstCanBypass` allows the destination account to bypass its own
 *  `lsfDepositAuth` flag when it is also the claim owner — i.e. the account is
 *  effectively sending funds to itself via an `XChainClaim`.
 */
enum class DepositAuthPolicy { Normal, DstCanBypass };

/** Account balance snapshot enabling fee-dipping in `transferHelper`.
 *
 *  Commit transactions are permitted to spend into the owner reserve to cover
 *  fees.  Passing this struct causes `transferHelper` to use `preFeeBalance`
 *  (the balance before the fee was deducted) when checking whether the source
 *  can afford the transfer, provided `account` and `postFeeBalance` match the
 *  current ledger state.  Other transactors should not seat this optional.
 */
struct TransferHelperSubmittingAccountInfo
{
    AccountID account;       ///< The submitting account's ID.
    STAmount preFeeBalance;  ///< Balance before the transaction fee was charged.
    STAmount postFeeBalance; ///< Current balance (after fee); used as a guard to
                             ///<   ensure the snapshot is still valid.
};

/** Transfer funds from the src account to the dst account

    @param psb The payment sandbox.
    @param src The source of funds.
    @param dst The destination for funds.
    @param dstTag Integer destination tag. Used to check if funds should be
           transferred to an account with a `RequireDstTag` flag set.
    @param claimOwner Owner of the claim ledger object.
    @param amt Amount to transfer from the src account to the dst account.
    @param canCreate Flag to determine if accounts may be created using this
           transfer.
    @param depositAuthPolicy Flag to determine if dst can bypass deposit auth if
           it is also the claim owner.
    @param submittingAccountInfo If the transaction is allowed to dip into the
           reserve to pay fees, then this optional will be seated ("commit"
           transactions support this, other transactions should not).
    @param j Log

    @return tesSUCCESS if payment succeeds, otherwise the error code for the
            failure reason.
 */

TER
transferHelper(
    PaymentSandbox& psb,
    AccountID const& src,
    AccountID const& dst,
    std::optional<std::uint32_t> const& dstTag,
    std::optional<AccountID> const& claimOwner,
    STAmount const& amt,
    CanCreateDstPolicy canCreate,
    DepositAuthPolicy depositAuthPolicy,
    std::optional<TransferHelperSubmittingAccountInfo> const& submittingAccountInfo,
    beast::Journal j)
{
    if (dst == src)
        return tesSUCCESS;

    auto const dstK = keylet::account(dst);
    if (auto sleDst = psb.read(dstK))
    {
        // Check dst tag and deposit auth

        if (((sleDst->getFlags() & lsfRequireDestTag) != 0u) && !dstTag)
            return tecDST_TAG_NEEDED;

        // If the destination is the claim owner, and this is a claim
        // transaction, that's the dst account sending funds to itself. It
        // can bypass deposit auth.
        bool const canBypassDepositAuth =
            dst == claimOwner && depositAuthPolicy == DepositAuthPolicy::DstCanBypass;

        if (!canBypassDepositAuth && ((sleDst->getFlags() & lsfDepositAuth) != 0u) &&
            !psb.exists(keylet::depositPreauth(dst, src)))
        {
            return tecNO_PERMISSION;
        }
    }
    else if (!amt.native() || canCreate == CanCreateDstPolicy::No)
    {
        return tecNO_DST;
    }

    if (amt.native())
    {
        auto const sleSrc = psb.peek(keylet::account(src));
        XRPL_ASSERT(sleSrc, "xrpl::transferHelper : non-null source account");
        if (!sleSrc)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        {
            auto const ownerCount = sleSrc->getFieldU32(sfOwnerCount);
            auto const reserve = psb.fees().accountReserve(ownerCount);

            auto const availableBalance = [&]() -> STAmount {
                STAmount curBal = (*sleSrc)[sfBalance];
                // Checking that account == src and postFeeBalance == curBal is
                // not strictly necessary, but helps protect against future
                // changes
                if (!submittingAccountInfo || submittingAccountInfo->account != src ||
                    submittingAccountInfo->postFeeBalance != curBal)
                    return curBal;
                return submittingAccountInfo->preFeeBalance;
            }();

            if (availableBalance < amt + reserve)
            {
                return tecUNFUNDED_PAYMENT;
            }
        }

        auto sleDst = psb.peek(dstK);
        if (!sleDst)
        {
            if (canCreate == CanCreateDstPolicy::No)
            {
                // Already checked, but OK to check again
                return tecNO_DST;
            }
            if (amt < psb.fees().reserve)
            {
                JLOG(j.trace()) << "Insufficient payment to create account.";
                return tecNO_DST_INSUF_XRP;
            }

            // Create the account.
            sleDst = std::make_shared<SLE>(dstK);
            sleDst->setAccountID(sfAccount, dst);
            sleDst->setFieldU32(sfSequence, psb.seq());

            psb.insert(sleDst);
        }

        (*sleSrc)[sfBalance] = (*sleSrc)[sfBalance] - amt;
        (*sleDst)[sfBalance] = (*sleDst)[sfBalance] + amt;
        psb.update(sleSrc);
        psb.update(sleDst);

        return tesSUCCESS;
    }

    auto const result = flow(
        psb,
        amt,
        src,
        dst,
        STPathSet{},
        /*default path*/ true,
        /*partial payment*/ false,
        /*owner pays transfer fee*/ true,
        /*offer crossing*/ OfferCrossing::No,
        /*limit quality*/ std::nullopt,
        /*sendmax*/ std::nullopt,
        /*domain id*/ std::nullopt,
        j);

    if (auto const r = result.result(); isTesSuccess(r) || isTecClaim(r) || isTerRetry(r))
        return r;
    return tecXCHAIN_PAYMENT_FAILED;
}

/** Action to take on the claim ID when the door-to-destination transfer fails.
 *
 *  Regular claims use `KeepClaim` so the sender can retry; account-creation
 *  claims use `RemoveClaim` to unblock subsequent ordered creates — the witness
 *  servers attested truthfully even if the destination was unacceptable.
 */
enum class OnTransferFail {
    /** Remove the claim ID even if the transfer fails. */
    RemoveClaim,
    /** Preserve the claim ID so the transfer can be retried later. */
    KeepClaim
};

/** Three-phase result from `finalizeClaimHelper`.
 *
 *  Each phase may independently succeed or fail; callers inspect individual
 *  fields to decide whether the attestation list change should still be
 *  committed.  Use `ter()` for the priority-ordered overall code, or
 *  `isTesSuccess()` for a simple pass/fail check.
 */
struct FinalizeClaimHelperResult
{
    /** TER for transferring the main payment from the door to the destination.
     *  `std::nullopt` if this phase was not attempted. */
    std::optional<TER> mainFundsTer;

    /** TER for distributing witness-server rewards from the reward pool.
     *  `std::nullopt` if no reward accounts were present. */
    std::optional<TER> rewardTer;

    /** TER for erasing the claim ID SLE from the ledger.
     *  `std::nullopt` if SLE removal was not attempted. */
    std::optional<TER> rmSleTer;

    /** Returns `true` only when every attempted phase succeeded. */
    [[nodiscard]] bool
    isTesSuccess() const
    {
        return (!mainFundsTer || xrpl::isTesSuccess(*mainFundsTer)) &&
            (!rewardTer || xrpl::isTesSuccess(*rewardTer)) &&
            (!rmSleTer || xrpl::isTesSuccess(*rmSleTer));
    }

    /** Priority-ordered TER for the overall operation.
     *
     *  `tecINTERNAL` and `tef*` codes from any phase take precedence over
     *  ordinary `tec*` failures.  Among ordinary failures the phases are
     *  checked in order: `mainFundsTer`, `rewardTer`, `rmSleTer`.
     *
     *  @return `tesSUCCESS` if all attempted phases succeeded, otherwise the
     *      highest-priority non-success code.
     */
    [[nodiscard]] TER
    ter() const
    {
        if (isTesSuccess())
            return tesSUCCESS;

        if (mainFundsTer && (isTefFailure(*mainFundsTer) || *mainFundsTer == tecINTERNAL))
            return *mainFundsTer;
        if (rewardTer && (isTefFailure(*rewardTer) || *rewardTer == tecINTERNAL))
            return *rewardTer;
        if (rmSleTer && (isTefFailure(*rmSleTer) || *rmSleTer == tecINTERNAL))
            return *rmSleTer;

        if (mainFundsTer && !xrpl::isTesSuccess(*mainFundsTer))
            return *mainFundsTer;
        if (rewardTer && !xrpl::isTesSuccess(*rewardTer))
            return *rewardTer;
        if (rmSleTer && !xrpl::isTesSuccess(*rmSleTer))
            return *rmSleTer;
        return tesSUCCESS;
    }
};

/** Transfer funds from the door account to the dst and distribute rewards

    @param psb The payment sandbox.
    @param bridgeSpc Bridge
    @param dst The destination for funds.
    @param dstTag Integer destination tag. Used to check if funds should be
           transferred to an account with a `RequireDstTag` flag set.
    @param claimOwner Owner of the claim ledger object.
    @param sendingAmount Amount that was committed on the source chain.
    @param rewardPoolSrc Source of the funds for the reward pool (claim owner).
    @param rewardPool Amount to split among the rewardAccounts.
    @param rewardAccounts Account to receive the reward pool.
    @param srcChain Chain where the commit event occurred.
    @param sleClaimID sle for the claim id (may be NULL or XChainClaimID or
           XChainCreateAccountClaimID). Don't read fields that aren't in common
           with those two types and always check for NULL. Remove on success (if
           not null). Remove on fail if the onTransferFail flag is removeClaim.
    @param onTransferFail Flag to determine if the claim is removed on transfer
           failure. This is used for create account transactions where claims
           are removed so they don't block future txns.
    @param j Log

    @return FinalizeClaimHelperResult. See the comments in this struct for what
            the fields mean. The individual ters need to be returned instead of
            an overall ter because the caller needs this information if the
            attestation list changed or not.
*/

FinalizeClaimHelperResult
finalizeClaimHelper(
    PaymentSandbox& outerSb,
    STXChainBridge const& bridgeSpec,
    AccountID const& dst,
    std::optional<std::uint32_t> const& dstTag,
    AccountID const& claimOwner,
    STAmount const& sendingAmount,
    AccountID const& rewardPoolSrc,
    STAmount const& rewardPool,
    std::vector<AccountID> const& rewardAccounts,
    STXChainBridge::ChainType const srcChain,
    Keylet const& claimIDKeylet,
    OnTransferFail onTransferFail,
    DepositAuthPolicy depositAuthPolicy,
    beast::Journal j)
{
    FinalizeClaimHelperResult result;

    STXChainBridge::ChainType const dstChain = STXChainBridge::otherChain(srcChain);
    STAmount const thisChainAmount = [&] {
        STAmount r = sendingAmount;
        r.setIssue(bridgeSpec.issue(dstChain));
        return r;
    }();
    auto const& thisDoor = bridgeSpec.door(dstChain);

    {
        PaymentSandbox innerSb{&outerSb};
        // If distributing the reward pool fails, the mainFunds transfer should
        // be rolled back
        //
        // If the claim ID is removed, the rewards should be distributed
        // even if the mainFunds fails.
        //
        // If OnTransferFail::removeClaim, the claim should be removed even if
        // the rewards cannot be distributed.

        // transfer funds to the dst
        result.mainFundsTer = transferHelper(
            innerSb,
            thisDoor,
            dst,
            dstTag,
            claimOwner,
            thisChainAmount,
            CanCreateDstPolicy::Yes,
            depositAuthPolicy,
            std::nullopt,
            j);

        if (!isTesSuccess(*result.mainFundsTer) && onTransferFail == OnTransferFail::KeepClaim)
        {
            return result;
        }

        // handle the reward pool
        result.rewardTer = [&]() -> TER {
            if (rewardAccounts.empty())
                return tesSUCCESS;

            // distribute the reward pool
            // if the transfer failed, distribute the pool for "OnTransferFail"
            // cases (the attesters did their job)
            STAmount const share = [&] {
                auto const roundMode = innerSb.rules().enabled(fixXChainRewardRounding)
                    ? Number::RoundingMode::Downward
                    : Number::getround();
                SaveNumberRoundMode const _{Number::setround(roundMode)};

                STAmount const den{rewardAccounts.size()};
                return divide(rewardPool, den, rewardPool.asset());
            }();
            STAmount distributed = rewardPool.zeroed();
            for (auto const& rewardAccount : rewardAccounts)
            {
                auto const thTer = transferHelper(
                    innerSb,
                    rewardPoolSrc,
                    rewardAccount,
                    /*dstTag*/ std::nullopt,
                    // claim owner is not relevant to distributing rewards
                    /*claimOwner*/ std::nullopt,
                    share,
                    CanCreateDstPolicy::No,
                    DepositAuthPolicy::Normal,
                    std::nullopt,
                    j);

                if (thTer == tecUNFUNDED_PAYMENT || thTer == tecINTERNAL)
                    return thTer;

                if (isTesSuccess(thTer))
                    distributed += share;

                // let txn succeed if error distributing rewards (other than
                // inability to pay)
            }

            if (distributed > rewardPool)
                return tecINTERNAL;  // LCOV_EXCL_LINE

            return tesSUCCESS;
        }();

        if (!isTesSuccess(*result.rewardTer) &&
            (onTransferFail == OnTransferFail::KeepClaim || *result.rewardTer == tecINTERNAL))
        {
            return result;
        }

        if (!isTesSuccess(*result.mainFundsTer) || isTesSuccess(*result.rewardTer))
        {
            // Note: if the mainFunds transfer succeeds and the result transfer
            // fails, we don't apply the inner sandbox (i.e. the mainTransfer is
            // rolled back)
            innerSb.apply(outerSb);
        }
    }

    if (auto const sleClaimID = outerSb.peek(claimIDKeylet))
    {
        auto const cidOwner = (*sleClaimID)[sfAccount];
        {
            // Remove the claim id
            auto const sleOwner = outerSb.peek(keylet::account(cidOwner));
            auto const page = (*sleClaimID)[sfOwnerNode];
            if (!outerSb.dirRemove(keylet::ownerDir(cidOwner), page, sleClaimID->key(), true))
            {
                JLOG(j.fatal()) << "Unable to delete xchain seq number from owner.";
                result.rmSleTer = tefBAD_LEDGER;
                return result;
            }

            // Remove the claim id from the ledger
            outerSb.erase(sleClaimID);

            adjustOwnerCount(outerSb, sleOwner, -1, j);
        }
    }

    return result;
}

/** Retrieve the signer list and quorum for the door account that owns a bridge.
 *
 *  @param view      Read-only ledger view.
 *  @param sleBridge SLE of the bridge whose door account is queried.
 *  @param j         Journal for diagnostic logging.
 *  @return A tuple of `(signersList, quorum, ter)` where `signersList` maps
 *      each signer's account ID to its weight, `quorum` is the minimum
 *      required weight (initialised to `UINT32_MAX` on error), and `ter` is
 *      `tesSUCCESS`, `tecXCHAIN_NO_SIGNERS_LIST`, or `tecINTERNAL`.
 */
std::tuple<std::unordered_map<AccountID, std::uint32_t>, std::uint32_t, TER>
getSignersListAndQuorum(ReadView const& view, SLE const& sleBridge, beast::Journal j)
{
    std::unordered_map<AccountID, std::uint32_t> r;
    std::uint32_t q = std::numeric_limits<std::uint32_t>::max();

    AccountID const thisDoor = sleBridge[sfAccount];
    auto const sleDoor = [&] { return view.read(keylet::account(thisDoor)); }();

    if (!sleDoor)
    {
        return {r, q, tecINTERNAL};
    }

    auto const sleS = view.read(keylet::signers(sleBridge[sfAccount]));
    if (!sleS)
    {
        return {r, q, tecXCHAIN_NO_SIGNERS_LIST};
    }
    q = (*sleS)[sfSignerQuorum];

    auto const accountSigners = SignerEntries::deserialize(*sleS, j, "ledger");

    if (!accountSigners)
    {
        return {r, q, tecINTERNAL};
    }

    for (auto const& as : *accountSigners)
    {
        r[as.account] = as.weight;
    }

    return {std::move(r), q, tesSUCCESS};
};

/** Resolve a bridge SLE from a `bridgeSpec` by trying the locking-chain
 *  keylet first, then the issuing-chain keylet.
 *
 *  A bridge SLE exists on exactly one of the two chains (the chain whose door
 *  account is the submitting account).  This helper abstracts over that
 *  ambiguity for both read-only and mutable callers via a generic `getter`
 *  callable.
 *
 *  @tparam R       SLE type (`SLE` for mutable, `SLE const` for read-only).
 *  @tparam F       Callable `(STXChainBridge, ChainType) -> shared_ptr<R>`.
 *  @param getter   Concrete read or peek function.
 *  @param bridgeSpec  Bridge specification to look up.
 *  @return The matching SLE, or `nullptr` if not found on either chain.
 */
template <class R, class F>
std::shared_ptr<R>
readOrpeekBridge(F&& getter, STXChainBridge const& bridgeSpec)
{
    auto tryGet = [&](STXChainBridge::ChainType ct) -> std::shared_ptr<R> {
        if (auto r = getter(bridgeSpec, ct))
        {
            if ((*r)[sfXChainBridge] == bridgeSpec)
                return r;
        }
        return nullptr;
    };
    if (auto r = tryGet(STXChainBridge::ChainType::Locking))
        return r;
    return tryGet(STXChainBridge::ChainType::Issuing);
}

/** Peek (mutable access) at the bridge SLE for `bridgeSpec` in an apply view.
 *
 *  @param v           Mutable apply view.
 *  @param bridgeSpec  Bridge to look up.
 *  @return Mutable SLE pointer, or `nullptr` if not found.
 */
std::shared_ptr<SLE>
peekBridge(ApplyView& v, STXChainBridge const& bridgeSpec)
{
    return readOrpeekBridge<SLE>(
        [&v](STXChainBridge const& b, STXChainBridge::ChainType ct) -> std::shared_ptr<SLE> {
            return v.peek(keylet::bridge(b, ct));
        },
        bridgeSpec);
}

/** Read (const access) the bridge SLE for `bridgeSpec` from a read view.
 *
 *  @param v           Read-only view.
 *  @param bridgeSpec  Bridge to look up.
 *  @return Const SLE pointer, or `nullptr` if not found.
 */
std::shared_ptr<SLE const>
readBridge(ReadView const& v, STXChainBridge const& bridgeSpec)
{
    return readOrpeekBridge<SLE const>(
        [&v](STXChainBridge const& b, STXChainBridge::ChainType ct) -> std::shared_ptr<SLE const> {
            return v.read(keylet::bridge(b, ct));
        },
        bridgeSpec);
}

/** Apply a batch of regular cross-chain claim attestations to the ledger.
 *
 *  Updates the claim ID's attestation array and, if quorum is reached and the
 *  attestations include a destination, automatically settles the transfer via
 *  `finalizeClaimHelper`.  All mutations are buffered in a `PaymentSandbox`
 *  and applied atomically at the end.
 *
 *  @pre All attestations in `[attBegin, attEnd)` are consistent — they attest
 *      to the same event (amount, sending account, claim ID, etc.).
 *
 *  @param view         Mutable apply view.
 *  @param rawView      Raw view for final sandbox commit.
 *  @param attBegin     Iterator to the first incoming attestation.
 *  @param attEnd       One-past-the-end iterator.
 *  @param bridgeSpec   Bridge specification.
 *  @param srcChain     Chain where the commit event occurred.
 *  @param signersList  Map from signer account ID to weight.
 *  @param quorum       Minimum weight required for settlement.
 *  @param j            Journal for diagnostic logging.
 *  @return `tesSUCCESS`, or an error code if the claim ID is missing, the
 *      sending account mismatches, or a fatal internal error occurs.
 */
template <class TIter>
TER
applyClaimAttestations(
    ApplyView& view,
    RawView& rawView,
    TIter attBegin,
    TIter attEnd,
    STXChainBridge const& bridgeSpec,
    STXChainBridge::ChainType const srcChain,
    std::unordered_map<AccountID, std::uint32_t> const& signersList,
    std::uint32_t quorum,
    beast::Journal j)
{
    if (attBegin == attEnd)
        return tesSUCCESS;

    PaymentSandbox psb(&view);

    auto const claimIDKeylet = keylet::xChainClaimID(bridgeSpec, attBegin->claimID);

    struct ScopeResult
    {
        OnNewAttestationResult newAttResult;
        STAmount rewardAmount;
        AccountID cidOwner;
    };

    auto const scopeResult = [&]() -> Expected<ScopeResult, TER> {
        // This lambda is ugly - admittedly. The purpose of this lambda is to
        // limit the scope of sles so they don't overlap with
        // `finalizeClaimHelper`. Since `finalizeClaimHelper` can create child
        // views, it's important that the sle's lifetime doesn't overlap.
        auto const sleClaimID = psb.peek(claimIDKeylet);
        if (!sleClaimID)
            return Unexpected(tecXCHAIN_NO_CLAIM_ID);

        std::vector<Attestations::AttestationClaim> atts;
        atts.reserve(std::distance(attBegin, attEnd));
        for (auto att = attBegin; att != attEnd; ++att)
        {
            if (!signersList.contains(att->attestationSignerAccount))
                continue;
            atts.push_back(*att);
        }

        if (atts.empty())
        {
            return Unexpected(tecXCHAIN_PROOF_UNKNOWN_KEY);
        }

        AccountID const otherChainSource = (*sleClaimID)[sfOtherChainSource];
        if (attBegin->sendingAccount != otherChainSource)
        {
            return Unexpected(tecXCHAIN_SENDING_ACCOUNT_MISMATCH);
        }

        {
            STXChainBridge::ChainType const dstChain = STXChainBridge::otherChain(srcChain);

            STXChainBridge::ChainType const attDstChain =
                STXChainBridge::dstChain(attBegin->wasLockingChainSend);

            if (attDstChain != dstChain)
            {
                return Unexpected(tecXCHAIN_WRONG_CHAIN);
            }
        }

        XChainClaimAttestations curAtts{sleClaimID->getFieldArray(sfXChainClaimAttestations)};

        auto const newAttResult = onNewAttestations(
            curAtts,
            view,
            &atts[0],
            &atts[0] + atts.size(),  // NOLINT(bugprone-pointer-arithmetic-on-polymorphic-object)
            quorum,
            signersList,
            j);

        // update the claim id
        sleClaimID->setFieldArray(sfXChainClaimAttestations, curAtts.toSTArray());
        psb.update(sleClaimID);

        return ScopeResult{
            newAttResult, (*sleClaimID)[sfSignatureReward], (*sleClaimID)[sfAccount]};
    }();

    if (!scopeResult.has_value())
        return scopeResult.error();

    auto const& [newAttResult, rewardAmount, cidOwner] = scopeResult.value();
    auto const& [rewardAccounts, attListChanged] = newAttResult;
    if (rewardAccounts && attBegin->dst)
    {
        auto const r = finalizeClaimHelper(
            psb,
            bridgeSpec,
            *attBegin->dst,
            /*dstTag*/ std::nullopt,
            cidOwner,
            attBegin->sendingAmount,
            cidOwner,
            rewardAmount,
            *rewardAccounts,
            srcChain,
            claimIDKeylet,
            OnTransferFail::KeepClaim,
            DepositAuthPolicy::Normal,
            j);

        auto const rTer = r.ter();

        if (!isTesSuccess(rTer) &&
            (!attListChanged || rTer == tecINTERNAL || rTer == tefBAD_LEDGER))
            return rTer;
    }

    psb.apply(rawView);

    return tesSUCCESS;
}

/** Apply account-creation attestations and, when quorum is reached, create the
 *  destination account on the issuing chain.
 *
 *  Account-creation transfers must happen in strict order (enforced by
 *  `sfXChainAccountCreateCount` / `sfXChainAccountClaimCount`).  When
 *  `createCount + 1 == attBegin->createCount`, the claim is processed
 *  immediately and `sfXChainAccountClaimCount` is advanced.  If the transfer
 *  fails, the claim ID is removed anyway (`OnTransferFail::RemoveClaim`) and
 *  the counter is advanced to prevent one stalled create from blocking all
 *  subsequent ones.
 *
 *  The inner lambda scopes SLE lifetimes to prevent overlap with
 *  `finalizeClaimHelper`'s child sandbox.
 *
 *  @param view         Mutable apply view.
 *  @param rawView      Raw view for final sandbox commit.
 *  @param attBegin     Iterator to the first incoming attestation.
 *  @param attEnd       One-past-the-end iterator.
 *  @param doorAccount  Door account ID (owns the bridge and created claim IDs).
 *  @param doorK        Keylet for the door account SLE.
 *  @param bridgeSpec   Bridge specification.
 *  @param bridgeK      Keylet for the bridge SLE.
 *  @param srcChain     Chain where the `XChainCreateAccountCommit` occurred.
 *  @param signersList  Map from signer account ID to weight.
 *  @param quorum       Minimum weight required for settlement.
 *  @param j            Journal for diagnostic logging.
 *  @return `tesSUCCESS`, or an error code if ordering, reserve, or internal
 *      checks fail.
 */
template <class TIter>
TER
applyCreateAccountAttestations(
    ApplyView& view,
    RawView& rawView,
    TIter attBegin,
    TIter attEnd,
    AccountID const& doorAccount,
    Keylet const& doorK,
    STXChainBridge const& bridgeSpec,
    Keylet const& bridgeK,
    STXChainBridge::ChainType const srcChain,
    std::unordered_map<AccountID, std::uint32_t> const& signersList,
    std::uint32_t quorum,
    beast::Journal j)
{
    if (attBegin == attEnd)
        return tesSUCCESS;

    PaymentSandbox psb(&view);

    auto const claimCountResult = [&]() -> Expected<std::uint64_t, TER> {
        auto const sleBridge = psb.peek(bridgeK);
        if (!sleBridge)
            return Unexpected(tecINTERNAL);

        return (*sleBridge)[sfXChainAccountClaimCount];
    }();

    if (!claimCountResult.has_value())
        return claimCountResult.error();

    std::uint64_t const claimCount = claimCountResult.value();

    if (attBegin->createCount <= claimCount)
    {
        return tecXCHAIN_ACCOUNT_CREATE_PAST;
    }
    if (attBegin->createCount >= claimCount + kXBRIDGE_MAX_ACCOUNT_CREATE_CLAIMS)
    {
        // Limit the number of claims on the account
        return tecXCHAIN_ACCOUNT_CREATE_TOO_MANY;
    }

    {
        STXChainBridge::ChainType const dstChain = STXChainBridge::otherChain(srcChain);

        STXChainBridge::ChainType const attDstChain =
            STXChainBridge::dstChain(attBegin->wasLockingChainSend);

        if (attDstChain != dstChain)
        {
            return tecXCHAIN_WRONG_CHAIN;
        }
    }

    auto const claimIDKeylet =
        keylet::xChainCreateAccountClaimID(bridgeSpec, attBegin->createCount);

    struct ScopeResult
    {
        OnNewAttestationResult newAttResult;
        bool createCID{};
        XChainCreateAccountAttestations curAtts;
    };

    auto const scopeResult = [&]() -> Expected<ScopeResult, TER> {
        // This lambda is ugly - admittedly. The purpose of this lambda is to
        // limit the scope of sles so they don't overlap with
        // `finalizeClaimHelper`. Since `finalizeClaimHelper` can create child
        // views, it's important that the sle's lifetime doesn't overlap.

        // sleClaimID may be null. If it's null it isn't created until the end
        // of this function (if needed)
        auto const sleClaimID = psb.peek(claimIDKeylet);
        bool createCID = false;
        if (!sleClaimID)
        {
            createCID = true;

            auto const sleDoor = psb.peek(doorK);
            if (!sleDoor)
                return Unexpected(tecINTERNAL);

            auto const balance = (*sleDoor)[sfBalance];
            auto const reserve = psb.fees().accountReserve((*sleDoor)[sfOwnerCount] + 1);

            if (balance < reserve)
                return Unexpected(tecINSUFFICIENT_RESERVE);
        }

        std::vector<Attestations::AttestationCreateAccount> atts;
        atts.reserve(std::distance(attBegin, attEnd));
        for (auto att = attBegin; att != attEnd; ++att)
        {
            if (!signersList.contains(att->attestationSignerAccount))
                continue;
            atts.push_back(*att);
        }
        if (atts.empty())
        {
            return Unexpected(tecXCHAIN_PROOF_UNKNOWN_KEY);
        }

        XChainCreateAccountAttestations curAtts = [&] {
            if (sleClaimID)
            {
                return XChainCreateAccountAttestations{
                    sleClaimID->getFieldArray(sfXChainCreateAccountAttestations)};
            }
            return XChainCreateAccountAttestations{};
        }();

        auto const newAttResult = onNewAttestations(
            curAtts,
            view,
            &atts[0],
            &atts[0] + atts.size(),  // NOLINT(bugprone-pointer-arithmetic-on-polymorphic-object)
            quorum,
            signersList,
            j);

        if (!createCID)
        {
            // Modify the object before it's potentially deleted, so the meta
            // data will include the new attestations
            if (!sleClaimID)
                return Unexpected(tecINTERNAL);
            sleClaimID->setFieldArray(sfXChainCreateAccountAttestations, curAtts.toSTArray());
            psb.update(sleClaimID);
        }
        return ScopeResult{newAttResult, createCID, curAtts};
    }();

    if (!scopeResult.has_value())
        return scopeResult.error();

    auto const& [attResult, createCID, curAtts] = scopeResult.value();
    auto const& [rewardAccounts, attListChanged] = attResult;

    // Account create transactions must happen in order
    if (rewardAccounts && claimCount + 1 == attBegin->createCount)
    {
        auto const r = finalizeClaimHelper(
            psb,
            bridgeSpec,
            attBegin->toCreate,
            /*dstTag*/ std::nullopt,
            doorAccount,
            attBegin->sendingAmount,
            /*rewardPoolSrc*/ doorAccount,
            attBegin->rewardAmount,
            *rewardAccounts,
            srcChain,
            claimIDKeylet,
            OnTransferFail::RemoveClaim,
            DepositAuthPolicy::Normal,
            j);

        auto const rTer = r.ter();

        if (!isTesSuccess(rTer))
        {
            if (rTer == tecINTERNAL || rTer == tecUNFUNDED_PAYMENT || isTefFailure(rTer))
                return rTer;
        }
        // Move past this claim id even if it fails, so it doesn't block
        // subsequent claim ids
        auto const sleBridge = psb.peek(bridgeK);
        if (!sleBridge)
            return tecINTERNAL;  // LCOV_EXCL_LINE
        (*sleBridge)[sfXChainAccountClaimCount] = attBegin->createCount;
        psb.update(sleBridge);
    }
    else if (createCID)
    {
        auto const createdSleClaimID = std::make_shared<SLE>(claimIDKeylet);
        (*createdSleClaimID)[sfAccount] = doorAccount;
        (*createdSleClaimID)[sfXChainBridge] = bridgeSpec;
        (*createdSleClaimID)[sfXChainAccountCreateCount] = attBegin->createCount;
        createdSleClaimID->setFieldArray(sfXChainCreateAccountAttestations, curAtts.toSTArray());

        auto const page = psb.dirInsert(
            keylet::ownerDir(doorAccount), claimIDKeylet, describeOwnerDir(doorAccount));
        if (!page)
            return tecDIR_FULL;  // LCOV_EXCL_LINE
        (*createdSleClaimID)[sfOwnerNode] = *page;

        auto const sleDoor = psb.peek(doorK);
        if (!sleDoor)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        // Reserve was already checked
        adjustOwnerCount(psb, sleDoor, 1, j);
        psb.insert(createdSleClaimID);
        psb.update(sleDoor);
    }

    psb.apply(rawView);

    return tesSUCCESS;
}

/** Deserialise a transaction into an `AttestationClaim` or
 *  `AttestationCreateAccount` object.
 *
 *  The transaction's `sfAccount` field is temporarily overwritten with the
 *  `sfOtherChainSource` value before construction, satisfying the attestation
 *  type's field layout expectations.
 *
 *  @tparam TAttestation  Must be `AttestationClaim` or
 *      `AttestationCreateAccount`.
 *  @param tx  Transaction to deserialise.
 *  @return The constructed attestation, or `std::nullopt` if construction
 *      throws (e.g. a required field is missing or malformed).
 */
template <class TAttestation>
std::optional<TAttestation>
toClaim(STTx const& tx)
{
    static_assert(
        std::is_same_v<TAttestation, Attestations::AttestationClaim> ||
        std::is_same_v<TAttestation, Attestations::AttestationCreateAccount>);

    try
    {
        STObject o{tx};
        o.setAccountID(sfAccount, o[sfOtherChainSource]);
        return TAttestation(o);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

/** Shared `preflight` implementation for both attestation transaction types.
 *
 *  Validates: public key is a recognised key type; the transaction can be
 *  deserialised into `TAttestation`; the attestation signature is valid for
 *  the bridge; amounts are structurally valid and positive; the sending amount
 *  matches the bridge's source-chain issue.
 *
 *  @tparam TAttestation  `AttestationClaim` or `AttestationCreateAccount`.
 *  @param ctx  Preflight context (no ledger access).
 *  @return `tesSUCCESS` or a `tem*` code.
 */
template <class TAttestation>
NotTEC
attestationPreflight(PreflightContext const& ctx)
{
    if (!publicKeyType(ctx.tx[sfPublicKey]))
        return temMALFORMED;

    auto const att = toClaim<TAttestation>(ctx.tx);
    if (!att)
        return temMALFORMED;

    STXChainBridge const bridgeSpec = ctx.tx[sfXChainBridge];
    if (!att->verify(bridgeSpec))
        return temXCHAIN_BAD_PROOF;
    if (!att->validAmounts())
        return temXCHAIN_BAD_PROOF;

    if (att->sendingAmount.signum() <= 0)
        return temXCHAIN_BAD_PROOF;
    auto const expectedIssue = bridgeSpec.issue(STXChainBridge::srcChain(att->wasLockingChainSend));
    if (att->sendingAmount.asset() != expectedIssue)
        return temXCHAIN_BAD_PROOF;

    return tesSUCCESS;
}

/** Shared `preclaim` implementation for both attestation transaction types.
 *
 *  Verifies the bridge exists and that the attestation's public key is
 *  authorised to sign on behalf of `sfAttestationSignerAccount` in the
 *  bridge door's signer list.
 *
 *  @tparam TAttestation  `AttestationClaim` or `AttestationCreateAccount`.
 *  @param ctx  Preclaim context (read-only ledger access).
 *  @return `tesSUCCESS`, `tecNO_ENTRY`, `tecXCHAIN_NO_SIGNERS_LIST`,
 *      `tecXCHAIN_BAD_PUBLIC_KEY_ACCOUNT_PAIR`, or `tecINTERNAL`.
 */
template <class TAttestation>
TER
attestationPreclaim(PreclaimContext const& ctx)
{
    auto const att = toClaim<TAttestation>(ctx.tx);
    // checked in preflight
    if (!att)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    STXChainBridge const bridgeSpec = ctx.tx[sfXChainBridge];
    auto const sleBridge = readBridge(ctx.view, bridgeSpec);
    if (!sleBridge)
    {
        return tecNO_ENTRY;
    }

    AccountID const attestationSignerAccount{ctx.tx[sfAttestationSignerAccount]};
    PublicKey const pk{ctx.tx[sfPublicKey]};

    // signersList is a map from account id to weights
    auto const [signersList, quorum, slTer] = getSignersListAndQuorum(ctx.view, *sleBridge, ctx.j);

    if (!isTesSuccess(slTer))
        return slTer;

    return checkAttestationPublicKey(ctx.view, signersList, attestationSignerAccount, pk, ctx.j);
}

/** Shared `doApply` implementation for both attestation transaction types.
 *
 *  Determines which chain is the source, fetches the signer list and quorum,
 *  and dispatches to `applyClaimAttestations` (for `AttestationClaim`) or
 *  `applyCreateAccountAttestations` (for `AttestationCreateAccount`) via
 *  `if constexpr`.  SLE lifetimes are scoped via a lambda to prevent overlap
 *  with `finalizeClaimHelper`'s child `PaymentSandbox`.
 *
 *  @tparam TAttestation  `AttestationClaim` or `AttestationCreateAccount`.
 *  @param ctx  Apply context (mutable ledger access).
 *  @return `tesSUCCESS`, or a `tec`/`tef` error code.
 */
template <class TAttestation>
TER
attestationDoApply(ApplyContext& ctx)
{
    auto const att = toClaim<TAttestation>(ctx.tx);
    if (!att)
    {
        // Should already be checked in preflight
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    STXChainBridge const bridgeSpec = ctx.tx[sfXChainBridge];

    struct ScopeResult
    {
        STXChainBridge::ChainType srcChain = STXChainBridge::ChainType::Locking;
        std::unordered_map<AccountID, std::uint32_t> signersList;
        std::uint32_t quorum{};
        AccountID thisDoor;
        Keylet bridgeK;
    };

    auto const scopeResult = [&]() -> Expected<ScopeResult, TER> {
        // This lambda is ugly - admittedly. The purpose of this lambda is to
        // limit the scope of sles so they don't overlap with
        // `finalizeClaimHelper`. Since `finalizeClaimHelper` can create child
        // views, it's important that the sle's lifetime doesn't overlap.
        auto sleBridge = readBridge(ctx.view(), bridgeSpec);
        if (!sleBridge)
        {
            return Unexpected(tecNO_ENTRY);
        }
        Keylet const bridgeK{ltBRIDGE, sleBridge->key()};
        AccountID const thisDoor = (*sleBridge)[sfAccount];

        STXChainBridge::ChainType dstChain = STXChainBridge::ChainType::Locking;
        {
            if (thisDoor == bridgeSpec.lockingChainDoor())
            {
                dstChain = STXChainBridge::ChainType::Locking;
            }
            else if (thisDoor == bridgeSpec.issuingChainDoor())
            {
                dstChain = STXChainBridge::ChainType::Issuing;
            }
            else
            {
                return Unexpected(tecINTERNAL);
            }
        }
        STXChainBridge::ChainType const srcChain = STXChainBridge::otherChain(dstChain);

        // signersList is a map from account id to weights
        auto [signersList, quorum, slTer] =
            getSignersListAndQuorum(ctx.view(), *sleBridge, ctx.journal);

        if (!isTesSuccess(slTer))
            return Unexpected(slTer);

        return ScopeResult{srcChain, std::move(signersList), quorum, thisDoor, bridgeK};
    }();

    if (!scopeResult.has_value())
        return scopeResult.error();

    auto const& [srcChain, signersList, quorum, thisDoor, bridgeK] = scopeResult.value();

    static_assert(
        std::is_same_v<TAttestation, Attestations::AttestationClaim> ||
        std::is_same_v<TAttestation, Attestations::AttestationCreateAccount>);

    if constexpr (std::is_same_v<TAttestation, Attestations::AttestationClaim>)
    {
        return applyClaimAttestations(
            ctx.view(),
            ctx.rawView(),
            &*att,
            &*att + 1,
            bridgeSpec,
            srcChain,
            signersList,
            quorum,
            ctx.journal);
    }
    else if constexpr (std::is_same_v<TAttestation, Attestations::AttestationCreateAccount>)
    {
        return applyCreateAccountAttestations(
            ctx.view(),
            ctx.rawView(),
            &*att,
            &*att + 1,
            thisDoor,
            keylet::account(thisDoor),
            bridgeSpec,
            bridgeK,
            srcChain,
            signersList,
            quorum,
            ctx.journal);
    }
}

}  // namespace
//------------------------------------------------------------------------------

/** Stateless validation for `XChainCreateBridge`.
 *
 *  Enforces: distinct door accounts (replay prevention); submitting account
 *  is one of the two door accounts; both sides are the same asset class (both
 *  XRP or both IOU); `sfSignatureReward` is non-negative XRP; optional
 *  `sfMinAccountCreateAmount` is positive XRP and only present on XRP bridges;
 *  for XRP bridges the issuing door must be the genesis root account; for IOU
 *  bridges the issuing door must be the currency issuer; the locking door must
 *  not be its own asset issuer.
 *
 *  @param ctx  Preflight context.
 *  @return `tesSUCCESS` or a `tem*` code.
 */
NotTEC
XChainCreateBridge::preflight(PreflightContext const& ctx)
{
    auto const account = ctx.tx[sfAccount];
    auto const reward = ctx.tx[sfSignatureReward];
    auto const minAccountCreate = ctx.tx[~sfMinAccountCreateAmount];
    auto const bridgeSpec = ctx.tx[sfXChainBridge];
    // Doors must be distinct to help prevent transaction replay attacks
    if (bridgeSpec.lockingChainDoor() == bridgeSpec.issuingChainDoor())
    {
        return temXCHAIN_EQUAL_DOOR_ACCOUNTS;
    }

    if (bridgeSpec.lockingChainDoor() != account && bridgeSpec.issuingChainDoor() != account)
    {
        return temXCHAIN_BRIDGE_NONDOOR_OWNER;
    }

    if (isXRP(bridgeSpec.lockingChainIssue()) != isXRP(bridgeSpec.issuingChainIssue()))
    {
        // Because ious and xrp have different numeric ranges, both the src and
        // dst issues must be both XRP or both IOU.
        return temXCHAIN_BRIDGE_BAD_ISSUES;
    }

    if (!isXRP(reward) || reward.signum() < 0)
    {
        return temXCHAIN_BRIDGE_BAD_REWARD_AMOUNT;
    }

    if (minAccountCreate &&
        ((!isXRP(*minAccountCreate) || minAccountCreate->signum() <= 0) ||
         !isXRP(bridgeSpec.lockingChainIssue()) || !isXRP(bridgeSpec.issuingChainIssue())))
    {
        return temXCHAIN_BRIDGE_BAD_MIN_ACCOUNT_CREATE_AMOUNT;
    }

    if (isXRP(bridgeSpec.issuingChainIssue()))
    {
        static auto const kROOT_ACCOUNT = calcAccountID(
            generateKeyPair(KeyType::Secp256k1, generateSeed("masterpassphrase")).first);
        if (bridgeSpec.issuingChainDoor() != kROOT_ACCOUNT)
        {
            return temXCHAIN_BRIDGE_BAD_ISSUES;
        }
    }
    else
    {
        if (bridgeSpec.issuingChainDoor() != bridgeSpec.issuingChainIssue().account)
        {
            return temXCHAIN_BRIDGE_BAD_ISSUES;
        }
    }

    if (bridgeSpec.lockingChainDoor() == bridgeSpec.lockingChainIssue().account)
    {
        // If the locking chain door is locking their own asset, in some sense
        // nothing is being locked. Disallow this.
        return temXCHAIN_BRIDGE_BAD_ISSUES;
    }

    return tesSUCCESS;
}

/** Read-only preclaim checks for `XChainCreateBridge`.
 *
 *  Verifies: no duplicate bridge exists on either chain; IOU issuer exists and
 *  has not enabled clawback (which would allow stealing backed assets); the
 *  submitting account has sufficient reserve for one new owned object.
 *
 *  @param ctx  Preclaim context (read-only ledger).
 *  @return `tesSUCCESS`, `tecDUPLICATE`, `tecNO_ISSUER`, `tecNO_PERMISSION`,
 *      `terNO_ACCOUNT`, or `tecINSUFFICIENT_RESERVE`.
 */
TER
XChainCreateBridge::preclaim(PreclaimContext const& ctx)
{
    auto const account = ctx.tx[sfAccount];
    auto const bridgeSpec = ctx.tx[sfXChainBridge];
    STXChainBridge::ChainType const chainType =
        STXChainBridge::srcChain(account == bridgeSpec.lockingChainDoor());

    {
        auto hasBridge = [&](STXChainBridge::ChainType ct) -> bool {
            return ctx.view.exists(keylet::bridge(bridgeSpec, ct));
        };

        if (hasBridge(STXChainBridge::ChainType::Issuing) ||
            hasBridge(STXChainBridge::ChainType::Locking))
        {
            return tecDUPLICATE;
        }
    }

    if (!isXRP(bridgeSpec.issue(chainType)))
    {
        auto const sleIssuer = ctx.view.read(keylet::account(bridgeSpec.issue(chainType).account));

        if (!sleIssuer)
            return tecNO_ISSUER;

        // Allowing clawing back funds would break the bridge's invariant that
        // wrapped funds are always backed by locked funds
        if ((sleIssuer->getFlags() & lsfAllowTrustLineClawback) != 0u)
            return tecNO_PERMISSION;
    }

    {
        auto const sleAcc = ctx.view.read(keylet::account(account));
        if (!sleAcc)
            return terNO_ACCOUNT;

        auto const balance = (*sleAcc)[sfBalance];
        auto const reserve = ctx.view.fees().accountReserve((*sleAcc)[sfOwnerCount] + 1);

        if (balance < reserve)
            return tecINSUFFICIENT_RESERVE;
    }

    return tesSUCCESS;
}

/** Create the bridge SLE and register it in the door account's owner directory.
 *
 *  Initialises `sfXChainClaimID`, `sfXChainAccountCreateCount`, and
 *  `sfXChainAccountClaimCount` to zero.  Increments the door account's owner
 *  count by one.
 *
 *  @return `tesSUCCESS` or `tecINTERNAL` / `tecDIR_FULL` on ledger corruption.
 */
TER
XChainCreateBridge::doApply()
{
    auto const account = ctx_.tx[sfAccount];
    auto const bridgeSpec = ctx_.tx[sfXChainBridge];
    auto const reward = ctx_.tx[sfSignatureReward];
    auto const minAccountCreate = ctx_.tx[~sfMinAccountCreateAmount];

    auto const sleAcct = ctx_.view().peek(keylet::account(account));
    if (!sleAcct)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    STXChainBridge::ChainType const chainType =
        STXChainBridge::srcChain(account == bridgeSpec.lockingChainDoor());

    Keylet const bridgeKeylet = keylet::bridge(bridgeSpec, chainType);
    auto const sleBridge = std::make_shared<SLE>(bridgeKeylet);

    (*sleBridge)[sfAccount] = account;
    (*sleBridge)[sfSignatureReward] = reward;
    if (minAccountCreate)
        (*sleBridge)[sfMinAccountCreateAmount] = *minAccountCreate;
    (*sleBridge)[sfXChainBridge] = bridgeSpec;
    (*sleBridge)[sfXChainClaimID] = 0;
    (*sleBridge)[sfXChainAccountCreateCount] = 0;
    (*sleBridge)[sfXChainAccountClaimCount] = 0;

    {
        auto const page = ctx_.view().dirInsert(
            keylet::ownerDir(account), bridgeKeylet, describeOwnerDir(account));
        if (!page)
            return tecDIR_FULL;  // LCOV_EXCL_LINE
        (*sleBridge)[sfOwnerNode] = *page;
    }

    adjustOwnerCount(ctx_.view(), sleAcct, 1, ctx_.journal);

    ctx_.view().insert(sleBridge);
    ctx_.view().update(sleAcct);

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

/** Returns the valid flag mask for `BridgeModify` transactions.
 *
 *  @return `tfXChainModifyBridgeMask`.
 */
std::uint32_t
BridgeModify::getFlagsMask(PreflightContext const& ctx)
{
    return tfXChainModifyBridgeMask;
}

/** Stateless validation for `BridgeModify`.
 *
 *  Requires at least one of `sfSignatureReward`, `sfMinAccountCreateAmount`,
 *  or `tfClearAccountCreateAmount` to be present (must change something).
 *  Rejects conflicting `sfMinAccountCreateAmount` + `tfClearAccountCreateAmount`
 *  in the same transaction.  Validates reward and min-create amounts.
 *
 *  @param ctx  Preflight context.
 *  @return `tesSUCCESS` or a `tem*` code.
 */
NotTEC
BridgeModify::preflight(PreflightContext const& ctx)
{
    auto const account = ctx.tx[sfAccount];
    auto const reward = ctx.tx[~sfSignatureReward];
    auto const minAccountCreate = ctx.tx[~sfMinAccountCreateAmount];
    auto const bridgeSpec = ctx.tx[sfXChainBridge];
    bool const clearAccountCreate = (ctx.tx.getFlags() & tfClearAccountCreateAmount) != 0u;

    if (!reward && !minAccountCreate && !clearAccountCreate)
    {
        // Must change something
        return temMALFORMED;
    }

    if (minAccountCreate && clearAccountCreate)
    {
        // Can't both clear and set account create in the same txn
        return temMALFORMED;
    }

    if (bridgeSpec.lockingChainDoor() != account && bridgeSpec.issuingChainDoor() != account)
    {
        return temXCHAIN_BRIDGE_NONDOOR_OWNER;
    }

    if (reward && (!isXRP(*reward) || reward->signum() < 0))
    {
        return temXCHAIN_BRIDGE_BAD_REWARD_AMOUNT;
    }

    if (minAccountCreate &&
        ((!isXRP(*minAccountCreate) || minAccountCreate->signum() <= 0) ||
         !isXRP(bridgeSpec.lockingChainIssue()) || !isXRP(bridgeSpec.issuingChainIssue())))
    {
        return temXCHAIN_BRIDGE_BAD_MIN_ACCOUNT_CREATE_AMOUNT;
    }

    return tesSUCCESS;
}

/** Verify the bridge exists for `BridgeModify`.
 *
 *  @param ctx  Preclaim context.
 *  @return `tesSUCCESS` or `tecNO_ENTRY`.
 */
TER
BridgeModify::preclaim(PreclaimContext const& ctx)
{
    auto const account = ctx.tx[sfAccount];
    auto const bridgeSpec = ctx.tx[sfXChainBridge];

    STXChainBridge::ChainType const chainType =
        STXChainBridge::srcChain(account == bridgeSpec.lockingChainDoor());

    if (!ctx.view.read(keylet::bridge(bridgeSpec, chainType)))
    {
        return tecNO_ENTRY;
    }

    return tesSUCCESS;
}

/** Update `sfSignatureReward` and/or `sfMinAccountCreateAmount` on the bridge.
 *
 *  `tfClearAccountCreateAmount` removes `sfMinAccountCreateAmount` entirely,
 *  disabling the account-creation pathway.
 *
 *  @return `tesSUCCESS` or `tecINTERNAL` on ledger corruption.
 */
TER
BridgeModify::doApply()
{
    auto const account = ctx_.tx[sfAccount];
    auto const bridgeSpec = ctx_.tx[sfXChainBridge];
    auto const reward = ctx_.tx[~sfSignatureReward];
    auto const minAccountCreate = ctx_.tx[~sfMinAccountCreateAmount];
    bool const clearAccountCreate = (ctx_.tx.getFlags() & tfClearAccountCreateAmount) != 0u;

    auto const sleAcct = ctx_.view().peek(keylet::account(account));
    if (!sleAcct)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    STXChainBridge::ChainType const chainType =
        STXChainBridge::srcChain(account == bridgeSpec.lockingChainDoor());

    auto const sleBridge = ctx_.view().peek(keylet::bridge(bridgeSpec, chainType));
    if (!sleBridge)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (reward)
        (*sleBridge)[sfSignatureReward] = *reward;
    if (minAccountCreate)
    {
        (*sleBridge)[sfMinAccountCreateAmount] = *minAccountCreate;
    }
    if (clearAccountCreate && sleBridge->isFieldPresent(sfMinAccountCreateAmount))
    {
        sleBridge->makeFieldAbsent(sfMinAccountCreateAmount);
    }
    ctx_.view().update(sleBridge);

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

/** Stateless validation for `XChainClaim`.
 *
 *  Verifies `sfAmount` is positive and matches one of the bridge's two issues.
 *
 *  @param ctx  Preflight context.
 *  @return `tesSUCCESS` or `temBAD_AMOUNT`.
 */
NotTEC
XChainClaim::preflight(PreflightContext const& ctx)
{
    STXChainBridge const bridgeSpec = ctx.tx[sfXChainBridge];
    auto const amount = ctx.tx[sfAmount];

    if (amount.signum() <= 0 ||
        (amount.asset() != bridgeSpec.lockingChainIssue() &&
         amount.asset() != bridgeSpec.issuingChainIssue()))
    {
        return temBAD_AMOUNT;
    }

    return tesSUCCESS;
}

/** Read-only preclaim checks for `XChainClaim`.
 *
 *  Verifies: bridge exists; destination account exists; the claim ID is owned
 *  by the submitting account; the amount's asset matches the destination chain's
 *  issue.  Quorum is not checked here — it is deferred to `doApply`.
 *
 *  @param ctx  Preclaim context.
 *  @return `tesSUCCESS`, `tecNO_ENTRY`, `tecNO_DST`, `tecXCHAIN_NO_CLAIM_ID`,
 *      `tecXCHAIN_BAD_CLAIM_ID`, `tecXCHAIN_BAD_TRANSFER_ISSUE`, or
 *      `tecINTERNAL`.
 */
TER
XChainClaim::preclaim(PreclaimContext const& ctx)
{
    AccountID const account = ctx.tx[sfAccount];
    STXChainBridge const bridgeSpec = ctx.tx[sfXChainBridge];
    STAmount const& thisChainAmount = ctx.tx[sfAmount];
    auto const claimID = ctx.tx[sfXChainClaimID];

    auto const sleBridge = readBridge(ctx.view, bridgeSpec);
    if (!sleBridge)
    {
        return tecNO_ENTRY;
    }

    if (!ctx.view.read(keylet::account(ctx.tx[sfDestination])))
    {
        return tecNO_DST;
    }

    auto const thisDoor = (*sleBridge)[sfAccount];
    bool isLockingChain = false;
    {
        if (thisDoor == bridgeSpec.lockingChainDoor())
        {
            isLockingChain = true;
        }
        else if (thisDoor == bridgeSpec.issuingChainDoor())
        {
            isLockingChain = false;
        }
        else
        {
            return tecINTERNAL;  // LCOV_EXCL_LINE
        }
    }

    {
        if (isLockingChain)
        {
            if (bridgeSpec.lockingChainIssue() != thisChainAmount.asset())
                return tecXCHAIN_BAD_TRANSFER_ISSUE;
        }
        else
        {
            if (bridgeSpec.issuingChainIssue() != thisChainAmount.asset())
                return tecXCHAIN_BAD_TRANSFER_ISSUE;
        }
    }

    if (isXRP(bridgeSpec.lockingChainIssue()) != isXRP(bridgeSpec.issuingChainIssue()))
    {
        // Should have been caught when creating the bridge
        // Detect here so `otherChainAmount` doesn't switch from IOU -> XRP
        // and the numeric issues that need to be addressed with that.
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    auto const otherChainAmount = [&]() -> STAmount {
        STAmount r(thisChainAmount);
        if (isLockingChain)
        {
            r.setIssue(bridgeSpec.issuingChainIssue());
        }
        else
        {
            r.setIssue(bridgeSpec.lockingChainIssue());
        }
        return r;
    }();

    auto const sleClaimID = ctx.view.read(keylet::xChainClaimID(bridgeSpec, claimID));
    {
        if (!sleClaimID)
        {
            return tecXCHAIN_NO_CLAIM_ID;
        }

        if ((*sleClaimID)[sfAccount] != account)
        {
            // Sequence number isn't owned by the sender of this transaction
            return tecXCHAIN_BAD_CLAIM_ID;
        }
    }

    // quorum is checked in `doApply`
    return tesSUCCESS;
}

/** Settle a cross-chain transfer using an accumulated quorum of attestations.
 *
 *  Verifies quorum via `onClaim` (destination-agnostic), then calls
 *  `finalizeClaimHelper` with `OnTransferFail::KeepClaim` and
 *  `DepositAuthPolicy::DstCanBypass` — the claim owner may send funds to
 *  themselves even if their destination has `lsfDepositAuth` set.
 *
 *  @return `tesSUCCESS`, `tecXCHAIN_CLAIM_NO_QUORUM`, or other error codes
 *      from `finalizeClaimHelper`.
 */
TER
XChainClaim::doApply()
{
    PaymentSandbox psb(&ctx_.view());

    AccountID const account = ctx_.tx[sfAccount];
    auto const dst = ctx_.tx[sfDestination];
    STXChainBridge const bridgeSpec = ctx_.tx[sfXChainBridge];
    STAmount const& thisChainAmount = ctx_.tx[sfAmount];
    auto const claimID = ctx_.tx[sfXChainClaimID];
    auto const claimIDKeylet = keylet::xChainClaimID(bridgeSpec, claimID);

    struct ScopeResult
    {
        std::vector<AccountID> rewardAccounts;
        AccountID rewardPoolSrc;
        STAmount sendingAmount;
        STXChainBridge::ChainType srcChain;
        STAmount signatureReward;
    };

    auto const scopeResult = [&]() -> Expected<ScopeResult, TER> {
        // This lambda is ugly - admittedly. The purpose of this lambda is to
        // limit the scope of sles so they don't overlap with
        // `finalizeClaimHelper`. Since `finalizeClaimHelper` can create child
        // views, it's important that the sle's lifetime doesn't overlap.

        auto const sleAcct = psb.peek(keylet::account(account));
        auto const sleBridge = peekBridge(psb, bridgeSpec);
        auto const sleClaimID = psb.peek(claimIDKeylet);

        if (!(sleBridge && sleClaimID && sleAcct))
            return Unexpected(tecINTERNAL);

        AccountID const thisDoor = (*sleBridge)[sfAccount];

        STXChainBridge::ChainType dstChain = STXChainBridge::ChainType::Locking;
        {
            if (thisDoor == bridgeSpec.lockingChainDoor())
            {
                dstChain = STXChainBridge::ChainType::Locking;
            }
            else if (thisDoor == bridgeSpec.issuingChainDoor())
            {
                dstChain = STXChainBridge::ChainType::Issuing;
            }
            else
            {
                return Unexpected(tecINTERNAL);
            }
        }
        STXChainBridge::ChainType const srcChain = STXChainBridge::otherChain(dstChain);

        auto const sendingAmount = [&]() -> STAmount {
            STAmount r(thisChainAmount);
            r.setIssue(bridgeSpec.issue(srcChain));
            return r;
        }();

        auto const [signersList, quorum, slTer] =
            getSignersListAndQuorum(ctx_.view(), *sleBridge, ctx_.journal);

        if (!isTesSuccess(slTer))
            return Unexpected(slTer);

        XChainClaimAttestations curAtts{sleClaimID->getFieldArray(sfXChainClaimAttestations)};

        auto const claimR = onClaim(
            curAtts,
            psb,
            sendingAmount,
            /*wasLockingChainSend*/ srcChain == STXChainBridge::ChainType::Locking,
            quorum,
            signersList,
            ctx_.journal);
        if (!claimR.has_value())
            return Unexpected(claimR.error());

        return ScopeResult{
            .rewardAccounts = claimR.value(),
            .rewardPoolSrc = (*sleClaimID)[sfAccount],
            .sendingAmount = sendingAmount,
            .srcChain = srcChain,
            .signatureReward = (*sleClaimID)[sfSignatureReward],
        };
    }();

    if (!scopeResult.has_value())
        return scopeResult.error();

    auto const& [rewardAccounts, rewardPoolSrc, sendingAmount, srcChain, signatureReward] =
        scopeResult.value();
    std::optional<std::uint32_t> const dstTag = ctx_.tx[~sfDestinationTag];

    auto const r = finalizeClaimHelper(
        psb,
        bridgeSpec,
        dst,
        dstTag,
        /*claimOwner*/ account,
        sendingAmount,
        rewardPoolSrc,
        signatureReward,
        rewardAccounts,
        srcChain,
        claimIDKeylet,
        OnTransferFail::KeepClaim,
        DepositAuthPolicy::DstCanBypass,
        ctx_.journal);
    if (!r.isTesSuccess())
        return r.ter();

    psb.apply(ctx_.rawView());

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

/** Compute transaction-queue consequences for `XChainCommit`.
 *
 *  Reports the XRP `sfAmount` as the maximum spend so the transaction queue
 *  can reserve that balance.  Non-XRP amounts report zero (IOU transfers do
 *  not consume XRP from the sender's balance).
 *
 *  @param ctx  Preflight context.
 *  @return `TxConsequences` with `maxSpend` set to the XRP amount, or zero.
 */
TxConsequences
XChainCommit::makeTxConsequences(PreflightContext const& ctx)
{
    auto const maxSpend = [&] {
        auto const amount = ctx.tx[sfAmount];
        if (amount.native() && amount.signum() > 0)
            return amount.xrp();
        return XRPAmount{beast::kZERO};
    }();

    return TxConsequences{ctx.tx, maxSpend};
}

/** Stateless validation for `XChainCommit`.
 *
 *  Checks `sfAmount` is positive, legally representable, and its asset matches
 *  one of the bridge's two issues.
 *
 *  @param ctx  Preflight context.
 *  @return `tesSUCCESS`, `temBAD_AMOUNT`, or `temBAD_ISSUER`.
 */
NotTEC
XChainCommit::preflight(PreflightContext const& ctx)
{
    auto const amount = ctx.tx[sfAmount];
    auto const bridgeSpec = ctx.tx[sfXChainBridge];

    if (amount.signum() <= 0 || !isLegalNet(amount))
        return temBAD_AMOUNT;

    if (amount.asset() != bridgeSpec.lockingChainIssue() &&
        amount.asset() != bridgeSpec.issuingChainIssue())
        return temBAD_ISSUER;

    return tesSUCCESS;
}

/** Read-only preclaim for `XChainCommit`.
 *
 *  Verifies: bridge exists; door account is not the submitter (self-commit
 *  disallowed); amount's asset matches the submitting chain's issue.
 *
 *  @param ctx  Preclaim context.
 *  @return `tesSUCCESS`, `tecNO_ENTRY`, `tecXCHAIN_SELF_COMMIT`,
 *      `tecXCHAIN_BAD_TRANSFER_ISSUE`, or `tecINTERNAL`.
 */
TER
XChainCommit::preclaim(PreclaimContext const& ctx)
{
    auto const bridgeSpec = ctx.tx[sfXChainBridge];
    auto const amount = ctx.tx[sfAmount];

    auto const sleBridge = readBridge(ctx.view, bridgeSpec);
    if (!sleBridge)
    {
        return tecNO_ENTRY;
    }

    AccountID const thisDoor = (*sleBridge)[sfAccount];
    AccountID const account = ctx.tx[sfAccount];

    if (thisDoor == account)
    {
        // Door account can't lock funds onto itself
        return tecXCHAIN_SELF_COMMIT;
    }

    bool isLockingChain = false;
    {
        if (thisDoor == bridgeSpec.lockingChainDoor())
        {
            isLockingChain = true;
        }
        else if (thisDoor == bridgeSpec.issuingChainDoor())
        {
            isLockingChain = false;
        }
        else
        {
            return tecINTERNAL;  // LCOV_EXCL_LINE
        }
    }

    if (isLockingChain)
    {
        if (bridgeSpec.lockingChainIssue() != ctx.tx[sfAmount].asset())
            return tecXCHAIN_BAD_TRANSFER_ISSUE;
    }
    else
    {
        if (bridgeSpec.issuingChainIssue() != ctx.tx[sfAmount].asset())
            return tecXCHAIN_BAD_TRANSFER_ISSUE;
    }

    return tesSUCCESS;
}

/** Transfer `sfAmount` from the submitter into the bridge door account.
 *
 *  Uses `transferHelper` with a `TransferHelperSubmittingAccountInfo` so the
 *  submitter may dip into their owner reserve to cover the transaction fee —
 *  deliberately supported to allow near-empty accounts to commit assets.
 *
 *  @return `tesSUCCESS` or an error code from `transferHelper`.
 */
TER
XChainCommit::doApply()
{
    PaymentSandbox psb(&ctx_.view());

    auto const account = ctx_.tx[sfAccount];
    auto const amount = ctx_.tx[sfAmount];
    auto const bridgeSpec = ctx_.tx[sfXChainBridge];

    auto const sleAccount = psb.read(keylet::account(account));
    if (!sleAccount)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const sleBridge = readBridge(psb, bridgeSpec);
    if (!sleBridge)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const dst = (*sleBridge)[sfAccount];

    // Support dipping into reserves to pay the fee
    TransferHelperSubmittingAccountInfo submittingAccountInfo{
        .account = account_,
        .preFeeBalance = preFeeBalance_,
        .postFeeBalance = (*sleAccount)[sfBalance]};

    auto const thTer = transferHelper(
        psb,
        account,
        dst,
        /*dstTag*/ std::nullopt,
        /*claimOwner*/ std::nullopt,
        amount,
        CanCreateDstPolicy::No,
        DepositAuthPolicy::Normal,
        submittingAccountInfo,
        ctx_.journal);

    if (!isTesSuccess(thTer))
        return thTer;

    psb.apply(ctx_.rawView());

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

/** Stateless validation for `XChainCreateClaimID`.
 *
 *  Verifies `sfSignatureReward` is non-negative, legally representable XRP.
 *
 *  @param ctx  Preflight context.
 *  @return `tesSUCCESS` or `temXCHAIN_BRIDGE_BAD_REWARD_AMOUNT`.
 */
NotTEC
XChainCreateClaimID::preflight(PreflightContext const& ctx)
{
    auto const reward = ctx.tx[sfSignatureReward];

    if (!isXRP(reward) || reward.signum() < 0 || !isLegalNet(reward))
        return temXCHAIN_BRIDGE_BAD_REWARD_AMOUNT;

    return tesSUCCESS;
}

/** Read-only preclaim for `XChainCreateClaimID`.
 *
 *  Verifies: bridge exists; `sfSignatureReward` exactly matches the bridge's
 *  current reward (prevents reward mismatch races); submitter has sufficient
 *  reserve for one new owned object.
 *
 *  @param ctx  Preclaim context.
 *  @return `tesSUCCESS`, `tecNO_ENTRY`, `tecXCHAIN_REWARD_MISMATCH`,
 *      `terNO_ACCOUNT`, or `tecINSUFFICIENT_RESERVE`.
 */
TER
XChainCreateClaimID::preclaim(PreclaimContext const& ctx)
{
    auto const account = ctx.tx[sfAccount];
    auto const bridgeSpec = ctx.tx[sfXChainBridge];
    auto const sleBridge = readBridge(ctx.view, bridgeSpec);

    if (!sleBridge)
    {
        return tecNO_ENTRY;
    }

    auto const reward = ctx.tx[sfSignatureReward];

    if (reward != (*sleBridge)[sfSignatureReward])
    {
        return tecXCHAIN_REWARD_MISMATCH;
    }

    {
        auto const sleAcc = ctx.view.read(keylet::account(account));
        if (!sleAcc)
            return terNO_ACCOUNT;

        auto const balance = (*sleAcc)[sfBalance];
        auto const reserve = ctx.view.fees().accountReserve((*sleAcc)[sfOwnerCount] + 1);

        if (balance < reserve)
            return tecINSUFFICIENT_RESERVE;
    }

    return tesSUCCESS;
}

/** Allocate a new claim ID SLE for a pending cross-chain transfer.
 *
 *  Increments the bridge's `sfXChainClaimID` counter monotonically, creates
 *  the `XChainOwnedClaimID` SLE with an empty attestation array, and inserts
 *  it into the submitter's owner directory.  The caller must have acquired the
 *  claim ID before committing on the source chain — ordering is enforced by
 *  the sequence number invariant.
 *
 *  @return `tesSUCCESS` or `tecINTERNAL` / `tecDIR_FULL` on ledger corruption.
 */
TER
XChainCreateClaimID::doApply()
{
    auto const account = ctx_.tx[sfAccount];
    auto const bridgeSpec = ctx_.tx[sfXChainBridge];
    auto const reward = ctx_.tx[sfSignatureReward];
    auto const otherChainSrc = ctx_.tx[sfOtherChainSource];

    auto const sleAcct = ctx_.view().peek(keylet::account(account));
    if (!sleAcct)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const sleBridge = peekBridge(ctx_.view(), bridgeSpec);
    if (!sleBridge)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    std::uint32_t const claimID = (*sleBridge)[sfXChainClaimID] + 1;
    if (claimID == 0)
    {
        // overflow
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    (*sleBridge)[sfXChainClaimID] = claimID;

    Keylet const claimIDKeylet = keylet::xChainClaimID(bridgeSpec, claimID);
    if (ctx_.view().exists(claimIDKeylet))
    {
        // already checked out!?!
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    auto const sleClaimID = std::make_shared<SLE>(claimIDKeylet);

    (*sleClaimID)[sfAccount] = account;
    (*sleClaimID)[sfXChainBridge] = bridgeSpec;
    (*sleClaimID)[sfXChainClaimID] = claimID;
    (*sleClaimID)[sfOtherChainSource] = otherChainSrc;
    (*sleClaimID)[sfSignatureReward] = reward;
    sleClaimID->setFieldArray(sfXChainClaimAttestations, STArray{sfXChainClaimAttestations});

    {
        auto const page = ctx_.view().dirInsert(
            keylet::ownerDir(account), claimIDKeylet, describeOwnerDir(account));
        if (!page)
            return tecDIR_FULL;  // LCOV_EXCL_LINE
        (*sleClaimID)[sfOwnerNode] = *page;
    }

    adjustOwnerCount(ctx_.view(), sleAcct, 1, ctx_.journal);

    ctx_.view().insert(sleClaimID);
    ctx_.view().update(sleBridge);
    ctx_.view().update(sleAcct);

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

/** Delegates to `attestationPreflight<AttestationClaim>`. */
NotTEC
XChainAddClaimAttestation::preflight(PreflightContext const& ctx)
{
    return attestationPreflight<Attestations::AttestationClaim>(ctx);
}

/** Delegates to `attestationPreclaim<AttestationClaim>`. */
TER
XChainAddClaimAttestation::preclaim(PreclaimContext const& ctx)
{
    return attestationPreclaim<Attestations::AttestationClaim>(ctx);
}

/** Delegates to `attestationDoApply<AttestationClaim>`.
 *
 *  Adds the witness attestation to the claim ID's attestation array and
 *  auto-settles if the new attestation pushes the collection over quorum and
 *  a destination is present.
 */
TER
XChainAddClaimAttestation::doApply()
{
    return attestationDoApply<Attestations::AttestationClaim>(ctx_);
}

//------------------------------------------------------------------------------

/** Delegates to `attestationPreflight<AttestationCreateAccount>`. */
NotTEC
XChainAddAccountCreateAttestation::preflight(PreflightContext const& ctx)
{
    return attestationPreflight<Attestations::AttestationCreateAccount>(ctx);
}

/** Delegates to `attestationPreclaim<AttestationCreateAccount>`. */
TER
XChainAddAccountCreateAttestation::preclaim(PreclaimContext const& ctx)
{
    return attestationPreclaim<Attestations::AttestationCreateAccount>(ctx);
}

/** Delegates to `attestationDoApply<AttestationCreateAccount>`.
 *
 *  Adds the witness attestation and, when quorum is reached and the strict
 *  ordering invariant is satisfied (`claimCount + 1 == createCount`), creates
 *  the destination account on the issuing chain.
 */
TER
XChainAddAccountCreateAttestation::doApply()
{
    return attestationDoApply<Attestations::AttestationCreateAccount>(ctx_);
}

//------------------------------------------------------------------------------

/** Stateless validation for `XChainCreateAccountCommit`.
 *
 *  Both `sfAmount` and `sfSignatureReward` must be positive, native XRP, and
 *  share the same asset type.  This transaction is only valid on XRP bridges
 *  because account creation requires XRP.
 *
 *  @param ctx  Preflight context.
 *  @return `tesSUCCESS` or `temBAD_AMOUNT`.
 */
NotTEC
XChainCreateAccountCommit::preflight(PreflightContext const& ctx)
{
    auto const amount = ctx.tx[sfAmount];

    if (amount.signum() <= 0 || !amount.native())
        return temBAD_AMOUNT;

    auto const reward = ctx.tx[sfSignatureReward];
    if (reward.signum() < 0 || !reward.native())
        return temBAD_AMOUNT;

    if (reward.asset() != amount.asset())
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

/** Read-only preclaim for `XChainCreateAccountCommit`.
 *
 *  Verifies: bridge exists; reward matches the bridge's configured reward;
 *  `sfMinAccountCreateAmount` is present on the bridge (account-creation
 *  pathway is enabled) and `sfAmount` meets the minimum; door is not the
 *  submitter; destination chain's asset is XRP (required for account creation).
 *
 *  @param ctx  Preclaim context.
 *  @return `tesSUCCESS`, `tecNO_ENTRY`, `tecXCHAIN_REWARD_MISMATCH`,
 *      `tecXCHAIN_CREATE_ACCOUNT_DISABLED`, `tecXCHAIN_INSUFF_CREATE_AMOUNT`,
 *      `tecXCHAIN_BAD_TRANSFER_ISSUE`, `tecXCHAIN_SELF_COMMIT`,
 *      `tecXCHAIN_CREATE_ACCOUNT_NONXRP_ISSUE`, or `tecINTERNAL`.
 */
TER
XChainCreateAccountCommit::preclaim(PreclaimContext const& ctx)
{
    STXChainBridge const bridgeSpec = ctx.tx[sfXChainBridge];
    STAmount const amount = ctx.tx[sfAmount];
    STAmount const reward = ctx.tx[sfSignatureReward];

    auto const sleBridge = readBridge(ctx.view, bridgeSpec);
    if (!sleBridge)
    {
        return tecNO_ENTRY;
    }

    if (reward != (*sleBridge)[sfSignatureReward])
    {
        return tecXCHAIN_REWARD_MISMATCH;
    }

    std::optional<STAmount> const minCreateAmount = (*sleBridge)[~sfMinAccountCreateAmount];

    if (!minCreateAmount)
        return tecXCHAIN_CREATE_ACCOUNT_DISABLED;

    if (amount < *minCreateAmount)
        return tecXCHAIN_INSUFF_CREATE_AMOUNT;

    if (minCreateAmount->asset() != amount.asset())
        return tecXCHAIN_BAD_TRANSFER_ISSUE;

    AccountID const thisDoor = (*sleBridge)[sfAccount];
    AccountID const account = ctx.tx[sfAccount];
    if (thisDoor == account)
    {
        // Door account can't lock funds onto itself
        return tecXCHAIN_SELF_COMMIT;
    }

    STXChainBridge::ChainType srcChain = STXChainBridge::ChainType::Locking;
    {
        if (thisDoor == bridgeSpec.lockingChainDoor())
        {
            srcChain = STXChainBridge::ChainType::Locking;
        }
        else if (thisDoor == bridgeSpec.issuingChainDoor())
        {
            srcChain = STXChainBridge::ChainType::Issuing;
        }
        else
        {
            return tecINTERNAL;  // LCOV_EXCL_LINE
        }
    }
    STXChainBridge::ChainType const dstChain = STXChainBridge::otherChain(srcChain);

    if (bridgeSpec.issue(srcChain) != ctx.tx[sfAmount].asset())
        return tecXCHAIN_BAD_TRANSFER_ISSUE;

    if (!isXRP(bridgeSpec.issue(dstChain)))
        return tecXCHAIN_CREATE_ACCOUNT_NONXRP_ISSUE;

    return tesSUCCESS;
}

/** Transfer `sfAmount + sfSignatureReward` from the submitter to the door
 *  account and increment `sfXChainAccountCreateCount` on the bridge.
 *
 *  Like `XChainCommit`, supports fee-dipping via
 *  `TransferHelperSubmittingAccountInfo`.  The reward is embedded in the
 *  transfer rather than held separately; witness servers claim it through
 *  `finalizeClaimHelper` when the account-create attestations reach quorum.
 *
 *  @return `tesSUCCESS` or an error code from `transferHelper`.
 */
TER
XChainCreateAccountCommit::doApply()
{
    PaymentSandbox psb(&ctx_.view());

    AccountID const account = ctx_.tx[sfAccount];
    STAmount const amount = ctx_.tx[sfAmount];
    STAmount const reward = ctx_.tx[sfSignatureReward];
    STXChainBridge const bridge = ctx_.tx[sfXChainBridge];

    auto const sle = psb.peek(keylet::account(account));
    if (!sle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const sleBridge = peekBridge(psb, bridge);
    if (!sleBridge)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const dst = (*sleBridge)[sfAccount];

    // Support dipping into reserves to pay the fee
    TransferHelperSubmittingAccountInfo submittingAccountInfo{
        .account = account_, .preFeeBalance = preFeeBalance_, .postFeeBalance = (*sle)[sfBalance]};
    STAmount const toTransfer = amount + reward;
    auto const thTer = transferHelper(
        psb,
        account,
        dst,
        /*dstTag*/ std::nullopt,
        /*claimOwner*/ std::nullopt,
        toTransfer,
        CanCreateDstPolicy::Yes,
        DepositAuthPolicy::Normal,
        submittingAccountInfo,
        ctx_.journal);

    if (!isTesSuccess(thTer))
        return thTer;

    (*sleBridge)[sfXChainAccountCreateCount] = (*sleBridge)[sfXChainAccountCreateCount] + 1;
    psb.update(sleBridge);

    psb.apply(ctx_.rawView());

    return tesSUCCESS;
}

void
XChainCreateBridge::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
XChainCreateBridge::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

void
BridgeModify::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
BridgeModify::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

void
XChainClaim::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
XChainClaim::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

void
XChainCommit::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
XChainCommit::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

void
XChainCreateClaimID::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
XChainCreateClaimID::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

void
XChainAddClaimAttestation::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
XChainAddClaimAttestation::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

void
XChainAddAccountCreateAttestation::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
XChainAddAccountCreateAttestation::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

void
XChainCreateAccountCommit::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
XChainCreateAccountCommit::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
