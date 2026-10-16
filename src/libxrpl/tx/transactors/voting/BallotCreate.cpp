#include <xrpl/tx/transactors/voting/BallotCreate.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/tx/Transactor.h>

#include <array>
#include <cstdint>
#include <memory>

namespace xrpl {

// Ballots allow between 2 and 8 options, inclusive.
static constexpr std::uint8_t kMinBallotOptions = 2;
static constexpr std::uint8_t kMaxBallotOptions = 8;
static constexpr std::size_t kMaxBallotUriLength = 256;

bool
BallotCreate::checkExtraFeatures(PreflightContext const& ctx)
{
    // Credential mode needs the permissioned-domain and credential stacks;
    // the base amendment is gated by the TRANSACTION() macro.
    if (ctx.tx.isFieldPresent(sfDomainID))
        return ctx.rules.enabled(featurePermissionedDomains) &&
            ctx.rules.enabled(featureCredentials);
    return true;
}

std::uint32_t
BallotCreate::getFlagsMask(PreflightContext const&)
{
    return tfBallotCreateMask;
}

NotTEC
BallotCreate::preflight(PreflightContext const& ctx)
{
    // Exactly one eligibility source must be present.
    bool const hasIssuance = ctx.tx.isFieldPresent(sfMPTokenIssuanceID);
    bool const hasDomain = ctx.tx.isFieldPresent(sfDomainID);
    if (hasIssuance == hasDomain)
        return temMALFORMED;

    if (hasDomain && ctx.tx[sfDomainID] == beast::kZero)
        return temMALFORMED;

    auto const optionCount = ctx.tx[sfOptionCount];
    if (optionCount < kMinBallotOptions || optionCount > kMaxBallotOptions)
        return temINVALID_COUNT;

    // Voting window must be non-empty.
    if (ctx.tx[sfOpenTime] >= ctx.tx[sfCloseTime])
        return temMALFORMED;

    // The tally key is an ElGamal public key registered with a Schnorr proof.
    if (!isValidCompressedECPoint(ctx.tx[sfTallyPublicKey]))
        return temMALFORMED;

    if (ctx.tx[sfZKProof].size() != kEcSchnorrProofLength)
        return temMALFORMED;

    if (ctx.tx.isFieldPresent(sfAuditorEncryptionKey) &&
        !isValidCompressedECPoint(ctx.tx[sfAuditorEncryptionKey]))
    {
        return temMALFORMED;
    }

    if (auto const uri = ctx.tx[~sfURI]; uri && (uri->empty() || uri->size() > kMaxBallotUriLength))
        return temMALFORMED;

    return tesSUCCESS;
}

XRPAmount
BallotCreate::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    return Transactor::calculateBaseFee(view, tx, kConfidentialFeeMultiplier);
}

TER
BallotCreate::preclaim(PreclaimContext const& ctx)
{
    auto const account = ctx.tx[sfAccount];
    if (!ctx.view.exists(keylet::account(account)))
        return terNO_ACCOUNT;

    // The Schnorr proof of possession is bound to the ballot's own index.
    auto const ballotKeylet = keylet::ballot(account, ctx.tx.getSeqProxy().value());
    if (auto const ter =
            verifySchnorrProof(ctx.tx[sfTallyPublicKey], ctx.tx[sfZKProof], ballotKeylet.key);
        !isTesSuccess(ter))
        return ter;

    if (ctx.tx.isFieldPresent(sfMPTokenIssuanceID))
    {
        auto const sleIssuance =
            ctx.view.read(keylet::mptokenIssuance(ctx.tx[sfMPTokenIssuanceID]));
        if (!sleIssuance)
            return tecOBJECT_NOT_FOUND;

        // Only the issuer (in this v1) may create a token-mode ballot.
        if (sleIssuance->getAccountID(sfIssuer) != account)
            return tecNO_PERMISSION;

        // At most one open ballot per issuance, so a single vote-lock field on
        // each MPToken is sufficient. A stale marker for a closed ballot is
        // overwritten.
        if (auto const openID = (*sleIssuance)[~sfBallotID])
        {
            if (auto const sleOpen = ctx.view.read(keylet::ballot(*openID)); sleOpen &&
                ctx.view.parentCloseTime().time_since_epoch().count() < (*sleOpen)[sfCloseTime])
            {
                return tecBALLOT_EXISTS;
            }
        }
    }
    else
    {
        auto const slePD = ctx.view.read(keylet::permissionedDomain(ctx.tx[sfDomainID]));
        if (!slePD)
            return tecOBJECT_NOT_FOUND;

        // Only the domain owner (in this v1) may create a credential-mode ballot.
        if (slePD->getAccountID(sfOwner) != account)
            return tecNO_PERMISSION;
    }

    return tesSUCCESS;
}

