#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Protocol.h>

#include <openssl/rand.h>
#include <openssl/sha.h>

namespace xrpl {
void
addCommonZKPFields(
    Serializer& s,
    std::uint16_t txType,
    AccountID const& account,
    std::uint32_t sequence,
    uint192 const& issuanceID)
{
    s.add16(txType);
    s.addBitString(account);
    s.add32(sequence);
    s.addBitString(issuanceID);
}

uint256
getSendContextHash(
    AccountID const& account,
    std::uint32_t sequence,
    uint192 const& issuanceID,
    AccountID const& destination,
    std::uint32_t version)
{
    Serializer s;
    addCommonZKPFields(s, ttCONFIDENTIAL_MPT_SEND, account, sequence, issuanceID);

    s.addBitString(destination);
    s.addInteger(version);

    return s.getSHA512Half();
}

uint256
getClawbackContextHash(
    AccountID const& account,
    std::uint32_t sequence,
    uint192 const& issuanceID,
    std::uint64_t amount,
    AccountID const& holder)
{
    Serializer s;
    addCommonZKPFields(s, ttCONFIDENTIAL_MPT_CLAWBACK, account, sequence, issuanceID);

    s.add64(amount);
    s.addBitString(holder);

    return s.getSHA512Half();
}

uint256
getConvertContextHash(AccountID const& account, std::uint32_t sequence, uint192 const& issuanceID, std::uint64_t amount)
{
    Serializer s;
    addCommonZKPFields(s, ttCONFIDENTIAL_MPT_CONVERT, account, sequence, issuanceID);

    s.add64(amount);

    return s.getSHA512Half();
}

uint256
getConvertBackContextHash(
    AccountID const& account,
    std::uint32_t sequence,
    uint192 const& issuanceID,
    std::uint64_t amount,
    std::uint32_t version)
{
    Serializer s;
    addCommonZKPFields(s, ttCONFIDENTIAL_MPT_CONVERT_BACK, account, sequence, issuanceID);

    s.add64(amount);
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
    // Local/temporary variables to pass to makeEcPair.
    // Their contents will be discarded when the function returns.
    secp256k1_pubkey key1;
    secp256k1_pubkey key2;

    // Call makeEcPair and return its result.
    return makeEcPair(buffer, key1, key2);
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
    Buffer buf(ecGamalEncryptedTotalLength);

    // Allocate ciphertext placeholders
    secp256k1_pubkey c1, c2;
    secp256k1_pubkey pubKey;

    if (blindingFactor.size() != ecBlindingFactorLength)
        return std::nullopt;

    std::memcpy(pubKey.data, pubKeySlice.data(), ecPubKeyLength);

    // Encrypt the amount
    if (!secp256k1_elgamal_encrypt(secp256k1Context(), &c1, &c2, &pubKey, amt, blindingFactor.data()))
        return std::nullopt;

    // Serialize the ciphertext pair into the buffer
    if (!serializeEcPair(c1, c2, buf))
        return std::nullopt;

    return buf;
}

std::optional<Buffer>
encryptCanonicalZeroAmount(Slice const& pubKeySlice, AccountID const& account, MPTID const& mptId)
{
    if (pubKeySlice.size() != ecPubKeyLength)
        return std::nullopt;  // LCOV_EXCL_LINE

    secp256k1_pubkey c1, c2;
    secp256k1_pubkey pubKey;

    std::memcpy(pubKey.data, pubKeySlice.data(), ecPubKeyLength);

    // Encrypt the amount
    if (!generate_canonical_encrypted_zero(secp256k1Context(), &c1, &c2, &pubKey, account.data(), mptId.data()))
        return std::nullopt;

    Buffer buf(ecGamalEncryptedTotalLength);

    // Serialize the ciphertext pair into the buffer
    if (!serializeEcPair(c1, c2, buf))
        return std::nullopt;

    return buf;
}

TER
verifySchnorrProof(Slice const& pubKeySlice, Slice const& proofSlice, uint256 const& contextHash)
{
    // sanity check proof length
    if (proofSlice.size() != ecSchnorrProofLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // sanity check public key length
    if (pubKeySlice.size() != ecPubKeyLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    secp256k1_pubkey pubKey;
    std::memcpy(pubKey.data, pubKeySlice.data(), ecPubKeyLength);

    int result = secp256k1_mpt_pok_sk_verify(secp256k1Context(), proofSlice.data(), &pubKey, contextHash.data());

    if (result != 1)
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
    // sanity check blinding factor length
    if (blindingFactor.size() != ecBlindingFactorLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // sanity check public key length
    if (pubKeySlice.size() != ecPubKeyLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    secp256k1_pubkey pubKey;
    std::memcpy(pubKey.data, pubKeySlice.data(), ecPubKeyLength);

    secp256k1_pubkey c1, c2;
    if (!makeEcPair(ciphertext, c1, c2))
        return tecINTERNAL;  // LCOV_EXCL_LINE

    int result =
        secp256k1_elgamal_verify_encryption(secp256k1Context(), &c1, &c2, &pubKey, amount, blindingFactor.data());

    if (result != 1)
    {
        return tecBAD_PROOF;
    }

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

        std::memcpy(pk[i].data, recipient.publicKey.data(), ecPubKeyLength);
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

    secp256k1_pubkey pubKey;
    std::memcpy(pubKey.data, pubKeySlice.data(), ecPubKeyLength);

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

    secp256k1_pubkey pubKey;
    if (pubKeySlice.size() != ecPubKeyLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    secp256k1_pubkey pcm;
    if (pcmSlice.size() != ecPedersenCommitmentLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    std::memcpy(pubKey.data, pubKeySlice.data(), ecPubKeyLength);
    std::memcpy(pcm.data, pcmSlice.data(), ecPedersenCommitmentLength);

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

    secp256k1_pubkey pubKey;
    if (pubKeySlice.size() != ecPubKeyLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    secp256k1_pubkey pcm;
    if (pcmSlice.size() != ecPedersenCommitmentLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    std::memcpy(pubKey.data, pubKeySlice.data(), ecPubKeyLength);
    std::memcpy(pcm.data, pcmSlice.data(), ecPubKeyLength);

    if (secp256k1_elgamal_pedersen_link_verify(
            secp256k1Context(), proof.data(), &pubKey, &c2, &c1, &pcm, contextHash.data()) != 1)
    {
        return tecBAD_PROOF;
    }

    return tesSUCCESS;
}

// The following functions belong to the mpt-crypto library,
// they will be finally removed and we will use conan2 to manage the dependency.
int
secp256k1_elgamal_generate_keypair(secp256k1_context const* ctx, unsigned char* privkey, secp256k1_pubkey* pubkey)
{
    // 1. Generate 32 random bytes for the private key
    do
    {
        if (RAND_bytes(privkey, 32) != 1)
        {
            return 0;  // Failure
        }
        // 2. Verify the random data is a valid private key.
    } while (secp256k1_ec_seckey_verify(ctx, privkey) != 1);

    // 3. Create the corresponding public key.
    if (secp256k1_ec_pubkey_create(ctx, pubkey, privkey) != 1)
    {
        return 0;  // Failure
    }

    return 1;  // Success
}

// ... implementation of secp256k1_elgamal_encrypt ...
int
secp256k1_elgamal_encrypt(
    secp256k1_context const* ctx,
    secp256k1_pubkey* c1,
    secp256k1_pubkey* c2,
    secp256k1_pubkey const* pubkey_Q,
    uint64_t amount,
    unsigned char const* blinding_factor)
{
    secp256k1_pubkey S;

    // First, calculate C1 = k * G
    if (secp256k1_ec_pubkey_create(ctx, c1, blinding_factor) != 1)
    {
        return 0;
    }

    // Next, calculate the shared secret S = k * Q
    S = *pubkey_Q;
    if (secp256k1_ec_pubkey_tweak_mul(ctx, &S, blinding_factor) != 1)
    {
        return 0;
    }

    // --- Handle the amount ---
    if (amount == 0)
    {
        // For amount = 0, C2 = S.
        *c2 = S;
    }
    else
    {
        // For non-zero amounts, proceed as before.
        unsigned char amount_scalar[32] = {0};
        secp256k1_pubkey M;
        secp256k1_pubkey const* points_to_add[2];

        // Convert amount to a 32-byte BIG-ENDIAN scalar.
        for (int i = 0; i < 8; ++i)
        {
            amount_scalar[31 - i] = (amount >> (i * 8)) & 0xFF;
        }

        // Calculate M = amount * G
        if (secp256k1_ec_pubkey_create(ctx, &M, amount_scalar) != 1)
        {
            return 0;
        }

        // Calculate C2 = M + S
        points_to_add[0] = &M;
        points_to_add[1] = &S;
        if (secp256k1_ec_pubkey_combine(ctx, c2, points_to_add, 2) != 1)
        {
            return 0;
        }
    }

    return 1;  // Success
}

// ... implementation of secp256k1_elgamal_decrypt ...
int
secp256k1_elgamal_decrypt(
    secp256k1_context const* ctx,
    uint64_t* amount,
    secp256k1_pubkey const* c1,
    secp256k1_pubkey const* c2,
    unsigned char const* privkey)
{
    secp256k1_pubkey S, M, G_point, current_M, next_M;
    secp256k1_pubkey const* points_to_add[2];
    unsigned char c2_bytes[33], s_bytes[33], m_bytes[33], current_m_bytes[33];
    size_t len;
    uint64_t i;

    /* Create the scalar '1' in big-endian format */
    unsigned char one_scalar[32] = {0};
    one_scalar[31] = 1;

    /* --- Executable Code --- */

    // 1. Calculate S = privkey * C1
    S = *c1;
    if (secp256k1_ec_pubkey_tweak_mul(ctx, &S, privkey) != 1)
    {
        return 0;
    }

    // 2. Check for amount = 0 by comparing serialized points
    len = sizeof(c2_bytes);
    if (secp256k1_ec_pubkey_serialize(ctx, c2_bytes, &len, c2, SECP256K1_EC_COMPRESSED) != 1)
        return 0;
    len = sizeof(s_bytes);
    if (secp256k1_ec_pubkey_serialize(ctx, s_bytes, &len, &S, SECP256K1_EC_COMPRESSED) != 1)
        return 0;
    if (memcmp(c2_bytes, s_bytes, sizeof(c2_bytes)) == 0)
    {
        *amount = 0;
        return 1;
    }

    // 3. Recover M = C2 - S
    if (secp256k1_ec_pubkey_negate(ctx, &S) != 1)
        return 0;
    points_to_add[0] = c2;
    points_to_add[1] = &S;
    if (secp256k1_ec_pubkey_combine(ctx, &M, points_to_add, 2) != 1)
    {
        return 0;
    }

    // 4. Serialize M once for comparison in the loop
    len = sizeof(m_bytes);
    if (secp256k1_ec_pubkey_serialize(ctx, m_bytes, &len, &M, SECP256K1_EC_COMPRESSED) != 1)
        return 0;

    // 5. Brute-force search loop
    if (secp256k1_ec_pubkey_create(ctx, &G_point, one_scalar) != 1)
        return 0;
    current_M = G_point;

    for (i = 1; i <= 1000000; ++i)
    {
        len = sizeof(current_m_bytes);
        if (secp256k1_ec_pubkey_serialize(ctx, current_m_bytes, &len, &current_M, SECP256K1_EC_COMPRESSED) != 1)
            return 0;
        if (memcmp(m_bytes, current_m_bytes, sizeof(m_bytes)) == 0)
        {
            *amount = i;
            return 1;
        }

        points_to_add[0] = &current_M;
        points_to_add[1] = &G_point;
        if (secp256k1_ec_pubkey_combine(ctx, &next_M, points_to_add, 2) != 1)
            return 0;
        current_M = next_M;
    }

    return 0;  // Not found
}

int
secp256k1_elgamal_add(
    secp256k1_context const* ctx,
    secp256k1_pubkey* sum_c1,
    secp256k1_pubkey* sum_c2,
    secp256k1_pubkey const* a_c1,
    secp256k1_pubkey const* a_c2,
    secp256k1_pubkey const* b_c1,
    secp256k1_pubkey const* b_c2)
{
    secp256k1_pubkey const* c1_points[2] = {a_c1, b_c1};
    if (secp256k1_ec_pubkey_combine(ctx, sum_c1, c1_points, 2) != 1)
    {
        return 0;
    }

    secp256k1_pubkey const* c2_points[2] = {a_c2, b_c2};
    if (secp256k1_ec_pubkey_combine(ctx, sum_c2, c2_points, 2) != 1)
    {
        return 0;
    }
    return 1;
}

int
secp256k1_elgamal_subtract(
    secp256k1_context const* ctx,
    secp256k1_pubkey* diff_c1,
    secp256k1_pubkey* diff_c2,
    secp256k1_pubkey const* a_c1,
    secp256k1_pubkey const* a_c2,
    secp256k1_pubkey const* b_c1,
    secp256k1_pubkey const* b_c2)
{
    // To subtract, we add the negation: (A - B) is (A + (-B))
    // Make a local, modifiable copy of B's points.
    secp256k1_pubkey neg_b_c1 = *b_c1;
    secp256k1_pubkey neg_b_c2 = *b_c2;

    // Negate the copies
    if (secp256k1_ec_pubkey_negate(ctx, &neg_b_c1) != 1 || secp256k1_ec_pubkey_negate(ctx, &neg_b_c2) != 1)
    {
        return 0;  // Negation failed
    }

    // Now, add A and the negated copies of B
    secp256k1_pubkey const* c1_points[2] = {a_c1, &neg_b_c1};
    if (secp256k1_ec_pubkey_combine(ctx, diff_c1, c1_points, 2) != 1)
    {
        return 0;
    }

    secp256k1_pubkey const* c2_points[2] = {a_c2, &neg_b_c2};
    if (secp256k1_ec_pubkey_combine(ctx, diff_c2, c2_points, 2) != 1)
    {
        return 0;
    }

    return 1;  // Success
}

// Helper function to concatenate data for hashing
static void
build_hash_input(
    unsigned char* output_buffer,
    size_t buffer_size,
    unsigned char const* account_id,      // 20 bytes
    unsigned char const* mpt_issuance_id  // 24 bytes
)
{
    char const* domain_separator = "EncZero";
    size_t domain_len = strlen(domain_separator);
    size_t offset = 0;

    // Ensure buffer is large enough (should be checked by caller if necessary)
    // Size = strlen("EncZero") + 20 + 24 = 7 + 20 + 24 = 51 bytes

    memcpy(output_buffer + offset, domain_separator, domain_len);
    offset += domain_len;

    memcpy(output_buffer + offset, account_id, 20);
    offset += 20;

    memcpy(output_buffer + offset, mpt_issuance_id, 24);
    // offset += 24; // Final size is offset + 24
}

// The canonical encrypted zero
int
generate_canonical_encrypted_zero(
    secp256k1_context const* ctx,
    secp256k1_pubkey* enc_zero_c1,
    secp256k1_pubkey* enc_zero_c2,
    secp256k1_pubkey const* pubkey,
    unsigned char const* account_id,      // 20 bytes
    unsigned char const* mpt_issuance_id  // 24 bytes
)
{
    unsigned char deterministic_scalar[32];
    unsigned char hash_input[51];  // Size calculated above

    /* 1. Create the input buffer for hashing */
    build_hash_input(hash_input, sizeof(hash_input), account_id, mpt_issuance_id);

    /* 2. Hash the buffer to create the deterministic scalar 'r' */
    do
    {
        // Hash the concatenated bytes
        SHA256(hash_input, sizeof(hash_input), deterministic_scalar);

        /* Note: If the hash output could be invalid (0 or >= n),
         * you might need to add a nonce/counter to hash_input
         * and re-hash in a loop until a valid scalar is produced. */
    } while (secp256k1_ec_seckey_verify(ctx, deterministic_scalar) != 1);

    /* 3. Encrypt the amount 0 using the deterministic scalar */
    return secp256k1_elgamal_encrypt(
        ctx,
        enc_zero_c1,
        enc_zero_c2,
        pubkey,
        0, /* The amount is zero */
        deterministic_scalar);
}

int
generate_random_scalar(secp256k1_context const* ctx, unsigned char* scalar_bytes)
{
    do
    {
        if (RAND_bytes(scalar_bytes, 32) != 1)
        {
            return 0;  // Randomness failure
        }
    } while (secp256k1_ec_seckey_verify(ctx, scalar_bytes) != 1);
    return 1;
}

int
compute_amount_point(secp256k1_context const* ctx, secp256k1_pubkey* mG, uint64_t amount)
{
    unsigned char amount_scalar[32] = {0};
    /* This function assumes amount != 0 */
    assert(amount != 0);

    /* Convert amount to 32-byte BIG-ENDIAN scalar */
    for (int i = 0; i < 8; ++i)
    {
        amount_scalar[31 - i] = (amount >> (i * 8)) & 0xFF;
    }
    return secp256k1_ec_pubkey_create(ctx, mG, amount_scalar);
}

void
build_challenge_hash_input_nonzero(
    unsigned char hash_input[253],
    secp256k1_pubkey const* c1,
    secp256k1_pubkey const* c2,
    secp256k1_pubkey const* pk,
    secp256k1_pubkey const* mG,
    secp256k1_pubkey const* T1,
    secp256k1_pubkey const* T2,
    unsigned char const* tx_context_id)
{
    char const* domain_sep = "MPT_POK_PLAINTEXT_PROOF";  // 23 bytes
    size_t offset = 0;
    size_t len;
    secp256k1_context* ser_ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    memcpy(hash_input + offset, domain_sep, strlen(domain_sep));
    offset += strlen(domain_sep);

    len = 33;
    secp256k1_ec_pubkey_serialize(ser_ctx, hash_input + offset, &len, c1, SECP256K1_EC_COMPRESSED);
    offset += len;
    len = 33;
    secp256k1_ec_pubkey_serialize(ser_ctx, hash_input + offset, &len, c2, SECP256K1_EC_COMPRESSED);
    offset += len;
    len = 33;
    secp256k1_ec_pubkey_serialize(ser_ctx, hash_input + offset, &len, pk, SECP256K1_EC_COMPRESSED);
    offset += len;
    len = 33;
    secp256k1_ec_pubkey_serialize(ser_ctx, hash_input + offset, &len, mG, SECP256K1_EC_COMPRESSED);
    offset += len;
    len = 33;
    secp256k1_ec_pubkey_serialize(ser_ctx, hash_input + offset, &len, T1, SECP256K1_EC_COMPRESSED);
    offset += len;
    len = 33;
    secp256k1_ec_pubkey_serialize(ser_ctx, hash_input + offset, &len, T2, SECP256K1_EC_COMPRESSED);
    offset += len;

    memcpy(hash_input + offset, tx_context_id, 32);
    offset += 32;

    assert(offset == 253);
    secp256k1_context_destroy(ser_ctx);
}

void
build_challenge_hash_input_zero(
    unsigned char hash_input[220],
    secp256k1_pubkey const* c1,
    secp256k1_pubkey const* c2,
    secp256k1_pubkey const* pk,
    secp256k1_pubkey const* T1,
    secp256k1_pubkey const* T2,
    unsigned char const* tx_context_id)
{
    char const* domain_sep = "MPT_POK_PLAINTEXT_PROOF";  // 23 bytes
    size_t offset = 0;
    size_t len;
    secp256k1_context* ser_ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    memcpy(hash_input + offset, domain_sep, strlen(domain_sep));
    offset += strlen(domain_sep);

    len = 33;
    secp256k1_ec_pubkey_serialize(ser_ctx, hash_input + offset, &len, c1, SECP256K1_EC_COMPRESSED);
    offset += len;
    len = 33;
    secp256k1_ec_pubkey_serialize(ser_ctx, hash_input + offset, &len, c2, SECP256K1_EC_COMPRESSED);
    offset += len;
    len = 33;
    secp256k1_ec_pubkey_serialize(ser_ctx, hash_input + offset, &len, pk, SECP256K1_EC_COMPRESSED);
    offset += len;
    len = 33;
    secp256k1_ec_pubkey_serialize(ser_ctx, hash_input + offset, &len, T1, SECP256K1_EC_COMPRESSED);
    offset += len;
    len = 33;
    secp256k1_ec_pubkey_serialize(ser_ctx, hash_input + offset, &len, T2, SECP256K1_EC_COMPRESSED);
    offset += len;

    memcpy(hash_input + offset, tx_context_id, 32);
    offset += 32;

    assert(offset == 220);
    secp256k1_context_destroy(ser_ctx);
}

int
secp256k1_equality_plaintext_prove(
    secp256k1_context const* ctx,
    unsigned char* proof,
    secp256k1_pubkey const* c1,
    secp256k1_pubkey const* c2,
    secp256k1_pubkey const* pk_recipient,
    uint64_t amount,
    unsigned char const* randomness_r,
    unsigned char const* tx_context_id)
{
    /* C90 Declarations */
    unsigned char t_scalar[32];
    unsigned char e_scalar[32];
    unsigned char s_scalar[32];
    unsigned char er_scalar[32];
    secp256k1_pubkey T1, T2;
    size_t len;

    /* Executable Code */

    /* 1. Generate random scalar t */
    if (!generate_random_scalar(ctx, t_scalar))
        return 0;

    /* 2. Compute commitments T1 = t*G, T2 = t*Pk */
    if (!secp256k1_ec_pubkey_create(ctx, &T1, t_scalar))
    {
        memset(t_scalar, 0, 32);
        return 0;
    }
    T2 = *pk_recipient;
    if (!secp256k1_ec_pubkey_tweak_mul(ctx, &T2, t_scalar))
    {
        memset(t_scalar, 0, 32);
        return 0;
    }

    /* 3. Compute challenge e = H(...) */
    if (amount == 0)
    {
        unsigned char hash_input[220];
        build_challenge_hash_input_zero(hash_input, c1, c2, pk_recipient, &T1, &T2, tx_context_id);
        SHA256(hash_input, sizeof(hash_input), e_scalar);
    }
    else
    {
        secp256k1_pubkey mG;
        unsigned char hash_input[253];
        if (!compute_amount_point(ctx, &mG, amount))
        {
            memset(t_scalar, 0, 32);
            return 0;
        }
        build_challenge_hash_input_nonzero(hash_input, c1, c2, pk_recipient, &mG, &T1, &T2, tx_context_id);
        SHA256(hash_input, sizeof(hash_input), e_scalar);
    }

    /* Ensure e is a valid scalar */
    if (!secp256k1_ec_seckey_verify(ctx, e_scalar))
    {
        memset(t_scalar, 0, 32);
        return 0;
    }

    /* 4. Compute s = (t + e*r) mod q */
    memcpy(er_scalar, randomness_r, 32);
    if (!secp256k1_ec_seckey_tweak_mul(ctx, er_scalar, e_scalar))
    {
        memset(t_scalar, 0, 32);
        return 0;
    }
    memcpy(s_scalar, t_scalar, 32);
    if (!secp256k1_ec_seckey_tweak_add(ctx, s_scalar, er_scalar))
    {
        memset(t_scalar, 0, 32);
        return 0;
    }

    /* 5. Format the proof = T1(33) || T2(33) || s(32) */
    len = 33;
    secp256k1_ec_pubkey_serialize(ctx, proof, &len, &T1, SECP256K1_EC_COMPRESSED);
    len = 33;
    secp256k1_ec_pubkey_serialize(ctx, proof + 33, &len, &T2, SECP256K1_EC_COMPRESSED);
    memcpy(proof + 66, s_scalar, 32);

    /* 6. Clear secret data */
    memset(t_scalar, 0, 32);
    memset(s_scalar, 0, 32);
    memset(er_scalar, 0, 32);

    return 1;
}

int
secp256k1_equality_plaintext_verify(
    secp256k1_context const* ctx,
    unsigned char const* proof,
    secp256k1_pubkey const* c1,
    secp256k1_pubkey const* c2,
    secp256k1_pubkey const* pk_recipient,
    uint64_t amount,
    unsigned char const* tx_context_id)
{
    /* C90 Declarations */
    secp256k1_pubkey T1, T2;
    unsigned char s_scalar[32];
    unsigned char e_scalar[32];
    secp256k1_pubkey lhs_eq1, rhs_eq1_term2, rhs_eq1;
    secp256k1_pubkey lhs_eq2, rhs_eq2, rhs_eq2_term2_base;
    secp256k1_pubkey const* points_to_add[2];
    unsigned char lhs_bytes[33], rhs_bytes[33];
    size_t len;

    /* Executable Code */

    /* 1. Deserialize proof into T1 (33), T2 (33), s_scalar (32) */
    if (secp256k1_ec_pubkey_parse(ctx, &T1, proof, 33) != 1)
        return 0;
    if (secp256k1_ec_pubkey_parse(ctx, &T2, proof + 33, 33) != 1)
        return 0;
    memcpy(s_scalar, proof + 66, 32);
    if (!secp256k1_ec_seckey_verify(ctx, s_scalar))
        return 0; /* s cannot be 0 */

    /* 2. Recompute challenge e' = H(...) */
    if (amount == 0)
    {
        unsigned char hash_input[220];
        build_challenge_hash_input_zero(hash_input, c1, c2, pk_recipient, &T1, &T2, tx_context_id);
        SHA256(hash_input, sizeof(hash_input), e_scalar);
    }
    else
    {
        secp256k1_pubkey mG;
        unsigned char hash_input[253];
        if (!compute_amount_point(ctx, &mG, amount))
            return 0;
        build_challenge_hash_input_nonzero(hash_input, c1, c2, pk_recipient, &mG, &T1, &T2, tx_context_id);
        SHA256(hash_input, sizeof(hash_input), e_scalar);
    }
    if (!secp256k1_ec_seckey_verify(ctx, e_scalar))
        return 0; /* e cannot be 0 */

    /* 3. Check Equation 1: s*G == T1 + e'*C1 */
    if (!secp256k1_ec_pubkey_create(ctx, &lhs_eq1, s_scalar))
        return 0;
    rhs_eq1_term2 = *c1;
    if (!secp256k1_ec_pubkey_tweak_mul(ctx, &rhs_eq1_term2, e_scalar))
        return 0;
    points_to_add[0] = &T1;
    points_to_add[1] = &rhs_eq1_term2;
    if (!secp256k1_ec_pubkey_combine(ctx, &rhs_eq1, points_to_add, 2))
        return 0;

    len = 33;
    secp256k1_ec_pubkey_serialize(ctx, lhs_bytes, &len, &lhs_eq1, SECP256K1_EC_COMPRESSED);
    len = 33;
    secp256k1_ec_pubkey_serialize(ctx, rhs_bytes, &len, &rhs_eq1, SECP256K1_EC_COMPRESSED);
    if (memcmp(lhs_bytes, rhs_bytes, 33) != 0)
        return 0;  // Eq 1 failed

    /* 4. Check Equation 2: s*Pk == T2 + e'*Y */
    /* 4a. LHS = s*Pk */
    lhs_eq2 = *pk_recipient;
    if (!secp256k1_ec_pubkey_tweak_mul(ctx, &lhs_eq2, s_scalar))
        return 0;

    /* 4b. Define Y (the base for the second part of the proof) */
    if (amount == 0)
    {
        rhs_eq2_term2_base = *c2;  // Y = C2
    }
    else
    {
        secp256k1_pubkey mG;
        compute_amount_point(ctx, &mG, amount);
        if (!secp256k1_ec_pubkey_negate(ctx, &mG))
            return 0;
        points_to_add[0] = c2;
        points_to_add[1] = &mG;
        if (!secp256k1_ec_pubkey_combine(ctx, &rhs_eq2_term2_base, points_to_add, 2))
            return 0;  // Y = C2 - mG
    }

    /* 4c. RHS term = e'*Y */
    if (!secp256k1_ec_pubkey_tweak_mul(ctx, &rhs_eq2_term2_base, e_scalar))
        return 0;
    /* 4d. RHS = T2 + (e'*Y) */
    points_to_add[0] = &T2;
    points_to_add[1] = &rhs_eq2_term2_base;
    if (!secp256k1_ec_pubkey_combine(ctx, &rhs_eq2, points_to_add, 2))
        return 0;

    /* 4e. Compare LHS == RHS */
    len = 33;
    secp256k1_ec_pubkey_serialize(ctx, lhs_bytes, &len, &lhs_eq2, SECP256K1_EC_COMPRESSED);
    len = 33;
    secp256k1_ec_pubkey_serialize(ctx, rhs_bytes, &len, &rhs_eq2, SECP256K1_EC_COMPRESSED);
    if (memcmp(lhs_bytes, rhs_bytes, 33) != 0)
        return 0;  // Eq 2 failed

    return 1; /* Both equations passed */
}

void
build_pok_challenge(
    unsigned char* e,
    secp256k1_context const* ctx,
    secp256k1_pubkey const* pk,
    secp256k1_pubkey const* T,
    unsigned char const* context_id)
{
    SHA256_CTX sha;
    unsigned char buf[33];
    size_t len = 33;

    SHA256_Init(&sha);
    // Domain Separator from LaTeX spec
    SHA256_Update(&sha, "MPT_POK_SK_REGISTER", 19);

    secp256k1_ec_pubkey_serialize(ctx, buf, &len, pk, SECP256K1_EC_COMPRESSED);
    SHA256_Update(&sha, buf, 33);

    len = 33;
    secp256k1_ec_pubkey_serialize(ctx, buf, &len, T, SECP256K1_EC_COMPRESSED);
    SHA256_Update(&sha, buf, 33);

    SHA256_Update(&sha, context_id, 32);
    SHA256_Final(e, &sha);
}

int
secp256k1_mpt_pok_sk_prove(
    secp256k1_context const* ctx,
    unsigned char* proof,
    secp256k1_pubkey const* pk,
    unsigned char const* sk,
    unsigned char const* context_id)
{
    unsigned char k[32], e[32], s[32];
    secp256k1_pubkey T;

    // 1. Sample k and T = kG
    do
    {
        if (RAND_bytes(k, 32) != 1)
            return 0;
    } while (!secp256k1_ec_seckey_verify(ctx, k));

    if (!secp256k1_ec_pubkey_create(ctx, &T, k))
        return 0;

    // 2. Challenge e
    build_pok_challenge(e, ctx, pk, &T, context_id);

    // 3. Response s = k + e*sk (mod n)
    memcpy(s, sk, 32);
    if (!secp256k1_ec_seckey_tweak_mul(ctx, s, e))
        return 0;
    if (!secp256k1_ec_seckey_tweak_add(ctx, s, k))
        return 0;

    // 4. Serialize Proof: T (33 bytes) || s (32 bytes)
    size_t clen = 33;
    secp256k1_ec_pubkey_serialize(ctx, proof, &clen, &T, SECP256K1_EC_COMPRESSED);
    memcpy(proof + 33, s, 32);

    return 1;
}

int
secp256k1_mpt_pok_sk_verify(
    secp256k1_context const* ctx,
    unsigned char const* proof,
    secp256k1_pubkey const* pk,
    unsigned char const* context_id)
{
    secp256k1_pubkey T, lhs, rhs, ePk;
    unsigned char e[32], s[32];

    // 1. Parse T and s
    if (!secp256k1_ec_pubkey_parse(ctx, &T, proof, 33))
        return 0;
    memcpy(s, proof + 33, 32);

    // 2. Challenge e
    build_pok_challenge(e, ctx, pk, &T, context_id);

    // 3. Verify sG = T + ePk
    // LHS = s*G
    if (!secp256k1_ec_pubkey_create(ctx, &lhs, s))
        return 0;

    // RHS = T + e*Pk
    ePk = *pk;
    if (!secp256k1_ec_pubkey_tweak_mul(ctx, &ePk, e))
        return 0;

    secp256k1_pubkey const* addends[2] = {&T, &ePk};
    if (!secp256k1_ec_pubkey_combine(ctx, &rhs, addends, 2))
        return 0;

    // 4. Compare serialized points
    unsigned char ser_lhs[33], ser_rhs[33];
    size_t clen = 33;
    secp256k1_ec_pubkey_serialize(ctx, ser_lhs, &clen, &lhs, SECP256K1_EC_COMPRESSED);
    clen = 33;
    secp256k1_ec_pubkey_serialize(ctx, ser_rhs, &clen, &rhs, SECP256K1_EC_COMPRESSED);

    return memcmp(ser_lhs, ser_rhs, 33) == 0;
}

int
secp256k1_elgamal_verify_encryption(
    secp256k1_context const* ctx,
    secp256k1_pubkey const* c1,
    secp256k1_pubkey const* c2,
    secp256k1_pubkey const* pubkey_Q,
    uint64_t amount,
    unsigned char const* blinding_factor)
{
    secp256k1_pubkey expected_c1, mG, s_shared, expected_c2;
    unsigned char amount_scalar[32] = {0};
    unsigned char ser1[33], ser2[33];
    size_t len = 33;

    if (secp256k1_ec_pubkey_create(ctx, &expected_c1, blinding_factor) != 1)
    {
        return 0;
    }

    secp256k1_ec_pubkey_serialize(ctx, ser1, &len, c1, SECP256K1_EC_COMPRESSED);
    len = 33;
    secp256k1_ec_pubkey_serialize(ctx, ser2, &len, &expected_c1, SECP256K1_EC_COMPRESSED);
    if (memcmp(ser1, ser2, 33) != 0)
        return 0;

    // Calculate Shared Secret S = k * Q
    s_shared = *pubkey_Q;
    if (secp256k1_ec_pubkey_tweak_mul(ctx, &s_shared, blinding_factor) != 1)
        return 0;

    // Compare C2
    if (amount == 0)
    {
        expected_c2 = s_shared;
    }
    else
    {
        for (int i = 0; i < 8; ++i)
        {
            amount_scalar[31 - i] = (amount >> (i * 8)) & 0xFF;
        }
        if (secp256k1_ec_pubkey_create(ctx, &mG, amount_scalar) != 1)
            return 0;

        // Combine M + S
        secp256k1_pubkey const* pts[2] = {&mG, &s_shared};
        if (secp256k1_ec_pubkey_combine(ctx, &expected_c2, pts, 2) != 1)
            return 0;
    }

    len = 33;
    secp256k1_ec_pubkey_serialize(ctx, ser1, &len, c2, SECP256K1_EC_COMPRESSED);
    len = 33;
    secp256k1_ec_pubkey_serialize(ctx, ser2, &len, &expected_c2, SECP256K1_EC_COMPRESSED);
    if (memcmp(ser1, ser2, 33) != 0)
        return 0;

    return 1;  // Success: Encryption is valid
}

void
get_h_generator(secp256k1_context const* ctx, secp256k1_pubkey* h)
{
    unsigned char h_scalar[32] = {0};
    h_scalar[31] = 0x03;
    if (!secp256k1_ec_pubkey_create(ctx, h, h_scalar))
    {
        fprintf(stderr, "ABORT: secp256k1_ec_pubkey_create failed\n");
    }
}

void
build_link_challenge_hash(
    secp256k1_context const* ctx,
    unsigned char hash_input[290],
    secp256k1_pubkey const* c1,
    secp256k1_pubkey const* c2,
    secp256k1_pubkey const* pk,
    secp256k1_pubkey const* pcm,
    secp256k1_pubkey const* T1,
    secp256k1_pubkey const* T2,
    secp256k1_pubkey const* T3,
    unsigned char const* context_id)
{
    char const* domain_sep = "MPT_ELGAMAL_PEDERSEN_LINK";
    size_t offset = 0, len;
    memset(hash_input, 0, 290);
    memcpy(hash_input + offset, domain_sep, 25);
    offset += 27;

    secp256k1_pubkey const* points[] = {c1, c2, pk, pcm, T1, T2, T3};
    for (int i = 0; i < 7; i++)
    {
        len = 33;
        secp256k1_ec_pubkey_serialize(ctx, hash_input + offset, &len, points[i], SECP256K1_EC_COMPRESSED);
        offset += 33;
    }
    memcpy(hash_input + offset, context_id, 32);
}

int
secp256k1_elgamal_pedersen_link_prove(
    secp256k1_context const* ctx,
    unsigned char* proof,
    secp256k1_pubkey const* c1,
    secp256k1_pubkey const* c2,
    secp256k1_pubkey const* pk,
    secp256k1_pubkey const* pcm,
    uint64_t amount,
    unsigned char const* r,
    unsigned char const* rho,
    unsigned char const* context_id)
{
    unsigned char km[32], kr[32], krho[32], e[32], sm[32], sr[32], srho[32], m_sc[32] = {0};
    secp256k1_pubkey T1, T2, T3, H, mG, rPk, rhoH;
    size_t len = 33;

    if (!generate_random_scalar(ctx, km) || !generate_random_scalar(ctx, kr) || !generate_random_scalar(ctx, krho))
        return 0;
    if (!secp256k1_ec_pubkey_create(ctx, &T1, kr))
        return 0;
    if (!secp256k1_ec_pubkey_create(ctx, &mG, km))
        return 0;
    rPk = *pk;
    if (!secp256k1_ec_pubkey_tweak_mul(ctx, &rPk, kr))
        return 0;
    secp256k1_pubkey const* add_t2[2] = {&mG, &rPk};
    if (!secp256k1_ec_pubkey_combine(ctx, &T2, add_t2, 2))
        return 0;
    get_h_generator(ctx, &H);
    rhoH = H;
    if (!secp256k1_ec_pubkey_tweak_mul(ctx, &rhoH, krho))
        return 0;
    secp256k1_pubkey const* add_t3[2] = {&mG, &rhoH};
    if (!secp256k1_ec_pubkey_combine(ctx, &T3, add_t3, 2))
        return 0;

    unsigned char hash_input[290];
    build_link_challenge_hash(ctx, hash_input, c1, c2, pk, pcm, &T1, &T2, &T3, context_id);
    SHA256(hash_input, 290, e);

    for (int i = 0; i < 8; i++)
        m_sc[31 - i] = (amount >> (i * 8)) & 0xFF;

    memcpy(sm, m_sc, 32);
    if (!secp256k1_ec_seckey_tweak_mul(ctx, sm, e))
        return 0;
    if (!secp256k1_ec_seckey_tweak_add(ctx, sm, km))
        return 0;
    memcpy(sr, r, 32);
    if (!secp256k1_ec_seckey_tweak_mul(ctx, sr, e))
        return 0;
    if (!secp256k1_ec_seckey_tweak_add(ctx, sr, kr))
        return 0;
    memcpy(srho, rho, 32);
    if (!secp256k1_ec_seckey_tweak_mul(ctx, srho, e))
        return 0;
    if (!secp256k1_ec_seckey_tweak_add(ctx, srho, krho))
        return 0;

    len = 33;
    secp256k1_ec_pubkey_serialize(ctx, proof, &len, &T1, SECP256K1_EC_COMPRESSED);
    len = 33;
    secp256k1_ec_pubkey_serialize(ctx, proof + 33, &len, &T2, SECP256K1_EC_COMPRESSED);
    len = 33;
    secp256k1_ec_pubkey_serialize(ctx, proof + 66, &len, &T3, SECP256K1_EC_COMPRESSED);
    memcpy(proof + 99, sm, 32);
    memcpy(proof + 131, sr, 32);
    memcpy(proof + 163, srho, 32);
    return 1;
}

/* --- Verifier Implementation --- */

int
secp256k1_elgamal_pedersen_link_verify(
    secp256k1_context const* ctx,
    unsigned char const* proof,
    secp256k1_pubkey const* c1,
    secp256k1_pubkey const* c2,
    secp256k1_pubkey const* pk,
    secp256k1_pubkey const* pcm,
    unsigned char const* context_id)
{
    secp256k1_pubkey T1_p, T2_p, T3_p;
    secp256k1_pubkey lhs, rhs, H, mG, term2;
    unsigned char sm[32], sr[32], srho[32], e[32], e_neg[32];
    unsigned char hash_input[290];

    if (!secp256k1_ec_pubkey_parse(ctx, &T1_p, proof, 33))
        return 0;
    if (!secp256k1_ec_pubkey_parse(ctx, &T2_p, proof + 33, 33))
        return 0;
    if (!secp256k1_ec_pubkey_parse(ctx, &T3_p, proof + 66, 33))
        return 0;

    memcpy(sm, proof + 99, 32);
    memcpy(sr, proof + 131, 32);
    memcpy(srho, proof + 163, 32);

    if (secp256k1_ec_seckey_verify(ctx, sm) != 1)
        return 0;
    if (secp256k1_ec_seckey_verify(ctx, sr) != 1)
        return 0;
    if (secp256k1_ec_seckey_verify(ctx, srho) != 1)
        return 0;

    build_link_challenge_hash(ctx, hash_input, c1, c2, pk, pcm, &T1_p, &T2_p, &T3_p, context_id);
    SHA256(hash_input, sizeof(hash_input), e);
    if (secp256k1_ec_seckey_verify(ctx, e) != 1)
        return 0;

    memcpy(e_neg, e, 32);
    if (!secp256k1_ec_seckey_negate(ctx, e_neg))
        return 0;

#define COMBINE2(out, A, B)                                    \
    do                                                         \
    {                                                          \
        secp256k1_pubkey _sum;                                 \
        const secp256k1_pubkey* _pts[2] = {(A), (B)};          \
        if (!secp256k1_ec_pubkey_combine(ctx, &_sum, _pts, 2)) \
            return 0;                                          \
        (out) = _sum;                                          \
    } while (0)

#define EQ_PUBKEY(A, B)                                                                 \
    do                                                                                  \
    {                                                                                   \
        unsigned char _a[33], _b[33];                                                   \
        size_t _l = 33;                                                                 \
        if (!secp256k1_ec_pubkey_serialize(ctx, _a, &_l, (A), SECP256K1_EC_COMPRESSED)) \
            return 0;                                                                   \
        _l = 33;                                                                        \
        if (!secp256k1_ec_pubkey_serialize(ctx, _b, &_l, (B), SECP256K1_EC_COMPRESSED)) \
            return 0;                                                                   \
        if (memcmp(_a, _b, 33) != 0)                                                    \
            return 0;                                                                   \
    } while (0)

    /* Eq 1 */
    if (!secp256k1_ec_pubkey_create(ctx, &lhs, sr))
        return 0;
    rhs = *c1;
    if (!secp256k1_ec_pubkey_tweak_mul(ctx, &rhs, e_neg))
        return 0;
    COMBINE2(lhs, &lhs, &rhs);
    EQ_PUBKEY(&lhs, &T1_p);

    /* Eq 2 */
    if (!secp256k1_ec_pubkey_create(ctx, &mG, sm))
        return 0;
    term2 = *pk;
    if (!secp256k1_ec_pubkey_tweak_mul(ctx, &term2, sr))
        return 0;
    COMBINE2(lhs, &mG, &term2);
    rhs = *c2;
    if (!secp256k1_ec_pubkey_tweak_mul(ctx, &rhs, e_neg))
        return 0;
    COMBINE2(lhs, &lhs, &rhs);
    EQ_PUBKEY(&lhs, &T2_p);

    /* Eq 3 */
    get_h_generator(ctx, &H);
    term2 = H;
    if (!secp256k1_ec_pubkey_tweak_mul(ctx, &term2, srho))
        return 0;
    COMBINE2(lhs, &mG, &term2);
    rhs = *pcm;
    if (!secp256k1_ec_pubkey_tweak_mul(ctx, &rhs, e_neg))
        return 0;
    COMBINE2(lhs, &lhs, &rhs);
    EQ_PUBKEY(&lhs, &T3_p);

#undef COMBINE2
#undef EQ_PUBKEY
    return 1;
}

int
secp256k1_mpt_pedersen_commit(
    secp256k1_context const* ctx,
    secp256k1_pubkey* commitment,
    uint64_t amount,
    unsigned char const* rho)
{
    secp256k1_pubkey mG, rH, H;
    unsigned char m_scalar[32] = {0};

    // 1. Calculate m * G
    for (int i = 0; i < 8; i++)
    {
        m_scalar[31 - i] = (amount >> (i * 8)) & 0xFF;
    }
    if (!secp256k1_ec_pubkey_create(ctx, &mG, m_scalar))
    {
        return 0;
    }

    // 2. Calculate rho * H
    get_h_generator(ctx, &H);
    rH = H;
    if (!secp256k1_ec_pubkey_tweak_mul(ctx, &rH, rho))
    {
        return 0;
    }

    // 3. Combine: mG + rH
    secp256k1_pubkey const* points[2] = {&mG, &rH};
    if (!secp256k1_ec_pubkey_combine(ctx, commitment, points, 2))
    {
        return 0;
    }

    return 1;
}

// Multi-proof for same plaintexts
/**
 *  Builds the challenge hash: Domain || PublicInputs || Commitments || TxID
 */
void
build_hash_input(
    unsigned char* hash_out,  // Output: 32-byte hash
    size_t n,
    secp256k1_pubkey const* R,
    secp256k1_pubkey const* S,
    secp256k1_pubkey const* Pk,
    secp256k1_pubkey const* T_m,
    secp256k1_pubkey const* T_rG,
    secp256k1_pubkey const* T_rP,
    unsigned char const* tx_id)
{
    SHA256_CTX sha_ctx;
    char const* domain = "MPT_POK_SAME_PLAINTEXT_PROOF";
    unsigned char buf[33];
    size_t len = 33;
    size_t i;
    secp256k1_context* ser_ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    SHA256_Init(&sha_ctx);
    SHA256_Update(&sha_ctx, domain, strlen(domain));

    // Public Inputs (R, S, Pk for each ciphertext)
    for (i = 0; i < n; ++i)
    {
        secp256k1_ec_pubkey_serialize(ser_ctx, buf, &len, &R[i], SECP256K1_EC_COMPRESSED);
        SHA256_Update(&sha_ctx, buf, 33);
        secp256k1_ec_pubkey_serialize(ser_ctx, buf, &len, &S[i], SECP256K1_EC_COMPRESSED);
        SHA256_Update(&sha_ctx, buf, 33);
        secp256k1_ec_pubkey_serialize(ser_ctx, buf, &len, &Pk[i], SECP256K1_EC_COMPRESSED);
        SHA256_Update(&sha_ctx, buf, 33);
    }

    // Commitments
    secp256k1_ec_pubkey_serialize(ser_ctx, buf, &len, T_m, SECP256K1_EC_COMPRESSED);
    SHA256_Update(&sha_ctx, buf, 33);

    for (i = 0; i < n; ++i)
    {
        secp256k1_ec_pubkey_serialize(ser_ctx, buf, &len, &T_rG[i], SECP256K1_EC_COMPRESSED);
        SHA256_Update(&sha_ctx, buf, 33);
        secp256k1_ec_pubkey_serialize(ser_ctx, buf, &len, &T_rP[i], SECP256K1_EC_COMPRESSED);
        SHA256_Update(&sha_ctx, buf, 33);
    }

    SHA256_Update(&sha_ctx, tx_id, 32);
    SHA256_Final(hash_out, &sha_ctx);
    secp256k1_context_destroy(ser_ctx);
}

/* --- Public API --- */

size_t
secp256k1_mpt_prove_same_plaintext_multi_size(size_t n)
{
    // (1 point T_m + 2*N points T_r) * 33 + (1 scalar s_m + N scalars s_r) * 32
    return ((1 + 2 * n) * 33) + ((1 + n) * 32);
}

int
secp256k1_mpt_prove_same_plaintext_multi(
    secp256k1_context const* ctx,
    unsigned char* proof_out,
    size_t* proof_len,
    uint64_t amount_m,
    size_t n,
    secp256k1_pubkey const* R,
    secp256k1_pubkey const* S,
    secp256k1_pubkey const* Pk,
    unsigned char const* r_array,
    unsigned char const* tx_id)
{
    size_t required_len = secp256k1_mpt_prove_same_plaintext_multi_size(n);
    if (*proof_len < required_len)
    {
        *proof_len = required_len;
        return 0;
    }
    *proof_len = required_len;

    unsigned char k_m[32];
    unsigned char k_r[n][32];
    secp256k1_pubkey T_m;
    secp256k1_pubkey T_rG[n];
    secp256k1_pubkey T_rP[n];
    unsigned char e[32];
    unsigned char s_m[32];
    unsigned char s_r[n][32];

    size_t i;
    int ok = 1;

    /* 1. Generate Randomness & Commitments */
    if (!generate_random_scalar(ctx, k_m))
        return 0;
    if (!secp256k1_ec_pubkey_create(ctx, &T_m, k_m))
        ok = 0;

    for (i = 0; i < n; ++i)
    {
        if (!generate_random_scalar(ctx, k_r[i]))
            ok = 0;
        if (!secp256k1_ec_pubkey_create(ctx, &T_rG[i], k_r[i]))
            ok = 0;

        T_rP[i] = Pk[i];
        if (!secp256k1_ec_pubkey_tweak_mul(ctx, &T_rP[i], k_r[i]))
            ok = 0;
    }

    if (!ok)
        return 0;

    /* 2. Compute Challenge e */
    build_hash_input(e, n, R, S, Pk, &T_m, T_rG, T_rP, tx_id);
    // Ensure e is valid
    if (!secp256k1_ec_seckey_verify(ctx, e))
        return 0;

    /* 3. Compute Responses */
    /* s_m = k_m + e * m */
    unsigned char m_scalar[32] = {0};
    for (i = 0; i < 8; ++i)
        m_scalar[31 - i] = (amount_m >> (i * 8)) & 0xFF;

    memcpy(s_m, k_m, 32);
    unsigned char term[32];
    memcpy(term, m_scalar, 32);
    if (!secp256k1_ec_seckey_tweak_mul(ctx, term, e))
        return 0;
    if (!secp256k1_ec_seckey_tweak_add(ctx, s_m, term))
        return 0;

    /* s_ri = k_ri + e * ri */
    for (i = 0; i < n; ++i)
    {
        memcpy(s_r[i], k_r[i], 32);
        memcpy(term, &r_array[i * 32], 32);  // Extract r_i from flat array
        if (!secp256k1_ec_seckey_tweak_mul(ctx, term, e))
            return 0;
        if (!secp256k1_ec_seckey_tweak_add(ctx, s_r[i], term))
            return 0;
    }

    /* 4. Serialize Proof */
    size_t offset = 0;
    size_t len = 33;

    // Points
    secp256k1_ec_pubkey_serialize(ctx, proof_out + offset, &len, &T_m, SECP256K1_EC_COMPRESSED);
    offset += 33;
    for (i = 0; i < n; ++i)
    {
        secp256k1_ec_pubkey_serialize(ctx, proof_out + offset, &len, &T_rG[i], SECP256K1_EC_COMPRESSED);
        offset += 33;
    }
    for (i = 0; i < n; ++i)
    {
        secp256k1_ec_pubkey_serialize(ctx, proof_out + offset, &len, &T_rP[i], SECP256K1_EC_COMPRESSED);
        offset += 33;
    }

    // Scalars
    memcpy(proof_out + offset, s_m, 32);
    offset += 32;
    for (i = 0; i < n; ++i)
    {
        memcpy(proof_out + offset, s_r[i], 32);
        offset += 32;
    }

    return 1;
}

int
secp256k1_mpt_verify_same_plaintext_multi(
    secp256k1_context const* ctx,
    unsigned char const* proof,
    size_t proof_len,
    size_t n,
    secp256k1_pubkey const* R,
    secp256k1_pubkey const* S,
    secp256k1_pubkey const* Pk,
    unsigned char const* tx_id)
{
    if (proof_len != secp256k1_mpt_prove_same_plaintext_multi_size(n))
        return 0;

    /* Deserialize */
    size_t offset = 0;
    secp256k1_pubkey T_m;
    secp256k1_pubkey T_rG[n];
    secp256k1_pubkey T_rP[n];
    unsigned char s_m[32];
    unsigned char s_r[n][32];
    size_t i;

    if (!secp256k1_ec_pubkey_parse(ctx, &T_m, proof + offset, 33))
        return 0;
    offset += 33;
    for (i = 0; i < n; ++i)
    {
        if (!secp256k1_ec_pubkey_parse(ctx, &T_rG[i], proof + offset, 33))
            return 0;
        offset += 33;
    }
    for (i = 0; i < n; ++i)
    {
        if (!secp256k1_ec_pubkey_parse(ctx, &T_rP[i], proof + offset, 33))
            return 0;
        offset += 33;
    }

    memcpy(s_m, proof + offset, 32);
    offset += 32;
    for (i = 0; i < n; ++i)
    {
        memcpy(s_r[i], proof + offset, 32);
        offset += 32;
    }

    /* Recompute Challenge */
    unsigned char e[32];
    build_hash_input(e, n, R, S, Pk, &T_m, T_rG, T_rP, tx_id);

    /* Verify Equations */
    secp256k1_pubkey lhs, rhs, term, SmG;
    secp256k1_pubkey const* add_pt[3];
    unsigned char b1[33], b2[33];
    size_t len;

    // Precompute s_m * G
    if (!secp256k1_ec_pubkey_create(ctx, &SmG, s_m))
        return 0;

    for (i = 0; i < n; ++i)
    {
        /* Check 1: s_ri * G == T_ri_G + e * R_i */
        if (!secp256k1_ec_pubkey_create(ctx, &lhs, s_r[i]))
            return 0;

        term = R[i];
        if (!secp256k1_ec_pubkey_tweak_mul(ctx, &term, e))
            return 0;
        add_pt[0] = &T_rG[i];
        add_pt[1] = &term;
        if (!secp256k1_ec_pubkey_combine(ctx, &rhs, add_pt, 2))
            return 0;

        len = 33;
        secp256k1_ec_pubkey_serialize(ctx, b1, &len, &lhs, SECP256K1_EC_COMPRESSED);
        len = 33;
        secp256k1_ec_pubkey_serialize(ctx, b2, &len, &rhs, SECP256K1_EC_COMPRESSED);
        if (memcmp(b1, b2, 33) != 0)
            return 0;

        /* Check 2: s_m * G + s_ri * P_i == T_m + T_ri_P + e * S_i */
        /* LHS = SmG + s_r[i] * Pk[i] */
        term = Pk[i];
        if (!secp256k1_ec_pubkey_tweak_mul(ctx, &term, s_r[i]))
            return 0;
        add_pt[0] = &SmG;
        add_pt[1] = &term;
        if (!secp256k1_ec_pubkey_combine(ctx, &lhs, add_pt, 2))
            return 0;

        /* RHS = T_m + T_rP[i] + e * S[i] */
        term = S[i];
        if (!secp256k1_ec_pubkey_tweak_mul(ctx, &term, e))
            return 0;
        add_pt[0] = &T_m;
        add_pt[1] = &T_rP[i];
        add_pt[2] = &term;
        if (!secp256k1_ec_pubkey_combine(ctx, &rhs, add_pt, 3))
            return 0;

        len = 33;
        secp256k1_ec_pubkey_serialize(ctx, b1, &len, &lhs, SECP256K1_EC_COMPRESSED);
        len = 33;
        secp256k1_ec_pubkey_serialize(ctx, b2, &len, &rhs, SECP256K1_EC_COMPRESSED);
        if (memcmp(b1, b2, 33) != 0)
            return 0;
    }

    return 1;
}

}  // namespace xrpl
