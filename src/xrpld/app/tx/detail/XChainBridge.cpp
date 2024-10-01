//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <xrpld/app/paths/Flow.h>
#include <xrpld/app/tx/detail/SignerEntries.h>
#include <xrpld/app/tx/detail/Transactor.h>
#include <xrpld/app/tx/detail/XChainBridge.h>
#include <xrpld/ledger/ApplyView.h>
#include <xrpld/ledger/PaymentSandbox.h>
#include <xrpld/ledger/View.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/XRPAmount.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XChainAttestations.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/st.h>
#include <unordered_map>
#include <unordered_set>

namespace ripple {

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

// Check that the public key is allowed to sign for the given account. If the
// account does not exist on the ledger, then the public key must be the master
// key for the given account if it existed. Otherwise the key must be an enabled
// master key or a regular key for the existing account.
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
            if (sleAttestationSigningAccount->getFieldU32(sfFlags) &
                lsfDisableMaster)
            {
                JLOG(j.trace()) << "Attempt to add an attestation with "
                                   "disabled master key.";
                return tecXCHAIN_BAD_PUBLIC_KEY_ACCOUNT_PAIR;
            }
        }
        else
        {
            // regular key
            if (std::optional<AccountID> regularKey =
                    (*sleAttestationSigningAccount)[~sfRegularKey];
                regularKey != accountFromPK)
            {
                if (!regularKey)
                {
                    JLOG(j.trace())
                        << "Attempt to add an attestation with "
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
            JLOG(j.trace())
                << "Attempt to add an attestation with non-existant account "
                   "and mismatched pk/account pair.";
            return tecXCHAIN_BAD_PUBLIC_KEY_ACCOUNT_PAIR;
        }
    }

    return tesSUCCESS;
}

// If there is a quorum of attestations for the given parameters, then
// return the reward accounts, otherwise return TER for the error.
// Also removes attestations that are no longer part of the signers list.
//
// Note: the dst parameter is what the attestations are attesting to, which
// is not always used (it is used when automatically triggering a transfer
// from an `addAttestation` transaction, it is not used in a `claim`
// transaction). If the `checkDst` parameter is `check`, the attestations
// must attest to this destination, if it is `ignore` then the `dst` of the
// attestations are not checked (as for a `claim` transaction)

enum class CheckDst { check, ignore };
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
    attestations.erase_if([&](auto const& a) {
        return checkAttestationPublicKey(
                   view, signersList, a.keyAccount, a.publicKey, j) !=
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
        if (matchR == nonDstMismatch ||
            (checkDst == CheckDst::check && matchR != match))
            continue;
        auto i = signersList.find(a.keyAccount);
        if (i == signersList.end())
        {
            UNREACHABLE(
                "ripple::claimHelper : invalid inputs");  // should have already
                                                          // been checked
            continue;
        }
        weight += i->second;
        rewardAccounts.push_back(a.rewardAccount);
    }

    if (weight >= quorum)
        return rewardAccounts;

    return Unexpected(tecXCHAIN_CLAIM_NO_QUORUM);
}

/**
 Handle a new attestation event.

 Attempt to add the given attestation and reconcile with the current
 signer's list. Attestations that are not part of the current signer's
 list will be removed.

 @param claimAtt New attestation to add. It will be added if it is not
 already part of the collection, or attests to a larger value.

 @param quorum Min weight required for a quorum

 @param signersList Map from signer's account id (derived from public keys)
 to the weight of that key.

 @return optional reward accounts. If after handling the new attestation
 there is a quorum for the amount specified on the new attestation, then
 return the reward accounts for that amount, otherwise return a nullopt.
 Note that if the signer's list changes and there have been `commit`
 transactions of different amounts then there may be a different subset that
 has reached quorum. However, to "trigger" that subset would require adding
 (or re-adding) an attestation that supports that subset.

 The reason for using a nullopt instead of an empty vector when a quorum is
 not reached is to allow for an interface where a quorum is reached but no
 rewards are distributed.

 @note This function is not called `add` because it does more than just
       add the new attestation (in fact, it may not add the attestation at
       all). Instead, it handles the event of a new attestation.
 */
struct OnNewAttestationResult
{
    std::optional<std::vector<AccountID>> rewardAccounts;
    // `changed` is true if the attestation collection changed in any way
    // (added/removed/changed)
    bool changed{false};
};

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
        if (checkAttestationPublicKey(
                view,
                signersList,
                att->attestationSignerAccount,
                att->publicKey,
                j) != tesSUCCESS)
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
                [&](auto const& a) {
                    return a.keyAccount == claimSigningAccount;
                });
            i != attestations.end())
        {
            // existing attestation
            // replace old attestation with new attestation
            *i = TAttestation{*att};
            changed = true;
        }
        else
        {
            attestations.emplace_back(*att);
            changed = true;
        }
    }

    auto r = claimHelper(
        attestations,
        view,
        typename TAttestation::MatchFields{*attBegin},
        CheckDst::check,
        quorum,
        signersList,
        j);

    if (!r.has_value())
        return {std::nullopt, changed};

    return {std::move(r.value()), changed};
};

