#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/TER.h>

#include <openssl/rand.h>
#include <openssl/sha.h>

namespace ripple {
void
addCommonZKPFields(
    Serializer& s,
    std::uint16_t txType,
    uint192 const& issuanceID,
    std::uint64_t amount)
{
    s.add16(txType);
    s.addBitString(issuanceID);
    s.add64(amount);
}

uint256
getContextHash(
    uint192 const& issuanceID,
    std::uint64_t amount,
    AccountID const& holder,
    TxType const& txType)
{
    Serializer s;
    addCommonZKPFields(s, txType, issuanceID, amount);

    s.addBitString(holder);

    return s.getSHA512Half();
}

int
secp256k1_elgamal_generate_keypair(
    secp256k1_context const* ctx,
    unsigned char* privkey,
    secp256k1_pubkey* pubkey)
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
    if (secp256k1_ec_pubkey_serialize(
            ctx, c2_bytes, &len, c2, SECP256K1_EC_COMPRESSED) != 1)
        return 0;
    len = sizeof(s_bytes);
    if (secp256k1_ec_pubkey_serialize(
            ctx, s_bytes, &len, &S, SECP256K1_EC_COMPRESSED) != 1)
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
    if (secp256k1_ec_pubkey_serialize(
            ctx, m_bytes, &len, &M, SECP256K1_EC_COMPRESSED) != 1)
        return 0;

    // 5. Brute-force search loop
    if (secp256k1_ec_pubkey_create(ctx, &G_point, one_scalar) != 1)
        return 0;
    current_M = G_point;

