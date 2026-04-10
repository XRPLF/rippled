#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Protocol.h>

#include <openssl/rand.h>
#include <utility/mpt_utility.h>

#include <secp256k1_mpt.h>

namespace xrpl {

/**
 * @brief Converts an XRPL AccountID to mpt-crypto lib C struct.
 *
 * @param account The AccountID.
 * @return The equivalent mpt-crypto lib account_id struct.
 */
account_id
toAccountId(AccountID const& account)
{
    account_id res;
    std::memcpy(res.bytes, account.data(), kMPT_ACCOUNT_ID_SIZE);
    return res;
}

/**
 * @brief Converts an XRPL uint192 to mpt-crypto lib C struct.
 *
 * @param i The XRPL MPTokenIssuance ID.
 * @return The equivalent mpt-crypto lib mpt_issuance_id struct.
 */
mpt_issuance_id
toIssuanceId(uint192 const& issuance)
{
    mpt_issuance_id res;
    std::memcpy(res.bytes, issuance.data(), kMPT_ISSUANCE_ID_SIZE);
    return res;
}

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
    if (buffer.length() != 2 * ecGamalEncryptedLength)
        return std::nullopt;  // LCOV_EXCL_LINE

    auto parsePubKey = [](Slice const& slice, secp256k1_pubkey& out) {
        return secp256k1_ec_pubkey_parse(
            secp256k1Context(),
            &out,
            reinterpret_cast<unsigned char const*>(slice.data()),
            slice.length());
    };

    Slice const s1{buffer.data(), ecGamalEncryptedLength};
    Slice const s2{buffer.data() + ecGamalEncryptedLength, ecGamalEncryptedLength};

    EcPair pair{};
    if (parsePubKey(s1, pair.c1) != 1 || parsePubKey(s2, pair.c2) != 1)
        return std::nullopt;

    return pair;
}

std::optional<Buffer>
serializeEcPair(EcPair const& pair)
{
    auto serializePubKey = [](secp256k1_pubkey const& pub, unsigned char* out) {
        size_t outLen = ecGamalEncryptedLength;  // 33 bytes
        int const ret = secp256k1_ec_pubkey_serialize(
            secp256k1Context(), out, &outLen, &pub, SECP256K1_EC_COMPRESSED);
        return ret == 1 && outLen == ecGamalEncryptedLength;
    };

    Buffer buffer(ecGamalEncryptedTotalLength);
    unsigned char* ptr = buffer.data();
    bool const res1 = serializePubKey(pair.c1, ptr);
    bool const res2 = serializePubKey(pair.c2, ptr + ecGamalEncryptedLength);

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
    if (buffer.size() != compressedECPointLength)
        return false;

    // Compressed EC points must start with 0x02 or 0x03
    if (buffer[0] != ecCompressedPrefixEvenY && buffer[0] != ecCompressedPrefixOddY)
        return false;

    secp256k1_pubkey point;
    return secp256k1_ec_pubkey_parse(secp256k1Context(), &point, buffer.data(), buffer.size()) == 1;
}

std::optional<Buffer>
homomorphicAdd(Slice const& a, Slice const& b)
{
    if (a.length() != ecGamalEncryptedTotalLength || b.length() != ecGamalEncryptedTotalLength)
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
    if (a.length() != ecGamalEncryptedTotalLength || b.length() != ecGamalEncryptedTotalLength)
        return std::nullopt;

    auto const pairA = makeEcPair(a);
    auto const pairB = makeEcPair(b);

    if (!pairA || !pairB)
        return std::nullopt;

    EcPair diff{};
    if (auto res = secp256k1_elgamal_subtract(
            secp256k1Context(), &diff.c1, &diff.c2, &pairA->c1, &pairA->c2, &pairB->c1, &pairB->c2);
        res != 1)
    {
        return std::nullopt;
    }

    return serializeEcPair(diff);
}

Buffer
generateBlindingFactor()
{
    unsigned char blindingFactor[ecBlindingFactorLength];

    // todo: might need to be updated using another RNG
    if (RAND_bytes(blindingFactor, ecBlindingFactorLength) != 1)
        Throw<std::runtime_error>("Failed to generate random number");

    return Buffer(blindingFactor, ecBlindingFactorLength);
}

std::optional<Buffer>
encryptAmount(uint64_t const amt, Slice const& pubKeySlice, Slice const& blindingFactor)
{
    if (blindingFactor.size() != ecBlindingFactorLength || pubKeySlice.size() != ecPubKeyLength)
        return std::nullopt;

    Buffer out(ecGamalEncryptedTotalLength);
    if (mpt_encrypt_amount(amt, pubKeySlice.data(), blindingFactor.data(), out.data()) != 0)
        return std::nullopt;

    return out;
}

