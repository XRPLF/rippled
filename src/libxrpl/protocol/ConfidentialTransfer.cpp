#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Protocol.h>

#include <boost/endian/conversion.hpp>

#include <openssl/rand.h>
#include <openssl/sha.h>

namespace xrpl {
void
addCommonZKPFields(
    Serializer& s,
    std::uint16_t txType,
    AccountID const& account,
    uint192 const& issuanceID,
    std::uint32_t sequence)
{
    // TxCommonHash = hash(TxType || Account || IssuanceID || SequenceOrTicket)
    s.add16(txType);
    s.addBitString(account);
    s.addBitString(issuanceID);
    s.add32(sequence);
}

uint256
getSendContextHash(
    AccountID const& account,
    uint192 const& issuanceID,
    std::uint32_t sequence,
    AccountID const& destination,
    std::uint32_t version)
{
    Serializer s;
    addCommonZKPFields(s, ttCONFIDENTIAL_MPT_SEND, account, issuanceID, sequence);

    // TxSpecific = identity || freshness
    s.addBitString(destination);
    s.addInteger(version);

    return s.getSHA512Half();
}

uint256
getClawbackContextHash(
    AccountID const& account,
    uint192 const& issuanceID,
    std::uint32_t sequence,
    AccountID const& holder)
{
    Serializer s;
    addCommonZKPFields(s, ttCONFIDENTIAL_MPT_CLAWBACK, account, issuanceID, sequence);

    // TxSpecific = identity || freshness
    s.addBitString(holder);
    s.addInteger(0);

    return s.getSHA512Half();
}

uint256
getConvertContextHash(AccountID const& account, uint192 const& issuanceID, std::uint32_t sequence)
{
    Serializer s;
    addCommonZKPFields(s, ttCONFIDENTIAL_MPT_CONVERT, account, issuanceID, sequence);

    // TxSpecific = identity || freshness
    s.addBitString(account);
    s.addInteger(0);

    return s.getSHA512Half();
}

uint256
getConvertBackContextHash(
    AccountID const& account,
    uint192 const& issuanceID,
    std::uint32_t sequence,
    std::uint32_t version)
{
    Serializer s;
    addCommonZKPFields(s, ttCONFIDENTIAL_MPT_CONVERT_BACK, account, issuanceID, sequence);

    // TxSpecific = identity || freshness
    s.addBitString(account);
    s.addInteger(version);

    return s.getSHA512Half();
}

bool
makeEcPair(Slice const& buffer, secp256k1_pubkey& out1, secp256k1_pubkey& out2)
{
    auto parsePubKey = [](Slice const& slice, secp256k1_pubkey& out) {
        return secp256k1_ec_pubkey_parse(
            secp256k1Context(), &out, reinterpret_cast<unsigned char const*>(slice.data()), slice.length());
    };

    Slice s1{buffer.data(), ecGamalEncryptedLength};
    Slice s2{buffer.data() + ecGamalEncryptedLength, ecGamalEncryptedLength};

    int const ret1 = parsePubKey(s1, out1);
    int const ret2 = parsePubKey(s2, out2);

    return ret1 == 1 && ret2 == 1;
}

bool
serializeEcPair(secp256k1_pubkey const& in1, secp256k1_pubkey const& in2, Buffer& buffer)
{
    auto serializePubKey = [](secp256k1_pubkey const& pub, unsigned char* out) {
        size_t outLen = ecGamalEncryptedLength;  // 33 bytes
        int const ret = secp256k1_ec_pubkey_serialize(secp256k1Context(), out, &outLen, &pub, SECP256K1_EC_COMPRESSED);
        return ret == 1 && outLen == ecGamalEncryptedLength;
    };

    unsigned char* ptr = buffer.data();
    bool const res1 = serializePubKey(in1, ptr);
    bool const res2 = serializePubKey(in2, ptr + ecGamalEncryptedLength);

    return res1 && res2;
}

bool
isValidCiphertext(Slice const& buffer)
{
    secp256k1_pubkey key1;
    secp256k1_pubkey key2;
    return makeEcPair(buffer, key1, key2);
}

bool
isValidCompressedECPoint(Slice const& buffer)
{
    if (buffer.size() != compressedECPointLength)
        return false;

    // Compressed EC points must start with 0x02 or 0x03
    if (buffer[0] != 0x02 && buffer[0] != 0x03)
        return false;

    secp256k1_pubkey point;
    return secp256k1_ec_pubkey_parse(secp256k1Context(), &point, buffer.data(), buffer.size()) == 1;
}

TER
homomorphicAdd(Slice const& a, Slice const& b, Buffer& out)
{
    if (a.length() != ecGamalEncryptedTotalLength || b.length() != ecGamalEncryptedTotalLength)
        return tecINTERNAL;

    secp256k1_pubkey aC1;
    secp256k1_pubkey aC2;
    secp256k1_pubkey bC1;
    secp256k1_pubkey bC2;

    if (!makeEcPair(a, aC1, aC2) || !makeEcPair(b, bC1, bC2))
        return tecINTERNAL;

    secp256k1_pubkey sumC1;
    secp256k1_pubkey sumC2;

    if (secp256k1_elgamal_add(secp256k1Context(), &sumC1, &sumC2, &aC1, &aC2, &bC1, &bC2) != 1)
        return tecINTERNAL;

    if (!serializeEcPair(sumC1, sumC2, out))
        return tecINTERNAL;

    return tesSUCCESS;
}

TER
homomorphicSubtract(Slice const& a, Slice const& b, Buffer& out)
{
    if (a.length() != ecGamalEncryptedTotalLength || b.length() != ecGamalEncryptedTotalLength)
        return tecINTERNAL;

    secp256k1_pubkey aC1;
    secp256k1_pubkey aC2;
    secp256k1_pubkey bC1;
    secp256k1_pubkey bC2;

    if (!makeEcPair(a, aC1, aC2) || !makeEcPair(b, bC1, bC2))
        return tecINTERNAL;

    secp256k1_pubkey diffC1;
    secp256k1_pubkey diffC2;

    if (secp256k1_elgamal_subtract(secp256k1Context(), &diffC1, &diffC2, &aC1, &aC2, &bC1, &bC2) != 1)
        return tecINTERNAL;

    if (!serializeEcPair(diffC1, diffC2, out))
        return tecINTERNAL;

    return tesSUCCESS;
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
    if (blindingFactor.size() != ecBlindingFactorLength)
        return std::nullopt;

    if (pubKeySlice.size() != ecPubKeyLength)
        return std::nullopt;

    secp256k1_pubkey c1, c2, pubKey;
    if (secp256k1_ec_pubkey_parse(secp256k1Context(), &pubKey, pubKeySlice.data(), ecPubKeyLength) != 1)
        return std::nullopt;

    if (!secp256k1_elgamal_encrypt(secp256k1Context(), &c1, &c2, &pubKey, amt, blindingFactor.data()))
        return std::nullopt;

    Buffer buf(ecGamalEncryptedTotalLength);
    if (!serializeEcPair(c1, c2, buf))
        return std::nullopt;

    return buf;
}

std::optional<Buffer>
encryptCanonicalZeroAmount(Slice const& pubKeySlice, AccountID const& account, MPTID const& mptId)
{
    if (pubKeySlice.size() != ecPubKeyLength)
        return std::nullopt;  // LCOV_EXCL_LINE

    secp256k1_pubkey c1, c2, pubKey;
    if (secp256k1_ec_pubkey_parse(secp256k1Context(), &pubKey, pubKeySlice.data(), ecPubKeyLength) != 1)
        return std::nullopt;  // LCOV_EXCL_LINE

    if (!generate_canonical_encrypted_zero(secp256k1Context(), &c1, &c2, &pubKey, account.data(), mptId.data()))
        return std::nullopt;  // LCOV_EXCL_LINE

    Buffer buf(ecGamalEncryptedTotalLength);
    if (!serializeEcPair(c1, c2, buf))
        return std::nullopt;  // LCOV_EXCL_LINE

    return buf;
}

TER
verifySchnorrProof(Slice const& pubKeySlice, Slice const& proofSlice, uint256 const& contextHash)
{
    if (proofSlice.size() != ecSchnorrProofLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (pubKeySlice.size() != ecPubKeyLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    secp256k1_pubkey pubKey;
    if (secp256k1_ec_pubkey_parse(secp256k1Context(), &pubKey, pubKeySlice.data(), ecPubKeyLength) != 1)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (secp256k1_mpt_pok_sk_verify(secp256k1Context(), proofSlice.data(), &pubKey, contextHash.data()) != 1)
        return tecBAD_PROOF;

    return tesSUCCESS;
}

TER
verifyElGamalEncryption(
    std::uint64_t const amount,
    Slice const& blindingFactor,
    Slice const& pubKeySlice,
    Slice const& ciphertext)
{
    if (blindingFactor.size() != ecBlindingFactorLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (pubKeySlice.size() != ecPubKeyLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    secp256k1_pubkey pubKey;
    if (secp256k1_ec_pubkey_parse(secp256k1Context(), &pubKey, pubKeySlice.data(), ecPubKeyLength) != 1)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    secp256k1_pubkey c1, c2;
    if (!makeEcPair(ciphertext, c1, c2))
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (secp256k1_elgamal_verify_encryption(secp256k1Context(), &c1, &c2, &pubKey, amount, blindingFactor.data()) != 1)
        return tecBAD_PROOF;

    return tesSUCCESS;
}

TER
verifyRevealedAmount(
    std::uint64_t const amount,
    Slice const& blindingFactor,
    ConfidentialRecipient const& holder,
    ConfidentialRecipient const& issuer,
    std::optional<ConfidentialRecipient> const& auditor)
{
    if (auto const res = verifyElGamalEncryption(amount, blindingFactor, holder.publicKey, holder.encryptedAmount);
        !isTesSuccess(res))
    {
        return res;
    }

    if (auto const res = verifyElGamalEncryption(amount, blindingFactor, issuer.publicKey, issuer.encryptedAmount);
        !isTesSuccess(res))
    {
        return res;
    }

    if (auditor)
    {
        if (auto const res =
                verifyElGamalEncryption(amount, blindingFactor, auditor->publicKey, auditor->encryptedAmount);
            !isTesSuccess(res))
        {
            return res;
        }
    }

    return tesSUCCESS;
}

std::size_t
getMultiCiphertextEqualityProofSize(std::size_t nRecipients)
{
    // Points (33 bytes): T_m (1) + T_rG (nRecipients) + T_rP (nRecipients) = 1
    // + 2nRecipients Scalars (32 bytes): s_m (1) + s_r (nRecipients) = 1 +
    // nRecipients
    return ((1 + (2 * nRecipients)) * 33) + ((1 + nRecipients) * 32);
}

TER
verifyMultiCiphertextEqualityProof(
    Slice const& proof,
    std::vector<ConfidentialRecipient> const& recipients,
    std::size_t const nRecipients,
    uint256 const& contextHash)
{
    if (recipients.size() != nRecipients)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (proof.size() != getMultiCiphertextEqualityProofSize(nRecipients))
        return tecINTERNAL;  // LCOV_EXCL_LINE

    std::vector<secp256k1_pubkey> r(nRecipients);
    std::vector<secp256k1_pubkey> s(nRecipients);
    std::vector<secp256k1_pubkey> pk(nRecipients);

    for (size_t i = 0; i < nRecipients; ++i)
    {
        auto const& recipient = recipients[i];
        if (recipient.encryptedAmount.size() != ecGamalEncryptedTotalLength)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        if (!makeEcPair(recipient.encryptedAmount, r[i], s[i]))
            return tecINTERNAL;  // LCOV_EXCL_LINE

        if (recipient.publicKey.size() != ecPubKeyLength)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        if (secp256k1_ec_pubkey_parse(secp256k1Context(), &pk[i], recipient.publicKey.data(), ecPubKeyLength) != 1)
            return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    int const result = secp256k1_mpt_verify_same_plaintext_multi(
        secp256k1Context(), proof.data(), proof.size(), nRecipients, r.data(), s.data(), pk.data(), contextHash.data());

    if (result != 1)
        return tecBAD_PROOF;

    return tesSUCCESS;
}

TER
verifyClawbackEqualityProof(
    uint64_t const amount,
    Slice const& proof,
    Slice const& pubKeySlice,
    Slice const& ciphertext,
    uint256 const& contextHash)
{
    secp256k1_pubkey c1, c2;
    if (!makeEcPair(ciphertext, c1, c2))
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (pubKeySlice.size() != ecPubKeyLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    secp256k1_pubkey pubKey;
    if (secp256k1_ec_pubkey_parse(secp256k1Context(), &pubKey, pubKeySlice.data(), ecPubKeyLength) != 1)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // Note: c2, c1 order - the proof is generated with c2 first (the encrypted
    // message component) because the equality proof structure expects the
    // message-containing term before the blinding term.
    if (secp256k1_equality_plaintext_verify(
            secp256k1Context(), proof.data(), &pubKey, &c2, &c1, amount, contextHash.data()) != 1)
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
    if (!object.isFieldPresent(sfHolderEncryptedAmount) || !object.isFieldPresent(sfIssuerEncryptedAmount))
        return temMALFORMED;  // LCOV_EXCL_LINE

    if (object[sfHolderEncryptedAmount].length() != ecGamalEncryptedTotalLength ||
        object[sfIssuerEncryptedAmount].length() != ecGamalEncryptedTotalLength)
        return temBAD_CIPHERTEXT;

    bool const hasAuditor = object.isFieldPresent(sfAuditorEncryptedAmount);
    if (hasAuditor && object[sfAuditorEncryptedAmount].length() != ecGamalEncryptedTotalLength)
        return temBAD_CIPHERTEXT;

    if (!isValidCiphertext(object[sfHolderEncryptedAmount]) || !isValidCiphertext(object[sfIssuerEncryptedAmount]))
        return temBAD_CIPHERTEXT;

    if (hasAuditor && !isValidCiphertext(object[sfAuditorEncryptedAmount]))
        return temBAD_CIPHERTEXT;

    return tesSUCCESS;
}

TER
verifyAmountPcmLinkage(
    Slice const& proof,
    Slice const& encAmt,
    Slice const& pubKeySlice,
    Slice const& pcmSlice,
    uint256 const& contextHash)
{
    if (proof.length() != ecPedersenProofLength)
        return tecINTERNAL;

    secp256k1_pubkey c1, c2;
    if (!makeEcPair(encAmt, c1, c2))
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (pubKeySlice.size() != ecPubKeyLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (pcmSlice.size() != ecPedersenCommitmentLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    secp256k1_pubkey pubKey;
    if (secp256k1_ec_pubkey_parse(secp256k1Context(), &pubKey, pubKeySlice.data(), ecPubKeyLength) != 1)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    secp256k1_pubkey pcm;
    if (secp256k1_ec_pubkey_parse(secp256k1Context(), &pcm, pcmSlice.data(), ecPedersenCommitmentLength) != 1)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (secp256k1_elgamal_pedersen_link_verify(
            secp256k1Context(), proof.data(), &c1, &c2, &pubKey, &pcm, contextHash.data()) != 1)
    {
        return tecBAD_PROOF;
    }

    return tesSUCCESS;
}

TER
verifyBalancePcmLinkage(
    Slice const& proof,
    Slice const& encAmt,
    Slice const& pubKeySlice,
    Slice const& pcmSlice,
    uint256 const& contextHash)
{
    if (proof.length() != ecPedersenProofLength)
        return tecINTERNAL;

    secp256k1_pubkey c1;
    secp256k1_pubkey c2;

    if (!makeEcPair(encAmt, c1, c2))
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (pubKeySlice.size() != ecPubKeyLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (pcmSlice.size() != ecPedersenCommitmentLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    secp256k1_pubkey pubKey;
    if (secp256k1_ec_pubkey_parse(secp256k1Context(), &pubKey, pubKeySlice.data(), ecPubKeyLength) != 1)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    secp256k1_pubkey pcm;
    if (secp256k1_ec_pubkey_parse(secp256k1Context(), &pcm, pcmSlice.data(), ecPedersenCommitmentLength) != 1)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // Note: c2, c1 order - the linkage proof expects the message-containing
    // component (c2 = m*G + r*Pk) before the blinding component (c1 = r*G).
    if (secp256k1_elgamal_pedersen_link_verify(
            secp256k1Context(), proof.data(), &pubKey, &c2, &c1, &pcm, contextHash.data()) != 1)
    {
        return tecBAD_PROOF;
    }

    return tesSUCCESS;
}

TER
verifyAggregatedBulletproof(
    Slice const& proof,
    std::vector<Slice> const& compressedCommitments,
    uint256 const& contextHash)
{
    // 1. Validate Aggregation Factor (m), m to be a power of 2
    std::size_t const m = compressedCommitments.size();
    if (m == 0 || (m & (m - 1)) != 0)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // 2. Prepare Pedersen Commitments, parse from compressed format
    auto const ctx = secp256k1Context();
    std::vector<secp256k1_pubkey> commitments(m);
    for (size_t i = 0; i < m; ++i)
    {
        // Sanity check length
        if (compressedCommitments[i].size() != ecPedersenCommitmentLength)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        if (secp256k1_ec_pubkey_parse(
                ctx, &commitments[i], compressedCommitments[i].data(), ecPedersenCommitmentLength) != 1)
            return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    // 3. Prepare Generator Vectors (G_vec, H_vec)
    // The range proof requires vectors of size 64 * m
    std::size_t const n = 64 * m;
    std::vector<secp256k1_pubkey> G_vec(n);
    std::vector<secp256k1_pubkey> H_vec(n);

    // Retrieve deterministic generators "G" and "H"
    if (secp256k1_mpt_get_generator_vector(ctx, G_vec.data(), n, (unsigned char const*)"G", 1) != 1)
    {
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    if (secp256k1_mpt_get_generator_vector(ctx, H_vec.data(), n, (unsigned char const*)"H", 1) != 1)
    {
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    // 4. Prepare Base Generator (pk_base / H)
    secp256k1_pubkey pk_base;
    if (secp256k1_mpt_get_h_generator(ctx, &pk_base) != 1)
    {
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    // 5. Verify the Proof
    int const result = secp256k1_bulletproof_verify_agg(
        ctx,
        G_vec.data(),
        H_vec.data(),
        reinterpret_cast<unsigned char const*>(proof.data()),
        proof.size(),
        commitments.data(),
        m,
        &pk_base,
        contextHash.data());

    if (result != 1)
        return tecBAD_PROOF;

    return tesSUCCESS;
}

TER
computeSendRemainder(Slice const& balanceCommitment, Slice const& amountCommitment, Buffer& out)
{
    if (balanceCommitment.size() != ecPedersenCommitmentLength || amountCommitment.size() != ecPedersenCommitmentLength)
        return tecINTERNAL;

    auto const ctx = secp256k1Context();

    secp256k1_pubkey pcBalance;
    if (secp256k1_ec_pubkey_parse(ctx, &pcBalance, balanceCommitment.data(), ecPedersenCommitmentLength) != 1)
        return tecINTERNAL;

    secp256k1_pubkey pcAmount;
    if (secp256k1_ec_pubkey_parse(ctx, &pcAmount, amountCommitment.data(), ecPedersenCommitmentLength) != 1)
        return tecINTERNAL;

    // Negate PC_amount point to get -PC_amount
    if (!secp256k1_ec_pubkey_negate(ctx, &pcAmount))
        return tecINTERNAL;

    // Compute pcRem = pcBalance + (-pcAmount)
    secp256k1_pubkey const* summands[2] = {&pcBalance, &pcAmount};
    secp256k1_pubkey pcRem;
    if (!secp256k1_ec_pubkey_combine(ctx, &pcRem, summands, 2))
        return tecINTERNAL;

    // Serialize result to compressed format
    out.alloc(ecPedersenCommitmentLength);
    size_t outLen = ecPedersenCommitmentLength;
    if (secp256k1_ec_pubkey_serialize(ctx, out.data(), &outLen, &pcRem, SECP256K1_EC_COMPRESSED) != 1)
        return tecINTERNAL;

    return tesSUCCESS;
}

TER
computeConvertBackRemainder(Slice const& commitment, std::uint64_t amount, Buffer& out)
{
    if (commitment.size() != ecPedersenCommitmentLength || amount == 0)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const ctx = secp256k1Context();

    // Parse commitment from compressed format
    secp256k1_pubkey pcBalance;
    if (secp256k1_ec_pubkey_parse(ctx, &pcBalance, commitment.data(), ecPedersenCommitmentLength) != 1)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // Convert amount to 32-byte big-endian scalar
    unsigned char mScalar[32] = {0};
    std::uint64_t amountBigEndian = boost::endian::native_to_big(amount);
    std::memcpy(&mScalar[24], &amountBigEndian, sizeof(amountBigEndian));

    // Compute mG = amount * G
    secp256k1_pubkey mG;
    if (!secp256k1_ec_pubkey_create(ctx, &mG, mScalar))
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // Negate mG to get -mG
    if (!secp256k1_ec_pubkey_negate(ctx, &mG))
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // Compute pcRem = pcBalance + (-mG)
    secp256k1_pubkey const* summands[2] = {&pcBalance, &mG};
    secp256k1_pubkey pcRem;
    if (!secp256k1_ec_pubkey_combine(ctx, &pcRem, summands, 2))
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // Serialize result to compressed format
    out.alloc(ecPedersenCommitmentLength);
    size_t outLen = ecPedersenCommitmentLength;
    if (secp256k1_ec_pubkey_serialize(ctx, out.data(), &outLen, &pcRem, SECP256K1_EC_COMPRESSED) != 1 ||
        outLen != ecPedersenCommitmentLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    return tesSUCCESS;
}
}  // namespace xrpl