    for (i = 1; i <= 1000000; ++i)
    {
        len = sizeof(current_m_bytes);
        if (secp256k1_ec_pubkey_serialize(
                ctx,
                current_m_bytes,
                &len,
                &current_M,
                SECP256K1_EC_COMPRESSED) != 1)
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
    if (secp256k1_ec_pubkey_negate(ctx, &neg_b_c1) != 1 ||
        secp256k1_ec_pubkey_negate(ctx, &neg_b_c2) != 1)
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
    build_hash_input(
        hash_input, sizeof(hash_input), account_id, mpt_issuance_id);

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
generate_random_scalar(
    secp256k1_context const* ctx,
    unsigned char* scalar_bytes)
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
compute_amount_point(
    secp256k1_context const* ctx,
    secp256k1_pubkey* mG,
    uint64_t amount)
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
    secp256k1_context* ser_ctx =
        secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    memcpy(hash_input + offset, domain_sep, strlen(domain_sep));
    offset += strlen(domain_sep);

    len = 33;
    secp256k1_ec_pubkey_serialize(
        ser_ctx, hash_input + offset, &len, c1, SECP256K1_EC_COMPRESSED);
    offset += len;
    len = 33;
    secp256k1_ec_pubkey_serialize(
        ser_ctx, hash_input + offset, &len, c2, SECP256K1_EC_COMPRESSED);
    offset += len;
    len = 33;
    secp256k1_ec_pubkey_serialize(
        ser_ctx, hash_input + offset, &len, pk, SECP256K1_EC_COMPRESSED);
    offset += len;
    len = 33;
    secp256k1_ec_pubkey_serialize(
        ser_ctx, hash_input + offset, &len, mG, SECP256K1_EC_COMPRESSED);
    offset += len;
    len = 33;
    secp256k1_ec_pubkey_serialize(
        ser_ctx, hash_input + offset, &len, T1, SECP256K1_EC_COMPRESSED);
    offset += len;
    len = 33;
    secp256k1_ec_pubkey_serialize(
        ser_ctx, hash_input + offset, &len, T2, SECP256K1_EC_COMPRESSED);
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
    secp256k1_context* ser_ctx =
        secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    memcpy(hash_input + offset, domain_sep, strlen(domain_sep));
    offset += strlen(domain_sep);

    len = 33;
    secp256k1_ec_pubkey_serialize(
        ser_ctx, hash_input + offset, &len, c1, SECP256K1_EC_COMPRESSED);
    offset += len;
    len = 33;
    secp256k1_ec_pubkey_serialize(
        ser_ctx, hash_input + offset, &len, c2, SECP256K1_EC_COMPRESSED);
    offset += len;
    len = 33;
    secp256k1_ec_pubkey_serialize(
        ser_ctx, hash_input + offset, &len, pk, SECP256K1_EC_COMPRESSED);
    offset += len;
    len = 33;
    secp256k1_ec_pubkey_serialize(
        ser_ctx, hash_input + offset, &len, T1, SECP256K1_EC_COMPRESSED);
    offset += len;
    len = 33;
    secp256k1_ec_pubkey_serialize(
        ser_ctx, hash_input + offset, &len, T2, SECP256K1_EC_COMPRESSED);
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
        build_challenge_hash_input_zero(
            hash_input, c1, c2, pk_recipient, &T1, &T2, tx_context_id);
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
        build_challenge_hash_input_nonzero(
            hash_input, c1, c2, pk_recipient, &mG, &T1, &T2, tx_context_id);
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
    secp256k1_ec_pubkey_serialize(
        ctx, proof, &len, &T1, SECP256K1_EC_COMPRESSED);
    len = 33;
    secp256k1_ec_pubkey_serialize(
        ctx, proof + 33, &len, &T2, SECP256K1_EC_COMPRESSED);
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
        build_challenge_hash_input_zero(
            hash_input, c1, c2, pk_recipient, &T1, &T2, tx_context_id);
        SHA256(hash_input, sizeof(hash_input), e_scalar);
    }
    else
    {
        secp256k1_pubkey mG;
        unsigned char hash_input[253];
        if (!compute_amount_point(ctx, &mG, amount))
            return 0;
        build_challenge_hash_input_nonzero(
            hash_input, c1, c2, pk_recipient, &mG, &T1, &T2, tx_context_id);
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
    secp256k1_ec_pubkey_serialize(
        ctx, lhs_bytes, &len, &lhs_eq1, SECP256K1_EC_COMPRESSED);
    len = 33;
    secp256k1_ec_pubkey_serialize(
        ctx, rhs_bytes, &len, &rhs_eq1, SECP256K1_EC_COMPRESSED);
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
        if (!secp256k1_ec_pubkey_combine(
                ctx, &rhs_eq2_term2_base, points_to_add, 2))
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
    secp256k1_ec_pubkey_serialize(
        ctx, lhs_bytes, &len, &lhs_eq2, SECP256K1_EC_COMPRESSED);
    len = 33;
    secp256k1_ec_pubkey_serialize(
        ctx, rhs_bytes, &len, &rhs_eq2, SECP256K1_EC_COMPRESSED);
    if (memcmp(lhs_bytes, rhs_bytes, 33) != 0)
        return 0;  // Eq 2 failed

    return 1; /* Both equations passed */
}

bool
makeEcPair(Slice const& buffer, secp256k1_pubkey& out1, secp256k1_pubkey& out2)
{
    auto parsePubKey = [](Slice const& slice, secp256k1_pubkey& out) {
        return secp256k1_ec_pubkey_parse(
            secp256k1Context(),
            &out,
            reinterpret_cast<unsigned char const*>(slice.data()),
            slice.length());
    };

    Slice s1{buffer.data(), ecGamalEncryptedLength};
    Slice s2{buffer.data() + ecGamalEncryptedLength, ecGamalEncryptedLength};

    int const ret1 = parsePubKey(s1, out1);
    int const ret2 = parsePubKey(s2, out2);

    return ret1 == 1 && ret2 == 1;
}

bool
serializeEcPair(
    secp256k1_pubkey const& in1,
    secp256k1_pubkey const& in2,
    Buffer& buffer)
{
    auto serializePubKey = [](secp256k1_pubkey const& pub, unsigned char* out) {
        size_t outLen = ecGamalEncryptedLength;  // 33 bytes
        int const ret = secp256k1_ec_pubkey_serialize(
            secp256k1Context(), out, &outLen, &pub, SECP256K1_EC_COMPRESSED);
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
    if (a.length() != ecGamalEncryptedTotalLength ||
        b.length() != ecGamalEncryptedTotalLength)
        return tecINTERNAL;

    secp256k1_pubkey aC1;
    secp256k1_pubkey aC2;
    secp256k1_pubkey bC1;
    secp256k1_pubkey bC2;

    if (!makeEcPair(a, aC1, aC2) || !makeEcPair(b, bC1, bC2))
        return tecINTERNAL;

    secp256k1_pubkey sumC1;
    secp256k1_pubkey sumC2;

    if (secp256k1_elgamal_add(
            secp256k1Context(), &sumC1, &sumC2, &aC1, &aC2, &bC1, &bC2) != 1)
        return tecINTERNAL;

    if (!serializeEcPair(sumC1, sumC2, out))
        return tecINTERNAL;

    return tesSUCCESS;
}

TER
homomorphicSubtract(Slice const& a, Slice const& b, Buffer& out)
{
    if (a.length() != ecGamalEncryptedTotalLength ||
        b.length() != ecGamalEncryptedTotalLength)
        return tecINTERNAL;

    secp256k1_pubkey aC1;
    secp256k1_pubkey aC2;
    secp256k1_pubkey bC1;
    secp256k1_pubkey bC2;

    if (!makeEcPair(a, aC1, aC2) || !makeEcPair(b, bC1, bC2))
        return tecINTERNAL;

    secp256k1_pubkey diffC1;
    secp256k1_pubkey diffC2;

    if (secp256k1_elgamal_subtract(
            secp256k1Context(), &diffC1, &diffC2, &aC1, &aC2, &bC1, &bC2) != 1)
        return tecINTERNAL;

    if (!serializeEcPair(diffC1, diffC2, out))
        return tecINTERNAL;

    return tesSUCCESS;
}

TER
proveEquality(
    Slice const& proof,
    Slice const& encAmt,  // encrypted amount
    Slice const& pubkey,
    uint64_t const amount,
    uint256 const& txHash,  // Transaction context data
    std::uint32_t const spendVersion)
{
    if (proof.length() != ecEqualityProofLength)
        return tecINTERNAL;

    secp256k1_pubkey c1;
    secp256k1_pubkey c2;

    if (!makeEcPair(encAmt, c1, c2))
        return tecINTERNAL;

    // todo: might need to change how its hashed
    Serializer s;
    s.addRaw(txHash.data(), txHash.bytes);
    s.add32(spendVersion);
    // auto const txContextId = s.getSHA512Half();

    // todo: support equality
    // if (secp256k1_equality_verify(
    //         secp256k1Context(),
    //         reinterpret_cast<unsigned char const*>(proof.data()),
    //         proof.length(),  // Length of the proof byte array (98 bytes)
    //         &c1,
    //         &c2,
    //         reinterpret_cast<unsigned char const*>(pubkey.data()),
    //         amount,
    //         txContextId.data(),  // Transaction context data
    //         txContextId.bytes    // Length of context data
    //         ) != 1)
    //     return tecBAD_PROOF;

    return tesSUCCESS;
}

Buffer
encryptAmount(uint64_t amt, Slice const& pubKeySlice)
{
    Buffer buf(ecGamalEncryptedTotalLength);

    // Allocate ciphertext placeholders
    secp256k1_pubkey c1, c2;

    // todo: might need to be updated using another RNG
    // Prepare a random blinding factor
    unsigned char blindingFactor[32];
    if (RAND_bytes(blindingFactor, 32) != 1)
        Throw<std::runtime_error>("Failed to generate random number");

    secp256k1_pubkey pubKey;

    std::memcpy(pubKey.data, pubKeySlice.data(), ecPubKeyLength);

    // Encrypt the amount
    if (!secp256k1_elgamal_encrypt(
            secp256k1Context(), &c1, &c2, &pubKey, amt, blindingFactor))
        Throw<std::runtime_error>("Failed to encrypt amount");

    // Serialize the ciphertext pair into the buffer
    if (!serializeEcPair(c1, c2, buf))
        Throw<std::runtime_error>(
            "Failed to serialize into 66 byte compressed format");

    return buf;
}

Buffer
encryptCanonicalZeroAmount(
    Slice const& pubKeySlice,
    AccountID const& account,
    MPTID const& mptId)
{
    Buffer buf(ecGamalEncryptedTotalLength);

    // Allocate ciphertext placeholders
    secp256k1_pubkey c1, c2;
    secp256k1_pubkey pubKey;

    std::memcpy(pubKey.data, pubKeySlice.data(), ecPubKeyLength);

    // Encrypt the amount
    if (!generate_canonical_encrypted_zero(
            secp256k1Context(),
            &c1,
            &c2,
            &pubKey,
            account.data(),
            mptId.data()))
        Throw<std::runtime_error>("Failed to encrypt amount");

    // Serialize the ciphertext pair into the buffer
    if (!serializeEcPair(c1, c2, buf))
        Throw<std::runtime_error>(
            "Failed to serialize into 66 byte compressed format");

    return buf;
}

TER
verifyConfidentialSendProof(
    Slice const& proof,
    Slice const& encSenderBalance,
    Slice const& encSenderAmt,
    Slice const& encDestAmt,
    Slice const& encIssuerAmt,
    Slice const& senderPubKey,
    Slice const& destPubKey,
    Slice const& issuerPubKey,
    std::uint32_t const version,
    uint256 const& txHash)
{
    // if (proof.length() != ecConfidentialSendProofLength)
    //     return tecINTERNAL;

    secp256k1_pubkey balC1, balC2;
    if (!makeEcPair(encSenderBalance, balC1, balC2))
        return tecINTERNAL;

    secp256k1_pubkey senderC1, senderC2;
    if (!makeEcPair(encSenderAmt, senderC1, senderC2))
        return tecINTERNAL;

    secp256k1_pubkey destC1, destC2;
    if (!makeEcPair(encDestAmt, destC1, destC2))
        return tecINTERNAL;

    secp256k1_pubkey issuerC1, issuerC2;
    if (!makeEcPair(encIssuerAmt, issuerC1, issuerC2))
        return tecINTERNAL;

    Serializer s;
    s.addRaw(txHash.data(), txHash.bytes);
    s.add32(version);
    // auto const txContextId = s.getSHA512Half();

    // todo: equality and range proof verification
    // if (secp256k1_equal_range_verify(
    //         secp256k1Context(),
    //         reinterpret_cast<unsigned char const*>(proof.data()),
    //         proof.length(),
    //         txContextId.data(),
    //         &balC1,
    //         &balC2,
    //         &senderC1,
    //         &senderC2,
    //         reinterpret_cast<unsigned char const*>(senderPubKey.data()),
    //         &destC1,
    //         &destC2,
    //         reinterpret_cast<unsigned char const*>(destPubKey.data()),
    //         &issuerC1,
    //         &issuerC2,
    //         reinterpret_cast<unsigned char const*>(issuerPubKey.data()),
    //         txContextId.data(),
    //         txContextId.bytes) != 1)
    //     return tecBAD_PROOF;

    return tesSUCCESS;
}

TER
verifyEqualityProof(
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
            secp256k1Context(),
            proof.data(),
            &pubKey,
            &c2,
            &c1,
            amount,
            contextHash.data()) != 1)
    {
        return tecBAD_PROOF;
    }

    return tesSUCCESS;
}
}  // namespace ripple