std::optional<Buffer>
encryptCanonicalZeroAmount(Slice const& pubKeySlice, AccountID const& account, MPTID const& mptId)
{
    if (pubKeySlice.size() != ecPubKeyLength)
        return std::nullopt;  // LCOV_EXCL_LINE

    EcPair pair{};
    secp256k1_pubkey pubKey;
    if (auto res = secp256k1_ec_pubkey_parse(
            secp256k1Context(), &pubKey, pubKeySlice.data(), ecPubKeyLength);
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
    if (blindingFactor.size() != ecBlindingFactorLength ||
        holder.publicKey.size() != ecPubKeyLength ||
        holder.encryptedAmount.size() != ecGamalEncryptedTotalLength ||
        issuer.publicKey.size() != ecPubKeyLength ||
        issuer.encryptedAmount.size() != ecGamalEncryptedTotalLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    secp256k1_context* ctx = mpt_secp256k1_context();
    if (!ctx)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // All calls are evaluated before combining results to prevent timing attacks.
    auto verifyCiphertext = [&](ConfidentialRecipient const& r) -> bool {
        secp256k1_pubkey pk, c1, c2;
        if (secp256k1_ec_pubkey_parse(ctx, &pk, r.publicKey.data(), r.publicKey.size()) != 1)
            return false;
        if (!mpt_make_ec_pair(r.encryptedAmount.data(), &c1, &c2))
            return false;
        return secp256k1_elgamal_verify_encryption(
                   ctx, &c1, &c2, &pk, amount, blindingFactor.data()) == 1;
    };

    bool const holderOk = verifyCiphertext(holder);
    bool const issuerOk = verifyCiphertext(issuer);
    bool valid = holderOk && issuerOk;
    if (auditor)
    {
        if (auditor->publicKey.size() != ecPubKeyLength ||
            auditor->encryptedAmount.size() != ecGamalEncryptedTotalLength)
            return tecINTERNAL;  // LCOV_EXCL_LINE
        bool const auditorOk = verifyCiphertext(*auditor);
        valid = valid && auditorOk;
    }

    return valid ? TER{tesSUCCESS} : TER{tecBAD_PROOF};
}

NotTEC
checkEncryptedAmountFormat(STObject const& object)
{
    // Current usage of this function is only for ConfidentialMPTConvert and
    // ConfidentialMPTConvertBack transactions, which already enforce that these fields
    // are present.
    if (!object.isFieldPresent(sfHolderEncryptedAmount) ||
        !object.isFieldPresent(sfIssuerEncryptedAmount))
        return temMALFORMED;  // LCOV_EXCL_LINE

    if (object[sfHolderEncryptedAmount].length() != ecGamalEncryptedTotalLength ||
        object[sfIssuerEncryptedAmount].length() != ecGamalEncryptedTotalLength)
        return temBAD_CIPHERTEXT;

    bool const hasAuditor = object.isFieldPresent(sfAuditorEncryptedAmount);
    if (hasAuditor && object[sfAuditorEncryptedAmount].length() != ecGamalEncryptedTotalLength)
        return temBAD_CIPHERTEXT;

    if (!isValidCiphertext(object[sfHolderEncryptedAmount]) ||
        !isValidCiphertext(object[sfIssuerEncryptedAmount]))
        return temBAD_CIPHERTEXT;

    if (hasAuditor && !isValidCiphertext(object[sfAuditorEncryptedAmount]))
        return temBAD_CIPHERTEXT;

    return tesSUCCESS;
}

TER
verifySchnorrProof(Slice const& pubKeySlice, Slice const& proofSlice, uint256 const& contextHash)
{
    if (proofSlice.size() != ecSchnorrProofLength || pubKeySlice.size() != ecPubKeyLength)
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
    if (ciphertext.size() != ecGamalEncryptedTotalLength || pubKeySlice.size() != ecPubKeyLength ||
        proof.size() != ecCompactClawbackProofLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (mpt_verify_clawback_proof(
            proof.data(), amount, pubKeySlice.data(), ciphertext.data(), contextHash.data()) != 0)
        return tecBAD_PROOF;

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
    if (proof.size() != ecCompactSendProofLength || sender.publicKey.size() != ecPubKeyLength ||
        sender.encryptedAmount.size() != ecGamalEncryptedTotalLength ||
        destination.publicKey.size() != ecPubKeyLength ||
        destination.encryptedAmount.size() != ecGamalEncryptedTotalLength ||
        issuer.publicKey.size() != ecPubKeyLength ||
        issuer.encryptedAmount.size() != ecGamalEncryptedTotalLength ||
        spendingBalance.size() != ecGamalEncryptedTotalLength ||
        amountCommitment.size() != ecPedersenCommitmentLength ||
        balanceCommitment.size() != ecPedersenCommitmentLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto makeParticipant = [](ConfidentialRecipient const& r) {
        mpt_confidential_participant p;
        std::memcpy(p.pubkey, r.publicKey.data(), kMPT_PUBKEY_SIZE);
        std::memcpy(p.ciphertext, r.encryptedAmount.data(), kMPT_ELGAMAL_TOTAL_SIZE);
        return p;
    };

    std::vector<mpt_confidential_participant> participants(recipientCount);
    participants[0] = makeParticipant(sender);
    participants[1] = makeParticipant(destination);
    participants[2] = makeParticipant(issuer);
    if (auditor)
    {
        if (auditor->publicKey.size() != ecPubKeyLength ||
            auditor->encryptedAmount.size() != ecGamalEncryptedTotalLength)
            return tecINTERNAL;
        participants[3] = makeParticipant(*auditor);
    }

    if (mpt_verify_send_proof(
            proof.data(),
            participants.data(),
            static_cast<uint8_t>(recipientCount),
            spendingBalance.data(),
            amountCommitment.data(),
            balanceCommitment.data(),
            contextHash.data()) != 0)
        return tecBAD_PROOF;

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
    if (proof.size() != ecCompactConvertBackProofLength || pubKeySlice.size() != ecPubKeyLength ||
        spendingBalance.size() != ecGamalEncryptedTotalLength ||
        balanceCommitment.size() != ecPedersenCommitmentLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (mpt_verify_convert_back_proof(
            proof.data(),
            pubKeySlice.data(),
            spendingBalance.data(),
            balanceCommitment.data(),
            amount,
            contextHash.data()) != 0)
        return tecBAD_PROOF;

    return tesSUCCESS;
}

}  // namespace xrpl
