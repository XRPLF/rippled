#include <xrpl/tx/transactors/voting/BallotCastVote.h>

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/PermissionedDEXHelpers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace xrpl {

namespace {

// Every ciphertext entry in a cast is a 66-byte ElGamal ciphertext; entries in
// the primary vote vector additionally carry a 33-byte Pedersen commitment.
bool
validCiphertextArray(STArray const& arr, bool requireCommitment)
{
    for (auto const& entry : arr)
    {
        if (!entry.isFieldPresent(sfEncryptedVote) ||
            entry[sfEncryptedVote].length() != kEcGamalEncryptedTotalLength ||
            !isValidCiphertext(entry[sfEncryptedVote]))
        {
            return false;
        }

        bool const hasCommitment = entry.isFieldPresent(sfAmountCommitment);
        if (requireCommitment != hasCommitment)
            return false;

        if (hasCommitment && !isValidCompressedECPoint(entry[sfAmountCommitment]))
            return false;

        // The primary vector also carries a per-option linkage proof binding the
        // ciphertext to its range-proven commitment.
        if (requireCommitment)
        {
            if (!entry.isFieldPresent(sfZKProof) ||
                entry[sfZKProof].length() != kEcSendSigmaProofLength)
                return false;
        }
    }
    return true;
}

// Homomorphic sum of the ciphertexts in a vote vector.
std::optional<Buffer>
sumCiphertexts(STArray const& arr)
{
    std::optional<Buffer> acc;
    for (auto const& entry : arr)
    {
        Slice const ct = entry[sfEncryptedVote];
        if (!acc)
        {
            acc.emplace(ct.data(), ct.size());
            continue;
        }
        acc = homomorphicAdd(*acc, ct);
        if (!acc)
            return std::nullopt;
    }
    return acc;
}

// Verify the homomorphic sum of a vote vector equals Enc(weight; R) under key.
// Revealing the aggregate randomness R leaks nothing (weight is public) but
// pins the vector's total to the voter's weight, blocking weight inflation.
TER
verifySumToWeight(
    STArray const& votes,
    Slice const& pubKey,
    std::uint64_t weight,
    Slice const& blinding)
{
    auto const sum = sumCiphertexts(votes);
    if (!sum)
        return tecBAD_PROOF;

    auto const expected = encryptAmount(weight, pubKey, blinding);
    if (!expected)
        return tecBAD_PROOF;

    if (sum->size() != expected->size() ||
        std::memcmp(sum->data(), expected->data(), sum->size()) != 0)
    {
        return tecBAD_PROOF;
    }
    return tesSUCCESS;
}

// Copy a cast vote vector into a lean stored form (ciphertext only).
STArray
storedVoteVector(STArray const& src)
{
    STArray out;
    for (auto const& entry : src)
    {
        STObject o = STObject::makeInnerObject(sfBallotOption);
        o.setFieldVL(sfEncryptedVote, entry[sfEncryptedVote]);
        out.push_back(std::move(o));
    }
    return out;
}

}  // namespace

bool
BallotCastVote::checkExtraFeatures(PreflightContext const& ctx)
{
    return !ctx.tx.isFieldPresent(sfCredentialIDs) || ctx.rules.enabled(featureCredentials);
}

NotTEC
BallotCastVote::preflight(PreflightContext const& ctx)
{
    auto const& votes = ctx.tx.getFieldArray(sfEncryptedVotes);
    if (votes.empty() || votes.size() > 8)
        return temMALFORMED;

    // Primary vote vector carries ciphertext + range-proof commitment per option.
    if (!validCiphertextArray(votes, /*requireCommitment=*/true))
        return temBAD_CIPHERTEXT;

    bool const hasAuditor = ctx.tx.isFieldPresent(sfAuditorEncryptedVotes);
    if (hasAuditor)
    {
        auto const& av = ctx.tx.getFieldArray(sfAuditorEncryptedVotes);
        if (av.size() != votes.size() || !validCiphertextArray(av, /*requireCommitment=*/false))
            return temBAD_CIPHERTEXT;
    }

    bool const hasVoterKey = ctx.tx.isFieldPresent(sfVoterPublicKey);
    bool const hasVoterVotes = ctx.tx.isFieldPresent(sfVoterEncryptedVotes);
    if (hasVoterKey != hasVoterVotes)
        return temMALFORMED;

    if (hasVoterKey)
    {
        if (!isValidCompressedECPoint(ctx.tx[sfVoterPublicKey]))
            return temMALFORMED;

        auto const& vv = ctx.tx.getFieldArray(sfVoterEncryptedVotes);
        if (vv.size() != votes.size() || !validCiphertextArray(vv, /*requireCommitment=*/false))
            return temBAD_CIPHERTEXT;
    }

    if (ctx.tx[sfZKProof].empty())
        return temMALFORMED;

    if (auto const err = credentials::checkFields(ctx.tx, ctx.rules, ctx.j); !isTesSuccess(err))
        return err;

    return tesSUCCESS;
}

XRPAmount
BallotCastVote::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    return Transactor::calculateBaseFee(view, tx, kConfidentialFeeMultiplier);
}