// Check if there is a quorurm of attestations for the given amount and
// chain. If so return the reward accounts, if not return the tec code (most
// likely tecXCHAIN_CLAIM_NO_QUORUM)
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
    XChainClaimAttestation::MatchFields toMatch{
        sendingAmount, wasLockingChainSend, std::nullopt};
    return claimHelper(
        attestations, view, toMatch, CheckDst::ignore, quorum, signersList, j);
}

enum class CanCreateDstPolicy { no, yes };

enum class DepositAuthPolicy { normal, dstCanBypass };

// Allow the fee to dip into the reserve. To support this, information about the
// submitting account needs to be fed to the transfer helper.
struct TransferHelperSubmittingAccountInfo
{
    AccountID account;
    STAmount preFeeBalance;
    STAmount postFeeBalance;
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
    std::optional<TransferHelperSubmittingAccountInfo> const&
        submittingAccountInfo,
    beast::Journal j)
{
    if (dst == src)
        return tesSUCCESS;

    auto const dstK = keylet::account(dst);
    if (auto sleDst = psb.read(dstK))
    {
        // Check dst tag and deposit auth

        if ((sleDst->getFlags() & lsfRequireDestTag) && !dstTag)
            return tecDST_TAG_NEEDED;

        // If the destination is the claim owner, and this is a claim
        // transaction, that's the dst account sending funds to itself. It
        // can bypass deposit auth.
        bool const canBypassDepositAuth = dst == claimOwner &&
            depositAuthPolicy == DepositAuthPolicy::dstCanBypass;

        if (!canBypassDepositAuth && (sleDst->getFlags() & lsfDepositAuth) &&
            !psb.exists(keylet::depositPreauth(dst, src)))
        {
            return tecNO_PERMISSION;
        }
    }
    else if (!amt.native() || canCreate == CanCreateDstPolicy::no)
    {
        return tecNO_DST;
    }

    if (amt.native())
    {
        auto const sleSrc = psb.peek(keylet::account(src));
        ASSERT(
            sleSrc != nullptr,
            "ripple::transferHelper : non-null source account");
        if (!sleSrc)
            return tecINTERNAL;

        {
            auto const ownerCount = sleSrc->getFieldU32(sfOwnerCount);
            auto const reserve = psb.fees().accountReserve(ownerCount);

            auto const availableBalance = [&]() -> STAmount {
                STAmount const curBal = (*sleSrc)[sfBalance];
                // Checking that account == src and postFeeBalance == curBal is
                // not strictly nessisary, but helps protect against future
                // changes
                if (!submittingAccountInfo ||
                    submittingAccountInfo->account != src ||
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
            if (canCreate == CanCreateDstPolicy::no)
            {
                // Already checked, but OK to check again
                return tecNO_DST;
            }
            if (amt < psb.fees().accountReserve(0))
            {
                JLOG(j.trace()) << "Insufficient payment to create account.";
                return tecNO_DST_INSUF_XRP;
            }

            // Create the account.
            std::uint32_t const seqno{
                psb.rules().enabled(featureDeletableAccounts) ? psb.seq() : 1};

            sleDst = std::make_shared<SLE>(dstK);
            sleDst->setAccountID(sfAccount, dst);
            sleDst->setFieldU32(sfSequence, seqno);

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
        /*offer crossing*/ OfferCrossing::no,
        /*limit quality*/ std::nullopt,
        /*sendmax*/ std::nullopt,
        j);

    if (auto const r = result.result();
        isTesSuccess(r) || isTecClaim(r) || isTerRetry(r))
        return r;
    return tecXCHAIN_PAYMENT_FAILED;
}

/**  Action to take when the transfer from the door account to the dst fails

     @note This is useful to prevent a failed "create account" transaction from
           blocking subsequent "create account" transactions.
*/
enum class OnTransferFail {
    /** Remove the claim even if the transfer fails */
    removeClaim,
    /**  Keep the claim if the transfer fails */
    keepClaim
};

struct FinalizeClaimHelperResult
{
    /// TER for transfering the payment funds
    std::optional<TER> mainFundsTer;
    // TER for transfering the reward funds
    std::optional<TER> rewardTer;
    // TER for removing the sle (if is sle is to be removed)
    std::optional<TER> rmSleTer;

    // Helper to check for overall success. If there wasn't overall success the
    // individual ters can be used to decide what needs to be done.
    bool
    isTesSuccess() const
    {
        return mainFundsTer == tesSUCCESS && rewardTer == tesSUCCESS &&
            (!rmSleTer || *rmSleTer == tesSUCCESS);
    }

    TER
    ter() const
    {
        if ((!mainFundsTer || *mainFundsTer == tesSUCCESS) &&
            (!rewardTer || *rewardTer == tesSUCCESS) &&
            (!rmSleTer || *rmSleTer == tesSUCCESS))
            return tesSUCCESS;

        // if any phase return a tecINTERNAL or a tef, prefer returning those
        // codes
        if (mainFundsTer &&
            (isTefFailure(*mainFundsTer) || *mainFundsTer == tecINTERNAL))
            return *mainFundsTer;
        if (rewardTer &&
            (isTefFailure(*rewardTer) || *rewardTer == tecINTERNAL))
            return *rewardTer;
        if (rmSleTer && (isTefFailure(*rmSleTer) || *rmSleTer == tecINTERNAL))
            return *rmSleTer;

        // Only after the tecINTERNAL and tef are checked, return the first
        // non-success error code.
        if (mainFundsTer && mainFundsTer != tesSUCCESS)
            return *mainFundsTer;
        if (rewardTer && rewardTer != tesSUCCESS)
            return *rewardTer;
        if (rmSleTer && rmSleTer != tesSUCCESS)
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

    STXChainBridge::ChainType const dstChain =
        STXChainBridge::otherChain(srcChain);
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
        // If the claimid is removed, the rewards should be distributed
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
            CanCreateDstPolicy::yes,
            depositAuthPolicy,
            std::nullopt,
            j);

        if (!isTesSuccess(*result.mainFundsTer) &&
            onTransferFail == OnTransferFail::keepClaim)
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
                auto const round_mode =
                    innerSb.rules().enabled(fixXChainRewardRounding)
                    ? Number::rounding_mode::downward
                    : Number::getround();
                saveNumberRoundMode _{Number::setround(round_mode)};

                STAmount const den{rewardAccounts.size()};
                return divide(rewardPool, den, rewardPool.issue());
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
                    CanCreateDstPolicy::no,
                    DepositAuthPolicy::normal,
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
                return tecINTERNAL;

            return tesSUCCESS;
        }();

        if (!isTesSuccess(*result.rewardTer) &&
            (onTransferFail == OnTransferFail::keepClaim ||
             *result.rewardTer == tecINTERNAL))
        {
            return result;
        }

        if (!isTesSuccess(*result.mainFundsTer) ||
            isTesSuccess(*result.rewardTer))
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
            if (!outerSb.dirRemove(
                    keylet::ownerDir(cidOwner), page, sleClaimID->key(), true))
            {
                JLOG(j.fatal())
                    << "Unable to delete xchain seq number from owner.";
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

/** Get signers list corresponding to the account that owns the bridge

    @param view View to read the signer's list from.
    @param sleBridge Sle of the bridge.
    @param j Log

    @return map of the signer's list (AccountIDs and weights), the quorum, and
            error code
*/
std::tuple<std::unordered_map<AccountID, std::uint32_t>, std::uint32_t, TER>
getSignersListAndQuorum(
    ReadView const& view,
    SLE const& sleBridge,
    beast::Journal j)
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
    if (auto r = tryGet(STXChainBridge::ChainType::locking))
        return r;
    return tryGet(STXChainBridge::ChainType::issuing);
}

std::shared_ptr<SLE>
peekBridge(ApplyView& v, STXChainBridge const& bridgeSpec)
{
    return readOrpeekBridge<SLE>(
        [&v](STXChainBridge const& b, STXChainBridge::ChainType ct)
            -> std::shared_ptr<SLE> { return v.peek(keylet::bridge(b, ct)); },
        bridgeSpec);
}

std::shared_ptr<SLE const>
readBridge(ReadView const& v, STXChainBridge const& bridgeSpec)
{
    return readOrpeekBridge<SLE const>(
        [&v](STXChainBridge const& b, STXChainBridge::ChainType ct)
            -> std::shared_ptr<SLE const> {
            return v.read(keylet::bridge(b, ct));
        },
        bridgeSpec);
}

// Precondition: all the claims in the range are consistent. They must sign for
// the same event (amount, sending account, claim id, etc).
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

    auto const claimIDKeylet =
        keylet::xChainClaimID(bridgeSpec, attBegin->claimID);

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

        // Add claims that are part of the signer's list to the "claims" vector
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
            STXChainBridge::ChainType const dstChain =
                STXChainBridge::otherChain(srcChain);

            STXChainBridge::ChainType const attDstChain =
                STXChainBridge::dstChain(attBegin->wasLockingChainSend);

            if (attDstChain != dstChain)
            {
                return Unexpected(tecXCHAIN_WRONG_CHAIN);
            }
        }

        XChainClaimAttestations curAtts{
            sleClaimID->getFieldArray(sfXChainClaimAttestations)};

        auto const newAttResult = onNewAttestations(
            curAtts,
            view,
            &atts[0],
            &atts[0] + atts.size(),
            quorum,
            signersList,
            j);

        // update the claim id
        sleClaimID->setFieldArray(
            sfXChainClaimAttestations, curAtts.toSTArray());
        psb.update(sleClaimID);

        return ScopeResult{
            newAttResult,
            (*sleClaimID)[sfSignatureReward],
            (*sleClaimID)[sfAccount]};
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
            OnTransferFail::keepClaim,
            DepositAuthPolicy::normal,
            j);

        auto const rTer = r.ter();

        if (!isTesSuccess(rTer) &&
            (!attListChanged || rTer == tecINTERNAL || rTer == tefBAD_LEDGER))
            return rTer;
    }

    psb.apply(rawView);

    return tesSUCCESS;
}

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
    if (attBegin->createCount >= claimCount + xbridgeMaxAccountCreateClaims)
    {
        // Limit the number of claims on the account
        return tecXCHAIN_ACCOUNT_CREATE_TOO_MANY;
    }

    {
        STXChainBridge::ChainType const dstChain =
            STXChainBridge::otherChain(srcChain);

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
        bool createCID;
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

            // Check reserve
            auto const balance = (*sleDoor)[sfBalance];
            auto const reserve =
                psb.fees().accountReserve((*sleDoor)[sfOwnerCount] + 1);

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
                return XChainCreateAccountAttestations{
                    sleClaimID->getFieldArray(
                        sfXChainCreateAccountAttestations)};
            return XChainCreateAccountAttestations{};
        }();

        auto const newAttResult = onNewAttestations(
            curAtts,
            view,
            &atts[0],
            &atts[0] + atts.size(),
            quorum,
            signersList,
            j);

        if (!createCID)
        {
            // Modify the object before it's potentially deleted, so the meta
            // data will include the new attestations
            if (!sleClaimID)
                return Unexpected(tecINTERNAL);
            sleClaimID->setFieldArray(
                sfXChainCreateAccountAttestations, curAtts.toSTArray());
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
            OnTransferFail::removeClaim,
            DepositAuthPolicy::normal,
            j);

        auto const rTer = r.ter();

        if (!isTesSuccess(rTer))
        {
            if (rTer == tecINTERNAL || rTer == tecUNFUNDED_PAYMENT ||
                isTefFailure(rTer))
                return rTer;
        }
        // Move past this claim id even if it fails, so it doesn't block
        // subsequent claim ids
        auto const sleBridge = psb.peek(bridgeK);
        if (!sleBridge)
            return tecINTERNAL;
        (*sleBridge)[sfXChainAccountClaimCount] = attBegin->createCount;
        psb.update(sleBridge);
    }
    else if (createCID)
    {
        auto const createdSleClaimID = std::make_shared<SLE>(claimIDKeylet);
        (*createdSleClaimID)[sfAccount] = doorAccount;
        (*createdSleClaimID)[sfXChainBridge] = bridgeSpec;
        (*createdSleClaimID)[sfXChainAccountCreateCount] =
            attBegin->createCount;
        createdSleClaimID->setFieldArray(
            sfXChainCreateAccountAttestations, curAtts.toSTArray());

        // Add to owner directory of the door account
        auto const page = psb.dirInsert(
            keylet::ownerDir(doorAccount),
            claimIDKeylet,
            describeOwnerDir(doorAccount));
        if (!page)
            return tecDIR_FULL;
        (*createdSleClaimID)[sfOwnerNode] = *page;

        auto const sleDoor = psb.peek(doorK);
        if (!sleDoor)
            return tecINTERNAL;

        // Reserve was already checked
        adjustOwnerCount(psb, sleDoor, 1, j);
        psb.insert(createdSleClaimID);
        psb.update(sleDoor);
    }

    psb.apply(rawView);

    return tesSUCCESS;
}

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
    }
    return std::nullopt;
}

