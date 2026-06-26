#include <xrpl/protocol/ConfidentialTransfer.h>

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

#include <openssl/rand.h>
#include <utility/mpt_utility.h>

#include <mpt_protocol.h>
#include <secp256k1.h>
#include <secp256k1_mpt.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <vector>

namespace xrpl {
namespace {

account_id
toAccountId(AccountID const& account)
{
    account_id res;
    std::memcpy(res.bytes, account.data(), kMPT_ACCOUNT_ID_SIZE);
    return res;
}

mpt_issuance_id
toIssuanceId(uint192 const& issuance)
{
    mpt_issuance_id res;
    std::memcpy(res.bytes, issuance.data(), kMPT_ISSUANCE_ID_SIZE);
    return res;
}

/**
 * @brief Pack a ConfidentialRecipient (public key + ElGamal ciphertext) into the
 * secp256k1-mpt participant struct. Callers MUST have already validated that
 * r.publicKey.size() == kEcPubKeyLength and
 * r.encryptedAmount.size() == kEcGamalEncryptedTotalLength;
 */
mpt_confidential_participant
toParticipant(ConfidentialRecipient const& r)
{
    mpt_confidential_participant p{};
    std::memcpy(p.pubkey, r.publicKey.data(), kEcPubKeyLength);
    std::memcpy(p.ciphertext, r.encryptedAmount.data(), kEcGamalEncryptedTotalLength);
    return p;
}

}  // namespace

uint256
getSendContextHash(
    AccountID const& account,
    uint192 const& issuanceID,
    std::uint32_t sequence,
    AccountID const& destination,
    std::uint32_t version)
{
    uint256 result;
    mpt_get_send_context_hash(
        toAccountId(account),
        toIssuanceId(issuanceID),
        sequence,
        toAccountId(destination),
        version,
        result.data());
    return result;
}

uint256
getClawbackContextHash(
    AccountID const& account,
    uint192 const& issuanceID,
    std::uint32_t sequence,
    AccountID const& holder)
{
    uint256 result;
    mpt_get_clawback_context_hash(
        toAccountId(account),
        toIssuanceId(issuanceID),
        sequence,
        toAccountId(holder),
        result.data());
    return result;
}

uint256
getConvertContextHash(AccountID const& account, uint192 const& issuanceID, std::uint32_t sequence)
{
    uint256 result;
    mpt_get_convert_context_hash(
        toAccountId(account), toIssuanceId(issuanceID), sequence, result.data());
    return result;
}

uint256
getConvertBackContextHash(
    AccountID const& account,
    uint192 const& issuanceID,
    std::uint32_t sequence,
    std::uint32_t version)
{
    uint256 result;
    mpt_get_convert_back_context_hash(
        toAccountId(account), toIssuanceId(issuanceID), sequence, version, result.data());
    return result;
}

std::optional<EcPair>
makeEcPair(Slice const& buffer)
{
    if (buffer.length() != 2 * kEcCiphertextComponentLength)
        return std::nullopt;  // LCOV_EXCL_LINE

    auto parsePubKey = [](Slice const& slice, secp256k1_pubkey& out) {
        return secp256k1_ec_pubkey_parse(secp256k1Context(), &out, slice.data(), slice.length());
    };

    Slice const s1{buffer.data(), kEcCiphertextComponentLength};
    Slice const s2{buffer.data() + kEcCiphertextComponentLength, kEcCiphertextComponentLength};

    EcPair pair{};
    if (parsePubKey(s1, pair.c1) != 1 || parsePubKey(s2, pair.c2) != 1)
        return std::nullopt;

    return pair;
}

std::optional<Buffer>
serializeEcPair(EcPair const& pair)
{
    auto serializePubKey = [](secp256k1_pubkey const& pub, unsigned char* out) {
        size_t outLen = kEcCiphertextComponentLength;  // 33 bytes
        auto const ret = secp256k1_ec_pubkey_serialize(
            secp256k1Context(), out, &outLen, &pub, SECP256K1_EC_COMPRESSED);
        return ret == 1 && outLen == kEcCiphertextComponentLength;
    };

    Buffer buffer(kEcGamalEncryptedTotalLength);
    auto const ptr = buffer.data();
    bool const res1 = serializePubKey(pair.c1, ptr);
    bool const res2 = serializePubKey(pair.c2, ptr + kEcCiphertextComponentLength);

    if (!res1 || !res2)
        return std::nullopt;

    return buffer;
}

bool
isValidCiphertext(Slice const& buffer)
{
    return makeEcPair(buffer).has_value();
}

bool
isValidCompressedECPoint(Slice const& buffer)
{
    if (buffer.size() != kCompressedEcPointLength)
        return false;

    // Compressed EC points must start with 0x02 or 0x03
    if (buffer[0] != kEcCompressedPrefixEvenY && buffer[0] != kEcCompressedPrefixOddY)
        return false;

    secp256k1_pubkey point;
    return secp256k1_ec_pubkey_parse(secp256k1Context(), &point, buffer.data(), buffer.size()) == 1;
}

std::optional<Buffer>
homomorphicAdd(Slice const& a, Slice const& b)
{
    if (a.length() != kEcGamalEncryptedTotalLength || b.length() != kEcGamalEncryptedTotalLength)
        return std::nullopt;

    auto const pairA = makeEcPair(a);
    auto const pairB = makeEcPair(b);

    if (!pairA || !pairB)
        return std::nullopt;

    EcPair sum{};
    if (auto res = secp256k1_elgamal_add(
            secp256k1Context(), &sum.c1, &sum.c2, &pairA->c1, &pairA->c2, &pairB->c1, &pairB->c2);
        res != 1)
    {
        return std::nullopt;
    }

    return serializeEcPair(sum);
}

std::optional<Buffer>
homomorphicSubtract(Slice const& a, Slice const& b)
{
    if (a.length() != kEcGamalEncryptedTotalLength || b.length() != kEcGamalEncryptedTotalLength)
        return std::nullopt;

    auto const pairA = makeEcPair(a);
    auto const pairB = makeEcPair(b);

    if (!pairA || !pairB)
        return std::nullopt;

    EcPair diff{};
    if (auto const res = secp256k1_elgamal_subtract(
            secp256k1Context(), &diff.c1, &diff.c2, &pairA->c1, &pairA->c2, &pairB->c1, &pairB->c2);
        res != 1)
    {
        return std::nullopt;
    }

    return serializeEcPair(diff);
}

std::optional<Buffer>
rerandomizeCiphertext(Slice const& ciphertext, Slice const& pubKeySlice, Slice const& randomness)
{
    auto zero = encryptAmount(0, pubKeySlice, randomness);
    if (!zero)
        return std::nullopt;

    return homomorphicAdd(ciphertext, *zero);
}

Buffer
generateBlindingFactor()
{
    unsigned char blindingFactor[kEcBlindingFactorLength];

    // todo: might need to be updated using another RNG
    if (RAND_bytes(blindingFactor, kEcBlindingFactorLength) != 1)
        Throw<std::runtime_error>("Failed to generate random number");

    return Buffer(blindingFactor, kEcBlindingFactorLength);
}

std::optional<Buffer>
encryptAmount(uint64_t const amt, Slice const& pubKeySlice, Slice const& blindingFactor)
{
    if (blindingFactor.size() != kEcBlindingFactorLength || pubKeySlice.size() != kEcPubKeyLength)
        return std::nullopt;

    Buffer out(kEcGamalEncryptedTotalLength);
    if (mpt_encrypt_amount(amt, pubKeySlice.data(), blindingFactor.data(), out.data()) != 0)
        return std::nullopt;

    return out;
}

std::optional<Buffer>
encryptCanonicalZeroAmount(Slice const& pubKeySlice, AccountID const& account, MPTID const& mptId)
{
    if (pubKeySlice.size() != kEcPubKeyLength)
        return std::nullopt;  // LCOV_EXCL_LINE

    EcPair pair{};
    secp256k1_pubkey pubKey;
    if (auto res = secp256k1_ec_pubkey_parse(
            secp256k1Context(), &pubKey, pubKeySlice.data(), kEcPubKeyLength);
        res != 1)
    {
        return std::nullopt;  // LCOV_EXCL_LINE
    }

    if (auto res = generate_canonical_encrypted_zero(
            secp256k1Context(), &pair.c1, &pair.c2, &pubKey, account.data(), mptId.data());
        res != 1)
    {
        return std::nullopt;  // LCOV_EXCL_LINE
    }

    return serializeEcPair(pair);
}

TER
verifyRevealedAmount(
    uint64_t const amount,
    Slice const& blindingFactor,
    ConfidentialRecipient const& holder,
    ConfidentialRecipient const& issuer,
    std::optional<ConfidentialRecipient> const& auditor)
{
    if (blindingFactor.size() != kEcBlindingFactorLength ||
        holder.publicKey.size() != kEcPubKeyLength ||
        holder.encryptedAmount.size() != kEcGamalEncryptedTotalLength ||
        issuer.publicKey.size() != kEcPubKeyLength ||
        issuer.encryptedAmount.size() != kEcGamalEncryptedTotalLength)
    {
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    auto const holderP = toParticipant(holder);
    auto const issuerP = toParticipant(issuer);
    mpt_confidential_participant auditorP{};
    mpt_confidential_participant const* auditorPtr = nullptr;
    if (auditor)
    {
        if (auditor->publicKey.size() != kEcPubKeyLength ||
            auditor->encryptedAmount.size() != kEcGamalEncryptedTotalLength)
        {
            return tecINTERNAL;  // LCOV_EXCL_LINE
        }
        auditorP = toParticipant(*auditor);
        auditorPtr = &auditorP;
    }

    if (mpt_verify_revealed_amount(amount, blindingFactor.data(), &holderP, &issuerP, auditorPtr) !=
        0)
    {
        return tecBAD_PROOF;
    }

    return tesSUCCESS;
}

NotTEC
checkEncryptedAmountFormat(STObject const& object)
{
    // Current usage of this function is only for ConfidentialMPTConvert and
    // ConfidentialMPTConvertBack transactions, which already enforce that these fields
    // are present.
    if (!object.isFieldPresent(sfHolderEncryptedAmount) ||
        !object.isFieldPresent(sfIssuerEncryptedAmount))
    {
        return temMALFORMED;  // LCOV_EXCL_LINE
    }

    if (object[sfHolderEncryptedAmount].length() != kEcGamalEncryptedTotalLength ||
        object[sfIssuerEncryptedAmount].length() != kEcGamalEncryptedTotalLength)
    {
        return temBAD_CIPHERTEXT;
    }

    bool const hasAuditor = object.isFieldPresent(sfAuditorEncryptedAmount);
    if (hasAuditor && object[sfAuditorEncryptedAmount].length() != kEcGamalEncryptedTotalLength)
        return temBAD_CIPHERTEXT;

    if (!isValidCiphertext(object[sfHolderEncryptedAmount]) ||
        !isValidCiphertext(object[sfIssuerEncryptedAmount]))
    {
        return temBAD_CIPHERTEXT;
    }

    if (hasAuditor && !isValidCiphertext(object[sfAuditorEncryptedAmount]))
        return temBAD_CIPHERTEXT;

    return tesSUCCESS;
}

TER
verifySchnorrProof(Slice const& pubKeySlice, Slice const& proofSlice, uint256 const& contextHash)
{
    if (proofSlice.size() != kEcSchnorrProofLength || pubKeySlice.size() != kEcPubKeyLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (mpt_verify_convert_proof(proofSlice.data(), pubKeySlice.data(), contextHash.data()) != 0)
        return tecBAD_PROOF;

    return tesSUCCESS;
}

TER
verifyClawbackProof(
    uint64_t const amount,
    Slice const& proof,
    Slice const& pubKeySlice,
    Slice const& ciphertext,
    uint256 const& contextHash)
{
    if (ciphertext.size() != kEcGamalEncryptedTotalLength ||
        pubKeySlice.size() != kEcPubKeyLength || proof.size() != kEcClawbackProofLength)
    {
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    if (mpt_verify_clawback_proof(
            proof.data(), amount, pubKeySlice.data(), ciphertext.data(), contextHash.data()) != 0)
    {
        return tecBAD_PROOF;
    }

    return tesSUCCESS;
}

TER
verifySendProof(
    Slice const& proof,
    ConfidentialRecipient const& sender,
    ConfidentialRecipient const& destination,
    ConfidentialRecipient const& issuer,
    std::optional<ConfidentialRecipient> const& auditor,
    Slice const& spendingBalance,
    Slice const& amountCommitment,
    Slice const& balanceCommitment,
    uint256 const& contextHash)
{
    auto const recipientCount = getConfidentialRecipientCount(auditor.has_value());
    if (proof.size() != kEcSendProofLength || sender.publicKey.size() != kEcPubKeyLength ||
        sender.encryptedAmount.size() != kEcGamalEncryptedTotalLength ||
        destination.publicKey.size() != kEcPubKeyLength ||
        destination.encryptedAmount.size() != kEcGamalEncryptedTotalLength ||
        issuer.publicKey.size() != kEcPubKeyLength ||
        issuer.encryptedAmount.size() != kEcGamalEncryptedTotalLength ||
        spendingBalance.size() != kEcGamalEncryptedTotalLength ||
        amountCommitment.size() != kEcPedersenCommitmentLength ||
        balanceCommitment.size() != kEcPedersenCommitmentLength)
    {
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    std::vector<mpt_confidential_participant> participants;
    participants.reserve(recipientCount);
    participants.push_back(toParticipant(sender));
    participants.push_back(toParticipant(destination));
    participants.push_back(toParticipant(issuer));
    if (auditor)
    {
        if (auditor->publicKey.size() != kEcPubKeyLength ||
            auditor->encryptedAmount.size() != kEcGamalEncryptedTotalLength)
        {
            return tecINTERNAL;  // LCOV_EXCL_LINE
        }
        participants.push_back(toParticipant(*auditor));
    }
    if (participants.size() != recipientCount)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (mpt_verify_send_proof(
            proof.data(),
            participants.data(),
            recipientCount,
            spendingBalance.data(),
            amountCommitment.data(),
            balanceCommitment.data(),
            contextHash.data()) != 0)
    {
        return tecBAD_PROOF;
    }

    return tesSUCCESS;
}

TER
verifyConvertBackProof(
    Slice const& proof,
    Slice const& pubKeySlice,
    Slice const& spendingBalance,
    Slice const& balanceCommitment,
    uint64_t amount,
    uint256 const& contextHash)
{
    if (proof.size() != kEcConvertBackProofLength || pubKeySlice.size() != kEcPubKeyLength ||
        spendingBalance.size() != kEcGamalEncryptedTotalLength ||
        balanceCommitment.size() != kEcPedersenCommitmentLength)
    {
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    if (mpt_verify_convert_back_proof(
            proof.data(),
            pubKeySlice.data(),
            spendingBalance.data(),
            balanceCommitment.data(),
            amount,
            contextHash.data()) != 0)
    {
        return tecBAD_PROOF;
    }

    return tesSUCCESS;
}

}  // namespace xrpl