TER
BallotCastVote::preclaim(PreclaimContext const& ctx)
{
    auto const account = ctx.tx[sfAccount];
    if (!ctx.view.exists(keylet::account(account)))
        return terNO_ACCOUNT;

    auto const ballotID = ctx.tx[sfBallotID];
    auto const sleBallot = ctx.view.read(keylet::ballot(ballotID));
    if (!sleBallot)
        return tecNO_ENTRY;

    if (sleBallot->isFlag(lsfBallotFinalized))
        return tecBALLOT_CLOSED;

    // Voting window: [OpenTime, CloseTime).
    auto const now = ctx.view.parentCloseTime().time_since_epoch().count();
    if (now < (*sleBallot)[sfOpenTime])
        return tecBALLOT_NOT_OPEN;
    if (now >= (*sleBallot)[sfCloseTime])
        return tecBALLOT_CLOSED;

    // Vote vectors must have exactly one entry per option.
    std::uint8_t const n = (*sleBallot)[sfOptionCount];
    auto const& votes = ctx.tx.getFieldArray(sfEncryptedVotes);
    if (votes.size() != n)
        return tecBALLOT_BAD_OPTIONS;

    // Auditor / voter-recovery presence must match the ballot's configuration.
    bool const ballotHasAuditor = sleBallot->isFieldPresent(sfAuditorEncryptionKey);
    if (ballotHasAuditor != ctx.tx.isFieldPresent(sfAuditorEncryptedVotes))
        return tecNO_PERMISSION;

    bool const ballotRecoverable = sleBallot->isFlag(lsfVoterRecoverable);
    if (ballotRecoverable != ctx.tx.isFieldPresent(sfVoterPublicKey))
        return tecNO_PERMISSION;

    // One BallotVote per account per ballot.
    if (ctx.view.exists(keylet::ballotVote(ballotID, account)))
        return tecBALLOT_VOTED;

    bool const tokenMode = sleBallot->isFieldPresent(sfMPTokenIssuanceID);

    // Eligibility and weight.
    std::uint64_t weight = 0;
    if (tokenMode)
    {
        MPTIssue const mptIssue{(*sleBallot)[sfMPTokenIssuanceID]};
        if (!ctx.view.exists(keylet::mptoken(mptIssue.getMptID(), account)))
            return tecNO_ENTRY;

        auto const held = accountHolds(
            ctx.view,
            account,
            mptIssue,
            FreezeHandling::ZeroIfFrozen,
            AuthHandling::ZeroIfUnauthorized,
            ctx.j);
        if (held.mpt().value() <= 0)
            return tecNO_ENTRY;
        weight = static_cast<std::uint64_t>(held.mpt().value());
    }
    else
    {
        if (!permissioned_dex::accountInDomain(ctx.view, account, (*sleBallot)[sfDomainID]))
            return tecNO_PERMISSION;
        weight = 1;
    }

    // --- Cast proof verification -------------------------------------------
    auto const ctxHash = getBallotCastContextHash(account, ballotID, ctx.tx.getSeqProxy().value());
    auto const blindingFactor = ctx.tx[sfBlindingFactor];
    Slice const blinding{blindingFactor.data(), blindingFactor.size()};

    // 1. Range: each committed option value is non-negative.
    std::vector<Slice> commitments;
    commitments.reserve(votes.size());
    for (auto const& entry : votes)
        commitments.push_back(entry[sfAmountCommitment]);
    if (auto const ter = verifyBallotRangeProof(ctx.tx[sfZKProof], commitments, ctxHash);
        !isTesSuccess(ter))
        return ter;

    // 2. Sum-to-weight against the tally key.
    if (auto const ter = verifySumToWeight(votes, (*sleBallot)[sfTallyPublicKey], weight, blinding);
        !isTesSuccess(ter))
        return ter;

    // 3. Mirror consistency: auditor / voter vectors carry the same aggregate.
    if (ballotHasAuditor)
    {
        if (auto const ter = verifySumToWeight(
                ctx.tx.getFieldArray(sfAuditorEncryptedVotes),
                (*sleBallot)[sfAuditorEncryptionKey],
                weight,
                blinding);
            !isTesSuccess(ter))
            return ter;
    }
    if (ballotRecoverable)
    {
        if (auto const ter = verifySumToWeight(
                ctx.tx.getFieldArray(sfVoterEncryptedVotes),
                ctx.tx[sfVoterPublicKey],
                weight,
                blinding);
            !isTesSuccess(ter))
            return ter;
    }

    // 4. Per-option ciphertext-commitment linkage. Without it a voter could put
    //    (W+k, -k) in the ciphertexts while committing (W, 0): sum and range
    //    both pass, but the tally would be corrupted. Each option's proof shows
    //    its ciphertext (and any auditor/voter mirror, under shared randomness)
    //    encrypts exactly the range-proven committed value. Sound for both token
    //    and credential mode.
    Slice const tallyKey = (*sleBallot)[sfTallyPublicKey];
    STArray const* auditorVotes =
        ballotHasAuditor ? &ctx.tx.getFieldArray(sfAuditorEncryptedVotes) : nullptr;
    STArray const* voterVotes =
        ballotRecoverable ? &ctx.tx.getFieldArray(sfVoterEncryptedVotes) : nullptr;

    for (std::uint8_t i = 0; i < n; ++i)
    {
        Slice const tallyCt = votes[i][sfEncryptedVote];
        Slice const c1{tallyCt.data(), kEcCiphertextComponentLength};

        std::vector<Slice> mirrorKeys{tallyKey};
        std::vector<Slice> c2PerKey{
            Slice{tallyCt.data() + kEcCiphertextComponentLength, kEcCiphertextComponentLength}};

        // Auditor / voter mirrors must share the option's ElGamal randomness, so
        // their C1 must equal the tally C1; only then does one shared-C1 linkage
        // proof pin every mirror to the same value.
        auto const addMirror = [&](STArray const* arr, Slice const& key) -> bool {
            Slice const ct = (*arr)[i][sfEncryptedVote];
            if (std::memcmp(ct.data(), c1.data(), kEcCiphertextComponentLength) != 0)
                return false;
            mirrorKeys.push_back(key);
            c2PerKey.push_back(
                Slice{ct.data() + kEcCiphertextComponentLength, kEcCiphertextComponentLength});
            return true;
        };
        if (auditorVotes && !addMirror(auditorVotes, (*sleBallot)[sfAuditorEncryptionKey]))
            return tecBAD_PROOF;
        if (voterVotes && !addMirror(voterVotes, ctx.tx[sfVoterPublicKey]))
            return tecBAD_PROOF;

        if (auto const ter = verifyBallotVoteLinkage(
                mirrorKeys,
                c1,
                c2PerKey,
                votes[i][sfAmountCommitment],
                votes[i][sfZKProof],
                ctxHash);
            !isTesSuccess(ter))
        {
            return ter;
        }
    }

    return tesSUCCESS;
}