template <class TAttestation>
NotTEC
attestationPreflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureXChainBridge))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

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
    auto const expectedIssue =
        bridgeSpec.issue(STXChainBridge::srcChain(att->wasLockingChainSend));
    if (att->sendingAmount.issue() != expectedIssue)
        return temXCHAIN_BAD_PROOF;

    return preflight2(ctx);
}

template <class TAttestation>
TER
attestationPreclaim(PreclaimContext const& ctx)
{
    auto const att = toClaim<TAttestation>(ctx.tx);
    if (!att)
        return tecINTERNAL;  // checked in preflight

    STXChainBridge const bridgeSpec = ctx.tx[sfXChainBridge];
    auto const sleBridge = readBridge(ctx.view, bridgeSpec);
    if (!sleBridge)
    {
        return tecNO_ENTRY;
    }

    AccountID const attestationSignerAccount{
        ctx.tx[sfAttestationSignerAccount]};
    PublicKey const pk{ctx.tx[sfPublicKey]};

    // signersList is a map from account id to weights
    auto const [signersList, quorum, slTer] =
        getSignersListAndQuorum(ctx.view, *sleBridge, ctx.j);

    if (!isTesSuccess(slTer))
        return slTer;

    return checkAttestationPublicKey(
        ctx.view, signersList, attestationSignerAccount, pk, ctx.j);
}

