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

    Slice s1{buffer.data(), ecGamalEncryptedLength};
    Slice s2{buffer.data() + ecGamalEncryptedLength, ecGamalEncryptedLength};

    EcPair pair;
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
    if (buffer[0] != 0x02 && buffer[0] != 0x03)
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

    EcPair sum;
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

    EcPair diff;
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
    if (blindingFactor.size() != ecBlindingFactorLength)
        return std::nullopt;

    if (pubKeySlice.size() != ecPubKeyLength)
        return std::nullopt;

    EcPair pair;
    secp256k1_pubkey pubKey;
    if (auto res = secp256k1_ec_pubkey_parse(
            secp256k1Context(), &pubKey, pubKeySlice.data(), ecPubKeyLength);
        res != 1)
    {
        return std::nullopt;
    }

    if (auto res = secp256k1_elgamal_encrypt(
            secp256k1Context(), &pair.c1, &pair.c2, &pubKey, amt, blindingFactor.data());
        res != 1)
    {
        return std::nullopt;
    }

    return serializeEcPair(pair);
}

std::optional<Buffer>
encryptCanonicalZeroAmount(Slice const& pubKeySlice, AccountID const& account, MPTID const& mptId)
{
    if (pubKeySlice.size() != ecPubKeyLength)
        return std::nullopt;  // LCOV_EXCL_LINE

    EcPair pair;
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
verifySchnorrProof(Slice const& pubKeySlice, Slice const& proofSlice, uint256 const& contextHash)
{
    if (proofSlice.size() != ecSchnorrProofLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (pubKeySlice.size() != ecPubKeyLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    secp256k1_pubkey pubKey;
    if (auto res = secp256k1_ec_pubkey_parse(
            secp256k1Context(), &pubKey, pubKeySlice.data(), ecPubKeyLength);
        res != 1)
    {
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    if (auto res = secp256k1_mpt_pok_sk_verify(
            secp256k1Context(), proofSlice.data(), &pubKey, contextHash.data());
        res != 1)
    {
        return tecBAD_PROOF;
    }

    return tesSUCCESS;
}

TER
verifyElGamalEncryption(
    uint64_t const amount,
    Slice const& blindingFactor,
    Slice const& pubKeySlice,
    Slice const& ciphertext)
{
    if (ciphertext.size() != ecGamalEncryptedTotalLength ||
        blindingFactor.size() != ecBlindingFactorLength || pubKeySlice.size() != ecPubKeyLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    secp256k1_pubkey pubKey;
    if (auto res = secp256k1_ec_pubkey_parse(
            secp256k1Context(), &pubKey, pubKeySlice.data(), ecPubKeyLength);
        res != 1)
    {
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    auto const pair = makeEcPair(ciphertext);
    if (!pair)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (auto res = secp256k1_elgamal_verify_encryption(
            secp256k1Context(), &pair->c1, &pair->c2, &pubKey, amount, blindingFactor.data());
        res != 1)
    {
        return tecBAD_PROOF;
    }

    return tesSUCCESS;
}

TER
verifyRevealedAmount(
    uint64_t const amount,
    Slice const& blindingFactor,
    ConfidentialRecipient const& holder,
    ConfidentialRecipient const& issuer,
    std::optional<ConfidentialRecipient> const& auditor)
{
    if (auto const res = verifyElGamalEncryption(
            amount, blindingFactor, holder.publicKey, holder.encryptedAmount);
        !isTesSuccess(res))
    {
        return res;
    }

    if (auto const res = verifyElGamalEncryption(
            amount, blindingFactor, issuer.publicKey, issuer.encryptedAmount);
        !isTesSuccess(res))
    {
        return res;
    }

    if (auditor)
    {
        if (auto const res = verifyElGamalEncryption(
                amount, blindingFactor, auditor->publicKey, auditor->encryptedAmount);
            !isTesSuccess(res))
        {
            return res;
        }
    }

    return tesSUCCESS;
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

    if (proof.size() != secp256k1_mpt_proof_equality_shared_r_size(nRecipients))
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const ctx = secp256k1Context();

    secp256k1_pubkey c1;
    std::vector<secp256k1_pubkey> c2_vec(nRecipients);
    std::vector<secp256k1_pubkey> pk_vec(nRecipients);

    for (size_t i = 0; i < nRecipients; ++i)
    {
        auto const& recipient = recipients[i];

        if (recipient.encryptedAmount.size() != ecGamalEncryptedTotalLength)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        if (recipient.publicKey.size() != ecPubKeyLength)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        // Parse Shared C1 from the first recipient only
        if (i == 0)
        {
            if (auto res = secp256k1_ec_pubkey_parse(
                    ctx, &c1, recipient.encryptedAmount.data(), ecGamalEncryptedLength);
                res != 1)
            {
                return tecINTERNAL;  // LCOV_EXCL_LINE
            }
        }
        else
        {
            // All C1 bytes must be the same
            if (std::memcmp(
                    recipient.encryptedAmount.data(),
                    recipients[0].encryptedAmount.data(),
                    ecGamalEncryptedLength) != 0)
            {
                return tecBAD_PROOF;
            }
        }

        if (auto res = secp256k1_ec_pubkey_parse(
                ctx,
                &c2_vec[i],
                recipient.encryptedAmount.data() + ecGamalEncryptedLength,
                ecGamalEncryptedLength);
            res != 1)
        {
            return tecINTERNAL;  // LCOV_EXCL_LINE
        }

        if (auto res = secp256k1_ec_pubkey_parse(
                ctx, &pk_vec[i], recipient.publicKey.data(), ecPubKeyLength);
            res != 1)
        {
            return tecINTERNAL;  // LCOV_EXCL_LINE
        }
    }

    if (auto res = secp256k1_mpt_verify_equality_shared_r(
            ctx, proof.data(), nRecipients, &c1, c2_vec.data(), pk_vec.data(), contextHash.data());
        res != 1)
    {
        return tecBAD_PROOF;
    }

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
    if (ciphertext.size() != ecGamalEncryptedTotalLength || pubKeySlice.size() != ecPubKeyLength ||
        proof.size() != ecEqualityProofLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const pair = makeEcPair(ciphertext);
    if (!pair)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    secp256k1_pubkey pubKey;
    if (auto res = secp256k1_ec_pubkey_parse(
            secp256k1Context(), &pubKey, pubKeySlice.data(), ecPubKeyLength);
        res != 1)
    {
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    // Note: c2, c1 order - the proof is generated with c2 first (the encrypted
    // message component) because the equality proof structure expects the
    // message-containing term before the blinding term.
    if (auto res = secp256k1_equality_plaintext_verify(
            secp256k1Context(),
            proof.data(),
            &pubKey,
            &pair->c2,
            &pair->c1,
            amount,
            contextHash.data());
        res != 1)
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
verifyAmountPcmLinkage(
    Slice const& proof,
    Slice const& encAmt,
    Slice const& pubKeySlice,
    Slice const& pcmSlice,
    uint256 const& contextHash)
{
    if (proof.length() != ecPedersenProofLength)
        return tecINTERNAL;

    auto const pair = makeEcPair(encAmt);
    if (!pair)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (pubKeySlice.size() != ecPubKeyLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (pcmSlice.size() != ecPedersenCommitmentLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    secp256k1_pubkey pubKey;
    if (auto res = secp256k1_ec_pubkey_parse(
            secp256k1Context(), &pubKey, pubKeySlice.data(), ecPubKeyLength);
        res != 1)
    {
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    secp256k1_pubkey pcm;
    if (auto res = secp256k1_ec_pubkey_parse(
            secp256k1Context(), &pcm, pcmSlice.data(), ecPedersenCommitmentLength);
        res != 1)
    {
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    if (auto res = secp256k1_elgamal_pedersen_link_verify(
            secp256k1Context(),
            proof.data(),
            &pair->c1,
            &pair->c2,
            &pubKey,
            &pcm,
            contextHash.data());
        res != 1)
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

    auto const pair = makeEcPair(encAmt);
    if (!pair)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (pubKeySlice.size() != ecPubKeyLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (pcmSlice.size() != ecPedersenCommitmentLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    secp256k1_pubkey pubKey;
    if (auto res = secp256k1_ec_pubkey_parse(
            secp256k1Context(), &pubKey, pubKeySlice.data(), ecPubKeyLength);
        res != 1)
    {
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    secp256k1_pubkey pcm;
    if (auto res = secp256k1_ec_pubkey_parse(
            secp256k1Context(), &pcm, pcmSlice.data(), ecPedersenCommitmentLength);
        res != 1)
    {
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    // Note: c2, c1 order - the linkage proof expects the message-containing
    // component (c2 = m*G + r*Pk) before the blinding component (c1 = r*G).
    if (auto res = secp256k1_elgamal_pedersen_link_verify(
            secp256k1Context(),
            proof.data(),
            &pubKey,
            &pair->c2,
            &pair->c1,
            &pcm,
            contextHash.data());
        res != 1)
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
    // 1. Validate input lengths
    // This function could support any power-of-2 m, but current usage only requires m=1 or m=2
    std::size_t const m = compressedCommitments.size();
    if (m != 1 && m != 2)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    std::size_t const expectedProofLen =
        (m == 1) ? ecSingleBulletproofLength : ecDoubleBulletproofLength;
    if (proof.size() != expectedProofLen)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // 2. Prepare Pedersen Commitments, parse from compressed format
    auto const ctx = secp256k1Context();
    std::vector<secp256k1_pubkey> commitments(m);
    for (size_t i = 0; i < m; ++i)
    {
        // Sanity check length
        if (compressedCommitments[i].size() != ecPedersenCommitmentLength)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        if (auto res = secp256k1_ec_pubkey_parse(
                ctx, &commitments[i], compressedCommitments[i].data(), ecPedersenCommitmentLength);
            res != 1)
        {
            return tecINTERNAL;  // LCOV_EXCL_LINE
        }
    }

    // 3. Prepare Generator Vectors (G_vec, H_vec)
    // The range proof requires vectors of size 64 * m
    std::size_t const n = 64 * m;
    std::vector<secp256k1_pubkey> G_vec(n);
    std::vector<secp256k1_pubkey> H_vec(n);

    // Retrieve deterministic generators "G" and "H"
    if (auto res =
            secp256k1_mpt_get_generator_vector(ctx, G_vec.data(), n, (unsigned char const*)"G", 1);
        res != 1)
    {
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    if (auto res =
            secp256k1_mpt_get_generator_vector(ctx, H_vec.data(), n, (unsigned char const*)"H", 1);
        res != 1)
    {
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    // 4. Prepare Base Generator (pk_base / H)
    secp256k1_pubkey pk_base;
    if (auto res = secp256k1_mpt_get_h_generator(ctx, &pk_base); res != 1)
    {
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    // 5. Verify the Proof
    if (auto res = secp256k1_bulletproof_verify_agg(
            ctx,
            G_vec.data(),
            H_vec.data(),
            reinterpret_cast<unsigned char const*>(proof.data()),
            proof.size(),
            commitments.data(),
            m,
            &pk_base,
            contextHash.data());
        res != 1)
    {
        return tecBAD_PROOF;
    }

    return tesSUCCESS;
}

TER
computeSendRemainder(Slice const& balanceCommitment, Slice const& amountCommitment, Buffer& out)
{
    if (balanceCommitment.size() != ecPedersenCommitmentLength ||
        amountCommitment.size() != ecPedersenCommitmentLength)
        return tecINTERNAL;

    auto const ctx = secp256k1Context();

    secp256k1_pubkey pcBalance;
    if (auto res = secp256k1_ec_pubkey_parse(
            ctx, &pcBalance, balanceCommitment.data(), ecPedersenCommitmentLength);
        res != 1)
    {
        return tecINTERNAL;
    }

    secp256k1_pubkey pcAmount;
    if (auto res = secp256k1_ec_pubkey_parse(
            ctx, &pcAmount, amountCommitment.data(), ecPedersenCommitmentLength);
        res != 1)
    {
        return tecINTERNAL;
    }

    // Negate PC_amount point to get -PC_amount
    if (auto res = secp256k1_ec_pubkey_negate(ctx, &pcAmount); res != 1)
    {
        return tecINTERNAL;
    }

    // Compute pcRem = pcBalance + (-pcAmount)
    secp256k1_pubkey const* summands[2] = {&pcBalance, &pcAmount};
    secp256k1_pubkey pcRem;
    if (auto res = secp256k1_ec_pubkey_combine(ctx, &pcRem, summands, 2); res != 1)
    {
        return tecINTERNAL;
    }

    // Serialize result to compressed format
    out.alloc(ecPedersenCommitmentLength);
    size_t outLen = ecPedersenCommitmentLength;
    if (auto res = secp256k1_ec_pubkey_serialize(
            ctx, out.data(), &outLen, &pcRem, SECP256K1_EC_COMPRESSED);
        res != 1)
    {
        return tecINTERNAL;
    }

    return tesSUCCESS;
}

TER
computeConvertBackRemainder(Slice const& commitment, uint64_t amount, Buffer& out)
{
    if (commitment.size() != ecPedersenCommitmentLength || amount == 0)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const ctx = secp256k1Context();

    // Parse commitment from compressed format
    secp256k1_pubkey pcBalance;
    if (auto res = secp256k1_ec_pubkey_parse(
            ctx, &pcBalance, commitment.data(), ecPedersenCommitmentLength);
        res != 1)
    {
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    // Convert amount to 32-byte big-endian scalar
    unsigned char mScalar[32] = {0};
    uint64_t amountBigEndian = boost::endian::native_to_big(amount);
    std::memcpy(&mScalar[24], &amountBigEndian, sizeof(amountBigEndian));

    // Compute mG = amount * G
    secp256k1_pubkey mG;
    if (auto res = secp256k1_ec_pubkey_create(ctx, &mG, mScalar); res != 1)
    {
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    // Negate mG to get -mG
    if (auto res = secp256k1_ec_pubkey_negate(ctx, &mG); res != 1)
    {
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    // Compute pcRem = pcBalance + (-mG)
    secp256k1_pubkey const* summands[2] = {&pcBalance, &mG};
    secp256k1_pubkey pcRem;
    if (auto res = secp256k1_ec_pubkey_combine(ctx, &pcRem, summands, 2); res != 1)
    {
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    // Serialize result to compressed format
    out.alloc(ecPedersenCommitmentLength);
    size_t outLen = ecPedersenCommitmentLength;
    if (auto res = secp256k1_ec_pubkey_serialize(
            ctx, out.data(), &outLen, &pcRem, SECP256K1_EC_COMPRESSED);
        res != 1 || outLen != ecPedersenCommitmentLength)
    {
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    return tesSUCCESS;
}
}  // namespace xrpl