TER
BallotCastVote::doApply()
{
    auto const ballotID = ctx_.tx[sfBallotID];
    auto sleBallot = view().peek(keylet::ballot(ballotID));
    if (!sleBallot)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const sleOwner = view().peek(keylet::account(accountID_));
    if (!sleOwner)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    Slice const tallyKey = (*sleBallot)[sfTallyPublicKey];
    auto const blindingFactor = ctx_.tx[sfBlindingFactor];
    Slice const blinding{blindingFactor.data(), blindingFactor.size()};
    auto const& votes = ctx_.tx.getFieldArray(sfEncryptedVotes);
    STArray const& oldTally = sleBallot->getFieldArray(sfEncryptedTally);

    if (votes.size() != oldTally.size())
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // Homomorphically add each (re-randomized) vote ciphertext into its tally
    // counter. Re-randomizing with a public deterministic scalar keeps the
    // ledger reproducible while adding Enc(0), so the plaintext tally is
    // unchanged.
    STArray newTally;
    for (std::size_t i = 0; i < oldTally.size(); ++i)
    {
        auto rr = rerandomizeCiphertext(votes[i][sfEncryptedVote], tallyKey, blinding);
        if (!rr)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        auto sum = homomorphicAdd(oldTally[i][sfEncryptedVote], *rr);
        if (!sum)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        STObject entry = STObject::makeInnerObject(sfBallotOption);
        entry.setFieldVL(sfEncryptedVote, *sum);
        newTally.push_back(std::move(entry));
    }
    sleBallot->setFieldArray(sfEncryptedTally, newTally);
    (*sleBallot)[sfVoteCount] = (*sleBallot)[sfVoteCount] + 1u;

    // Recompute the public weight (mirrors preclaim; the lock is not yet set).
    bool const tokenMode = sleBallot->isFieldPresent(sfMPTokenIssuanceID);
    std::uint64_t weight = 1;
    std::optional<MPTIssue> mptIssue;
    if (tokenMode)
    {
        mptIssue.emplace((*sleBallot)[sfMPTokenIssuanceID]);
        auto const held = accountHolds(
            view(),
            accountID_,
            *mptIssue,
            FreezeHandling::ZeroIfFrozen,
            AuthHandling::ZeroIfUnauthorized,
            j_);
        weight = static_cast<std::uint64_t>(held.mpt().value());
    }

    // Create the BallotVote object.
    auto const voteKeylet = keylet::ballotVote(ballotID, accountID_);
    auto sleVote = std::make_shared<SLE>(voteKeylet);
    (*sleVote)[sfAccount] = accountID_;
    (*sleVote)[sfBallotID] = ballotID;
    (*sleVote)[sfBallotWeight] = weight;
    sleVote->setFieldArray(sfEncryptedVotes, storedVoteVector(votes));
    if (ctx_.tx.isFieldPresent(sfAuditorEncryptedVotes))
    {
        sleVote->setFieldArray(
            sfAuditorEncryptedVotes,
            storedVoteVector(ctx_.tx.getFieldArray(sfAuditorEncryptedVotes)));
    }
    if (ctx_.tx.isFieldPresent(sfVoterPublicKey))
    {
        (*sleVote)[sfVoterPublicKey] = ctx_.tx[sfVoterPublicKey];
        sleVote->setFieldArray(
            sfVoterEncryptedVotes, storedVoteVector(ctx_.tx.getFieldArray(sfVoterEncryptedVotes)));
    }

    if (auto const ret = checkReserve(
            ctx_.getApplyViewContext(), sleOwner, preFeeBalance_, {.ownerCountDelta = 1}, j_);
        !isTesSuccess(ret))
        return ret;

    auto const page =
        view().dirInsert(keylet::ownerDir(accountID_), voteKeylet, describeOwnerDir(accountID_));
    if (!page)
        return tecDIR_FULL;  // LCOV_EXCL_LINE
    (*sleVote)[sfOwnerNode] = *page;

    increaseOwnerCount(ctx_.getApplyViewContext(), sleOwner, 1, j_);
    addSponsorToLedgerEntry(ctx_.getApplyViewContext(), sleVote);
    view().insert(sleVote);

    // Token mode: lock the voter's balance until the ballot closes.
    if (tokenMode)
    {
        auto sleMpt = view().peek(keylet::mptoken(mptIssue->getMptID(), accountID_));
        if (!sleMpt)
            return tecINTERNAL;  // LCOV_EXCL_LINE
        (*sleMpt)[sfVoteLockedAmount] = weight;
        (*sleMpt)[sfBallotID] = ballotID;
        view().update(sleMpt);
    }

    view().update(sleBallot);
    return tesSUCCESS;
}

void
BallotCastVote::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
}

bool
BallotCastVote::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