template <class TAttestation>
TER
attestationDoApply(ApplyContext& ctx)
{
    auto const att = toClaim<TAttestation>(ctx.tx);
    if (!att)
        // Should already be checked in preflight
        return tecINTERNAL;

    STXChainBridge const bridgeSpec = ctx.tx[sfXChainBridge];

    struct ScopeResult
    {
        STXChainBridge::ChainType srcChain;
        std::unordered_map<AccountID, std::uint32_t> signersList;
        std::uint32_t quorum;
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

        STXChainBridge::ChainType dstChain = STXChainBridge::ChainType::locking;
        {
            if (thisDoor == bridgeSpec.lockingChainDoor())
                dstChain = STXChainBridge::ChainType::locking;
            else if (thisDoor == bridgeSpec.issuingChainDoor())
                dstChain = STXChainBridge::ChainType::issuing;
            else
                return Unexpected(tecINTERNAL);
        }
        STXChainBridge::ChainType const srcChain =
            STXChainBridge::otherChain(dstChain);

        // signersList is a map from account id to weights
        auto [signersList, quorum, slTer] =
            getSignersListAndQuorum(ctx.view(), *sleBridge, ctx.journal);

        if (!isTesSuccess(slTer))
            return Unexpected(slTer);

        return ScopeResult{
            srcChain, std::move(signersList), quorum, thisDoor, bridgeK};
    }();

    if (!scopeResult.has_value())
        return scopeResult.error();

    auto const& [srcChain, signersList, quorum, thisDoor, bridgeK] =
        scopeResult.value();

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
    else if constexpr (std::is_same_v<
                           TAttestation,
                           Attestations::AttestationCreateAccount>)
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

NotTEC
XChainCreateBridge::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureXChainBridge))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    auto const account = ctx.tx[sfAccount];
    auto const reward = ctx.tx[sfSignatureReward];
    auto const minAccountCreate = ctx.tx[~sfMinAccountCreateAmount];
    auto const bridgeSpec = ctx.tx[sfXChainBridge];
    // Doors must be distinct to help prevent transaction replay attacks
    if (bridgeSpec.lockingChainDoor() == bridgeSpec.issuingChainDoor())
    {
        return temXCHAIN_EQUAL_DOOR_ACCOUNTS;
    }

    if (bridgeSpec.lockingChainDoor() != account &&
        bridgeSpec.issuingChainDoor() != account)
    {
        return temXCHAIN_BRIDGE_NONDOOR_OWNER;
    }

    if (isXRP(bridgeSpec.lockingChainIssue()) !=
        isXRP(bridgeSpec.issuingChainIssue()))
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
         !isXRP(bridgeSpec.lockingChainIssue()) ||
         !isXRP(bridgeSpec.issuingChainIssue())))
    {
        return temXCHAIN_BRIDGE_BAD_MIN_ACCOUNT_CREATE_AMOUNT;
    }

    if (isXRP(bridgeSpec.issuingChainIssue()))
    {
        // Issuing account must be the root account for XRP (which presumably
        // owns all the XRP). This is done so the issuing account can't "run
        // out" of wrapped tokens.
        static auto const rootAccount = calcAccountID(
            generateKeyPair(
                KeyType::secp256k1, generateSeed("masterpassphrase"))
                .first);
        if (bridgeSpec.issuingChainDoor() != rootAccount)
        {
            return temXCHAIN_BRIDGE_BAD_ISSUES;
        }
    }
    else
    {
        // Issuing account must be the issuer for non-XRP. This is done so the
        // issuing account can't "run out" of wrapped tokens.
        if (bridgeSpec.issuingChainDoor() !=
            bridgeSpec.issuingChainIssue().account)
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

    return preflight2(ctx);
}

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

        if (hasBridge(STXChainBridge::ChainType::issuing) ||
            hasBridge(STXChainBridge::ChainType::locking))
        {
            return tecDUPLICATE;
        }
    }

    if (!isXRP(bridgeSpec.issue(chainType)))
    {
        auto const sleIssuer =
            ctx.view.read(keylet::account(bridgeSpec.issue(chainType).account));

        if (!sleIssuer)
            return tecNO_ISSUER;

        // Allowing clawing back funds would break the bridge's invariant that
        // wrapped funds are always backed by locked funds
        if (sleIssuer->getFlags() & lsfAllowTrustLineClawback)
            return tecNO_PERMISSION;
    }

    {
        // Check reserve
        auto const sleAcc = ctx.view.read(keylet::account(account));
        if (!sleAcc)
            return terNO_ACCOUNT;

        auto const balance = (*sleAcc)[sfBalance];
        auto const reserve =
            ctx.view.fees().accountReserve((*sleAcc)[sfOwnerCount] + 1);

        if (balance < reserve)
            return tecINSUFFICIENT_RESERVE;
    }

    return tesSUCCESS;
}