TER
BallotCreate::doApply()
{
    auto const sleOwner = view().peek(keylet::account(accountID_));
    if (!sleOwner)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    if (auto const ret = checkReserve(
            ctx_.getApplyViewContext(), sleOwner, preFeeBalance_, {.ownerCountDelta = 1}, j_);
        !isTesSuccess(ret))
        return ret;

    auto const seq = ctx_.tx.getSeqProxy().value();
    auto const ballotKeylet = keylet::ballot(accountID_, seq);
    auto sleBallot = std::make_shared<SLE>(ballotKeylet);

    (*sleBallot)[sfOwner] = accountID_;
    (*sleBallot)[sfSequence] = seq;
    (*sleBallot)[sfDigest] = ctx_.tx[sfDigest];
    (*sleBallot)[sfOptionCount] = ctx_.tx[sfOptionCount];
    (*sleBallot)[sfTallyPublicKey] = ctx_.tx[sfTallyPublicKey];
    (*sleBallot)[sfOpenTime] = ctx_.tx[sfOpenTime];
    (*sleBallot)[sfCloseTime] = ctx_.tx[sfCloseTime];
    (*sleBallot)[sfVoteCount] = 0u;

    std::uint32_t flags = 0;
    if (ctx_.tx.isFlag(tfVoterRecoverable))
        flags |= lsfVoterRecoverable;
    (*sleBallot)[sfFlags] = flags;

    if (ctx_.tx.isFieldPresent(sfMPTokenIssuanceID))
        (*sleBallot)[sfMPTokenIssuanceID] = ctx_.tx[sfMPTokenIssuanceID];
    else
        (*sleBallot)[sfDomainID] = ctx_.tx[sfDomainID];

    if (ctx_.tx.isFieldPresent(sfURI))
        (*sleBallot)[sfURI] = ctx_.tx[sfURI];

    if (ctx_.tx.isFieldPresent(sfAuditorEncryptionKey))
        (*sleBallot)[sfAuditorEncryptionKey] = ctx_.tx[sfAuditorEncryptionKey];

    // Initialize each per-option counter to a deterministic Enc(0) under the
    // tally key. The blinding is derived from the ballot index and option so
    // every validator computes the identical genesis ciphertext, yet it is a
    // valid (non-identity) EC point — Enc(0; 0) would be the point at infinity
    // and fail serialization. It decrypts to 0 regardless of the randomness.
    STArray tally;
    for (std::uint8_t i = 0; i < ctx_.tx[sfOptionCount]; ++i)
    {
        auto const blinding = sha512Half(ballotKeylet.key, i);
        auto const zero =
            encryptAmount(0, ctx_.tx[sfTallyPublicKey], Slice(blinding.data(), blinding.size()));
        if (!zero)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        STObject entry = STObject::makeInnerObject(sfBallotOption);
        entry.setFieldVL(sfEncryptedVote, *zero);
        tally.push_back(std::move(entry));
    }
    sleBallot->setFieldArray(sfEncryptedTally, tally);

    auto const page =
        view().dirInsert(keylet::ownerDir(accountID_), ballotKeylet, describeOwnerDir(accountID_));
    if (!page)
        return tecDIR_FULL;  // LCOV_EXCL_LINE
    (*sleBallot)[sfOwnerNode] = *page;

    increaseOwnerCount(ctx_.getApplyViewContext(), sleOwner, 1, j_);
    addSponsorToLedgerEntry(ctx_.getApplyViewContext(), sleBallot);

    view().insert(sleBallot);

    // Record the open ballot on the issuance so a second one is rejected.
    if (ctx_.tx.isFieldPresent(sfMPTokenIssuanceID))
    {
        auto sleIssuance = view().peek(keylet::mptokenIssuance(ctx_.tx[sfMPTokenIssuanceID]));
        if (!sleIssuance)
            return tecINTERNAL;  // LCOV_EXCL_LINE
        (*sleIssuance)[sfBallotID] = ballotKeylet.key;
        view().update(sleIssuance);
    }

    return tesSUCCESS;
}

void
BallotCreate::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
}

bool
BallotCreate::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
