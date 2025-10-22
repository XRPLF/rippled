//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/TER.h>

#include <openssl/rand.h>
#include <openssl/sha.h>

namespace ripple {
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

TER
homomorphicAdd(Slice const& a, Slice const& b, Buffer& out)
{
    if (a.length() != ecGamalEncryptedTotalLength ||
        b.length() != ecGamalEncryptedTotalLength)
        return tecINTERNAL;

    secp256k1_pubkey a_c1;
    secp256k1_pubkey a_c2;
    secp256k1_pubkey b_c1;
    secp256k1_pubkey b_c2;

    if (!makeEcPair(a, a_c1, a_c2) || !makeEcPair(b, b_c1, b_c2))
        return tecINTERNAL;

    secp256k1_pubkey sum_c1;
    secp256k1_pubkey sum_c2;

    // todo:: support addition after it's supported
    if (secp256k1_elgamal_add(
            secp256k1Context(), &sum_c1, &sum_c2, &a_c1, &a_c2, &b_c1, &b_c2) !=
        1)
        return tecINTERNAL;

    if (!serializeEcPair(sum_c1, sum_c2, out))
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
    auto const txContextId = s.getSHA512Half();

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

    // Prepare a random blinding factor
    unsigned char blinding_factor[32];
    if (RAND_bytes(blinding_factor, 32) != 1)
        Throw<std::runtime_error>("Failed to generate random number");

    secp256k1_pubkey pubKey;

    std::memcpy(pubKey.data, pubKeySlice.data(), ecPubKeyLength);

    // Encrypt the amount
    if (!secp256k1_elgamal_encrypt(
            secp256k1Context(), &c1, &c2, &pubKey, amt, blinding_factor))
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

    // Prepare a random blinding factor
    unsigned char blinding_factor[32];
    if (RAND_bytes(blinding_factor, 32) != 1)
        Throw<std::runtime_error>("Failed to generate random number");

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

}  // namespace ripple