TER
XChainCreateBridge::doApply()
{
    auto const account = ctx_.tx[sfAccount];
    auto const bridgeSpec = ctx_.tx[sfXChainBridge];
    auto const reward = ctx_.tx[sfSignatureReward];
    auto const minAccountCreate = ctx_.tx[~sfMinAccountCreateAmount];

    auto const sleAcct = ctx_.view().peek(keylet::account(account));
    if (!sleAcct)
        return tecINTERNAL;

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

    // Add to owner directory
    {
        auto const page = ctx_.view().dirInsert(
            keylet::ownerDir(account), bridgeKeylet, describeOwnerDir(account));
        if (!page)
            return tecDIR_FULL;
        (*sleBridge)[sfOwnerNode] = *page;
    }

    adjustOwnerCount(ctx_.view(), sleAcct, 1, ctx_.journal);

    ctx_.view().insert(sleBridge);
    ctx_.view().update(sleAcct);

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

NotTEC
BridgeModify::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureXChainBridge))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfBridgeModifyMask)
        return temINVALID_FLAG;

    auto const account = ctx.tx[sfAccount];
    auto const reward = ctx.tx[~sfSignatureReward];
    auto const minAccountCreate = ctx.tx[~sfMinAccountCreateAmount];
    auto const bridgeSpec = ctx.tx[sfXChainBridge];
    bool const clearAccountCreate =
        ctx.tx.getFlags() & tfClearAccountCreateAmount;

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

    if (bridgeSpec.lockingChainDoor() != account &&
        bridgeSpec.issuingChainDoor() != account)
    {
        return temXCHAIN_BRIDGE_NONDOOR_OWNER;
    }

    if (reward && (!isXRP(*reward) || reward->signum() < 0))
    {
        return temXCHAIN_BRIDGE_BAD_REWARD_AMOUNT;
    }

    if (minAccountCreate &&
        ((!isXRP(*minAccountCreate) || minAccountCreate->signum() <= 0) ||
         !isXRP(bridgeSpec.lockingChainIssue()) ||
         !isXRP(bridgeSpec.issuingChainIssue())))
    {
        return temXCHAIN_BRIDGE_BAD_MIN_ACCOUNT_CREATE_AMOUNT;
    }

    return preflight2(ctx);
}

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

TER
BridgeModify::doApply()
{
    auto const account = ctx_.tx[sfAccount];
    auto const bridgeSpec = ctx_.tx[sfXChainBridge];
    auto const reward = ctx_.tx[~sfSignatureReward];
    auto const minAccountCreate = ctx_.tx[~sfMinAccountCreateAmount];
    bool const clearAccountCreate =
        ctx_.tx.getFlags() & tfClearAccountCreateAmount;

    auto const sleAcct = ctx_.view().peek(keylet::account(account));
    if (!sleAcct)
        return tecINTERNAL;

    STXChainBridge::ChainType const chainType =
        STXChainBridge::srcChain(account == bridgeSpec.lockingChainDoor());

    auto const sleBridge =
        ctx_.view().peek(keylet::bridge(bridgeSpec, chainType));
    if (!sleBridge)
        return tecINTERNAL;

    if (reward)
        (*sleBridge)[sfSignatureReward] = *reward;
    if (minAccountCreate)
    {
        (*sleBridge)[sfMinAccountCreateAmount] = *minAccountCreate;
    }
    if (clearAccountCreate &&
        sleBridge->isFieldPresent(sfMinAccountCreateAmount))
    {
        sleBridge->makeFieldAbsent(sfMinAccountCreateAmount);
    }
    ctx_.view().update(sleBridge);

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

NotTEC
XChainClaim::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureXChainBridge))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    STXChainBridge const bridgeSpec = ctx.tx[sfXChainBridge];
    auto const amount = ctx.tx[sfAmount];

    if (amount.signum() <= 0 ||
        (amount.issue() != bridgeSpec.lockingChainIssue() &&
         amount.issue() != bridgeSpec.issuingChainIssue()))
    {
        return temBAD_AMOUNT;
    }

    return preflight2(ctx);
}

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
            isLockingChain = true;
        else if (thisDoor == bridgeSpec.issuingChainDoor())
            isLockingChain = false;
        else
            return tecINTERNAL;
    }

    {
        // Check that the amount specified matches the expected issue

        if (isLockingChain)
        {
            if (bridgeSpec.lockingChainIssue() != thisChainAmount.issue())
                return tecXCHAIN_BAD_TRANSFER_ISSUE;
        }
        else
        {
            if (bridgeSpec.issuingChainIssue() != thisChainAmount.issue())
                return tecXCHAIN_BAD_TRANSFER_ISSUE;
        }
    }

    if (isXRP(bridgeSpec.lockingChainIssue()) !=
        isXRP(bridgeSpec.issuingChainIssue()))
    {
        // Should have been caught when creating the bridge
        // Detect here so `otherChainAmount` doesn't switch from IOU -> XRP
        // and the numeric issues that need to be addressed with that.
        return tecINTERNAL;
    }

    auto const otherChainAmount = [&]() -> STAmount {
        STAmount r(thisChainAmount);
        if (isLockingChain)
            r.setIssue(bridgeSpec.issuingChainIssue());
        else
            r.setIssue(bridgeSpec.lockingChainIssue());
        return r;
    }();

    auto const sleClaimID =
        ctx.view.read(keylet::xChainClaimID(bridgeSpec, claimID));
    {
        // Check that the sequence number is owned by the sender of this
        // transaction
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

        STXChainBridge::ChainType dstChain = STXChainBridge::ChainType::locking;
        {
            if (thisDoor == bridgeSpec.lockingChainDoor())
                dstChain = STXChainBridge::ChainType::locking;
            else if (thisDoor == bridgeSpec.issuingChainDoor())
                dstChain = STXChainBridge::ChainType::issuing;
            else
                return Unexpected(tecINTERNAL);
        }
        STXChainBridge::ChainType const srcChain =
            STXChainBridge::otherChain(dstChain);

        auto const sendingAmount = [&]() -> STAmount {
            STAmount r(thisChainAmount);
            r.setIssue(bridgeSpec.issue(srcChain));
            return r;
        }();

        auto const [signersList, quorum, slTer] =
            getSignersListAndQuorum(ctx_.view(), *sleBridge, ctx_.journal);

        if (!isTesSuccess(slTer))
            return Unexpected(slTer);

        XChainClaimAttestations curAtts{
            sleClaimID->getFieldArray(sfXChainClaimAttestations)};

        auto const claimR = onClaim(
            curAtts,
            psb,
            sendingAmount,
            /*wasLockingChainSend*/ srcChain ==
                STXChainBridge::ChainType::locking,
            quorum,
            signersList,
            ctx_.journal);
        if (!claimR.has_value())
            return Unexpected(claimR.error());

        return ScopeResult{
            claimR.value(),
            (*sleClaimID)[sfAccount],
            sendingAmount,
            srcChain,
            (*sleClaimID)[sfSignatureReward],
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
        OnTransferFail::keepClaim,
        DepositAuthPolicy::dstCanBypass,
        ctx_.journal);
    if (!r.isTesSuccess())
        return r.ter();

    psb.apply(ctx_.rawView());

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

TxConsequences
XChainCommit::makeTxConsequences(PreflightContext const& ctx)
{
    auto const maxSpend = [&] {
        auto const amount = ctx.tx[sfAmount];
        if (amount.native() && amount.signum() > 0)
            return amount.xrp();
        return XRPAmount{beast::zero};
    }();

    return TxConsequences{ctx.tx, maxSpend};
}

NotTEC
XChainCommit::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureXChainBridge))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    auto const amount = ctx.tx[sfAmount];
    auto const bridgeSpec = ctx.tx[sfXChainBridge];

    if (amount.signum() <= 0 || !isLegalNet(amount))
        return temBAD_AMOUNT;

    if (amount.issue() != bridgeSpec.lockingChainIssue() &&
        amount.issue() != bridgeSpec.issuingChainIssue())
        return temBAD_ISSUER;

    return preflight2(ctx);
}

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
            isLockingChain = true;
        else if (thisDoor == bridgeSpec.issuingChainDoor())
            isLockingChain = false;
        else
            return tecINTERNAL;
    }

    if (isLockingChain)
    {
        if (bridgeSpec.lockingChainIssue() != ctx.tx[sfAmount].issue())
            return tecXCHAIN_BAD_TRANSFER_ISSUE;
    }
    else
    {
        if (bridgeSpec.issuingChainIssue() != ctx.tx[sfAmount].issue())
            return tecXCHAIN_BAD_TRANSFER_ISSUE;
    }

    return tesSUCCESS;
}

TER
XChainCommit::doApply()
{
    PaymentSandbox psb(&ctx_.view());

    auto const account = ctx_.tx[sfAccount];
    auto const amount = ctx_.tx[sfAmount];
    auto const bridgeSpec = ctx_.tx[sfXChainBridge];

    if (!psb.read(keylet::account(account)))
        return tecINTERNAL;

    auto const sleBridge = readBridge(psb, bridgeSpec);
    if (!sleBridge)
        return tecINTERNAL;

    auto const dst = (*sleBridge)[sfAccount];

    // Support dipping into reserves to pay the fee
    TransferHelperSubmittingAccountInfo submittingAccountInfo{
        account_, mPriorBalance, mSourceBalance};

    auto const thTer = transferHelper(
        psb,
        account,
        dst,
        /*dstTag*/ std::nullopt,
        /*claimOwner*/ std::nullopt,
        amount,
        CanCreateDstPolicy::no,
        DepositAuthPolicy::normal,
        submittingAccountInfo,
        ctx_.journal);

    if (!isTesSuccess(thTer))
        return thTer;

    psb.apply(ctx_.rawView());

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

NotTEC
XChainCreateClaimID::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureXChainBridge))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    auto const reward = ctx.tx[sfSignatureReward];

    if (!isXRP(reward) || reward.signum() < 0 || !isLegalNet(reward))
        return temXCHAIN_BRIDGE_BAD_REWARD_AMOUNT;

    return preflight2(ctx);
}

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

    // Check that the reward matches
    auto const reward = ctx.tx[sfSignatureReward];

    if (reward != (*sleBridge)[sfSignatureReward])
    {
        return tecXCHAIN_REWARD_MISMATCH;
    }

    {
        // Check reserve
        auto const sleAcc = ctx.view.read(keylet::account(account));
        if (!sleAcc)
            return terNO_ACCOUNT;

        auto const balance = (*sleAcc)[sfBalance];
        auto const reserve =
            ctx.view.fees().accountReserve((*sleAcc)[sfOwnerCount] + 1);

        if (balance < reserve)
            return tecINSUFFICIENT_RESERVE;
    }

    return tesSUCCESS;
}

TER
XChainCreateClaimID::doApply()
{
    auto const account = ctx_.tx[sfAccount];
    auto const bridgeSpec = ctx_.tx[sfXChainBridge];
    auto const reward = ctx_.tx[sfSignatureReward];
    auto const otherChainSrc = ctx_.tx[sfOtherChainSource];

    auto const sleAcct = ctx_.view().peek(keylet::account(account));
    if (!sleAcct)
        return tecINTERNAL;

    auto const sleBridge = peekBridge(ctx_.view(), bridgeSpec);
    if (!sleBridge)
        return tecINTERNAL;

    std::uint32_t const claimID = (*sleBridge)[sfXChainClaimID] + 1;
    if (claimID == 0)
        return tecINTERNAL;  // overflow

    (*sleBridge)[sfXChainClaimID] = claimID;

    Keylet const claimIDKeylet = keylet::xChainClaimID(bridgeSpec, claimID);
    if (ctx_.view().exists(claimIDKeylet))
        return tecINTERNAL;  // already checked out!?!

    auto const sleClaimID = std::make_shared<SLE>(claimIDKeylet);

    (*sleClaimID)[sfAccount] = account;
    (*sleClaimID)[sfXChainBridge] = bridgeSpec;
    (*sleClaimID)[sfXChainClaimID] = claimID;
    (*sleClaimID)[sfOtherChainSource] = otherChainSrc;
    (*sleClaimID)[sfSignatureReward] = reward;
    sleClaimID->setFieldArray(
        sfXChainClaimAttestations, STArray{sfXChainClaimAttestations});

    // Add to owner directory
    {
        auto const page = ctx_.view().dirInsert(
            keylet::ownerDir(account),
            claimIDKeylet,
            describeOwnerDir(account));
        if (!page)
            return tecDIR_FULL;
        (*sleClaimID)[sfOwnerNode] = *page;
    }

    adjustOwnerCount(ctx_.view(), sleAcct, 1, ctx_.journal);

    ctx_.view().insert(sleClaimID);
    ctx_.view().update(sleBridge);
    ctx_.view().update(sleAcct);

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

NotTEC
XChainAddClaimAttestation::preflight(PreflightContext const& ctx)
{
    return attestationPreflight<Attestations::AttestationClaim>(ctx);
}

TER
XChainAddClaimAttestation::preclaim(PreclaimContext const& ctx)
{
    return attestationPreclaim<Attestations::AttestationClaim>(ctx);
}

TER
XChainAddClaimAttestation::doApply()
{
    return attestationDoApply<Attestations::AttestationClaim>(ctx_);
}

//------------------------------------------------------------------------------

NotTEC
XChainAddAccountCreateAttestation::preflight(PreflightContext const& ctx)
{
    return attestationPreflight<Attestations::AttestationCreateAccount>(ctx);
}

TER
XChainAddAccountCreateAttestation::preclaim(PreclaimContext const& ctx)
{
    return attestationPreclaim<Attestations::AttestationCreateAccount>(ctx);
}

TER
XChainAddAccountCreateAttestation::doApply()
{
    return attestationDoApply<Attestations::AttestationCreateAccount>(ctx_);
}

//------------------------------------------------------------------------------

NotTEC
XChainCreateAccountCommit::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureXChainBridge))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    auto const amount = ctx.tx[sfAmount];

    if (amount.signum() <= 0 || !amount.native())
        return temBAD_AMOUNT;

    auto const reward = ctx.tx[sfSignatureReward];
    if (reward.signum() < 0 || !reward.native())
        return temBAD_AMOUNT;

    if (reward.issue() != amount.issue())
        return temBAD_AMOUNT;

    return preflight2(ctx);
}

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

    std::optional<STAmount> const minCreateAmount =
        (*sleBridge)[~sfMinAccountCreateAmount];

    if (!minCreateAmount)
        return tecXCHAIN_CREATE_ACCOUNT_DISABLED;

    if (amount < *minCreateAmount)
        return tecXCHAIN_INSUFF_CREATE_AMOUNT;

    if (minCreateAmount->issue() != amount.issue())
        return tecXCHAIN_BAD_TRANSFER_ISSUE;

    AccountID const thisDoor = (*sleBridge)[sfAccount];
    AccountID const account = ctx.tx[sfAccount];
    if (thisDoor == account)
    {
        // Door account can't lock funds onto itself
        return tecXCHAIN_SELF_COMMIT;
    }

    STXChainBridge::ChainType srcChain = STXChainBridge::ChainType::locking;
    {
        if (thisDoor == bridgeSpec.lockingChainDoor())
            srcChain = STXChainBridge::ChainType::locking;
        else if (thisDoor == bridgeSpec.issuingChainDoor())
            srcChain = STXChainBridge::ChainType::issuing;
        else
            return tecINTERNAL;
    }
    STXChainBridge::ChainType const dstChain =
        STXChainBridge::otherChain(srcChain);

    if (bridgeSpec.issue(srcChain) != ctx.tx[sfAmount].issue())
        return tecXCHAIN_BAD_TRANSFER_ISSUE;

    if (!isXRP(bridgeSpec.issue(dstChain)))
        return tecXCHAIN_CREATE_ACCOUNT_NONXRP_ISSUE;

    return tesSUCCESS;
}

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
        return tecINTERNAL;

    auto const sleBridge = peekBridge(psb, bridge);
    if (!sleBridge)
        return tecINTERNAL;

    auto const dst = (*sleBridge)[sfAccount];

    // Support dipping into reserves to pay the fee
    TransferHelperSubmittingAccountInfo submittingAccountInfo{
        account_, mPriorBalance, mSourceBalance};
    STAmount const toTransfer = amount + reward;
    auto const thTer = transferHelper(
        psb,
        account,
        dst,
        /*dstTag*/ std::nullopt,
        /*claimOwner*/ std::nullopt,
        toTransfer,
        CanCreateDstPolicy::yes,
        DepositAuthPolicy::normal,
        submittingAccountInfo,
        ctx_.journal);

    if (!isTesSuccess(thTer))
        return thTer;

    (*sleBridge)[sfXChainAccountCreateCount] =
        (*sleBridge)[sfXChainAccountCreateCount] + 1;
    psb.update(sleBridge);

    psb.apply(ctx_.rawView());

    return tesSUCCESS;
}

}  // namespace ripple
