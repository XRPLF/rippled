#ifndef XRPL_PROTOCOL_CONFIDENTIALTRANSFER_H_INCLUDED
#define XRPL_PROTOCOL_CONFIDENTIALTRANSFER_H_INCLUDED

#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/detail/secp256k1.h>

#include <secp256k1.h>

namespace ripple {

void
addCommonZKPFields(
    Serializer& s,
    std::uint16_t txType,
    AccountID const& account,
    std::uint32_t sequence,
    uint192 const& issuanceID,
    std::uint64_t amount);

uint256
getClawbackContextHash(
    AccountID const& account,
    std::uint32_t sequence,
    uint192 const& issuanceID,
    std::uint64_t amount,
    AccountID const& holder);

uint256
getConvertContextHash(
    AccountID const& account,
    std::uint32_t sequence,
    uint192 const& issuanceID,
    std::uint64_t amount);

uint256
getConvertBackContextHash(
    AccountID const& account,
    std::uint32_t sequence,
    uint192 const& issuanceID,
    std::uint64_t amount,
    std::uint32_t version);

// breaks a 66-byte encrypted amount into two 33-byte components
// then parses each 33-byte component into 64-byte secp256k1_pubkey format
bool
makeEcPair(Slice const& buffer, secp256k1_pubkey& out1, secp256k1_pubkey& out2);

// serialize two secp256k1_pubkey components back into compressed 66-byte form
bool
serializeEcPair(
    secp256k1_pubkey const& in1,
    secp256k1_pubkey const& in2,
    Buffer& buffer);

/**
 * @brief Verifies that a buffer contains two valid, parsable EC public keys.
 * @param buffer The input buffer containing two concatenated components.
 * @return true if both components can be parsed successfully, false otherwise.
 */
bool
isValidCiphertext(Slice const& buffer);

TER
homomorphicAdd(Slice const& a, Slice const& b, Buffer& out);

TER
homomorphicSubtract(Slice const& a, Slice const& b, Buffer& out);

// returns ciphertext and the blinding factor used
std::optional<Buffer>
encryptAmount(
    uint64_t const amt,
    Slice const& pubKeySlice,
    Slice const& blindingFactor);

Buffer
encryptCanonicalZeroAmount(
    Slice const& pubKeySlice,
    AccountID const& account,
    MPTID const& mptId);

TER
verifySchnorrProof(
    Slice const& pubKeySlice,
    Slice const& proofSlice,
    uint256 const& contextHash);

TER
verifyElGamalEncryption(
    std::uint64_t const amount,
    Slice const& blindingFactor,
    Slice const& pubKeySlice,
    Slice const& ciphertext);

TER
verifyClawbackEqualityProof(
    uint64_t const amount,
    Slice const& proof,
    Slice const& pubKeySlice,
    Slice const& ciphertext,
    uint256 const& contextHash);

std::vector<Buffer>
getEqualityProofs(Slice const& zkp);

NotTEC
checkEncryptedAmountFormat(STObject const& object);

// Helper struct to bundle the ElGamal Public Key and the associated Ciphertext
struct EncryptedAmountInfo
{
    Slice const publicKey;
    Slice const encryptedAmount;
};

TER
verifyRevealedAmount(
    std::uint64_t const amount,
    Slice const& blindingFactor,
    EncryptedAmountInfo const& holder,
    EncryptedAmountInfo const& issuer,
    std::optional<EncryptedAmountInfo> const& auditor);

// returns the number of entries
size_t inline getEqualityProofSize(bool const hasAuditor)
{
    // Be careful if we ever need to change the numbers below, it will be a
    // breaking change!
    return (hasAuditor ? 3 : 2);
}

// returns the total byte length of all the equality proofs combined
size_t inline getEqualityProofLength(bool const hasAuditor)
{
    return getEqualityProofSize(hasAuditor) * ecEqualityProofLength;
}

// generates a 32 byte randomness factor to be used in encryption and proofs
Buffer
generateBlindingFactor();

// The following functions belong to the mpt-crypto library,
// they will be finally removed and we will use conan2 to manage the dependency.
/**
 * @brief Generates a new secp256k1 key pair.
 */
SECP256K1_API int
secp256k1_elgamal_generate_keypair(
    secp256k1_context const* ctx,
    unsigned char* privkey,
    secp256k1_pubkey* pubkey);

/**
 * @brief Encrypts a 64-bit amount using ElGamal.
 */
SECP256K1_API int
secp256k1_elgamal_encrypt(
    secp256k1_context const* ctx,
    secp256k1_pubkey* c1,
    secp256k1_pubkey* c2,
    secp256k1_pubkey const* pubkey_Q,
    uint64_t amount,
    unsigned char const* blinding_factor);

/**
 * @brief Decrypts an ElGamal ciphertext to recover the amount.
 */
SECP256K1_API int
secp256k1_elgamal_decrypt(
    secp256k1_context const* ctx,
    uint64_t* amount,
    secp256k1_pubkey const* c1,
    secp256k1_pubkey const* c2,
    unsigned char const* privkey);

/**
 * @brief Homomorphically adds two ElGamal ciphertexts.
 */
SECP256K1_API int
secp256k1_elgamal_add(
    secp256k1_context const* ctx,
    secp256k1_pubkey* sum_c1,
    secp256k1_pubkey* sum_c2,
    secp256k1_pubkey const* a_c1,
    secp256k1_pubkey const* a_c2,
    secp256k1_pubkey const* b_c1,
    secp256k1_pubkey const* b_c2);

/**
 * @brief Homomorphically subtracts two ElGamal ciphertexts.
 */
SECP256K1_API int
secp256k1_elgamal_subtract(
    secp256k1_context const* ctx,
    secp256k1_pubkey* diff_c1,
    secp256k1_pubkey* diff_c2,
    secp256k1_pubkey const* a_c1,
    secp256k1_pubkey const* a_c2,
    secp256k1_pubkey const* b_c1,
    secp256k1_pubkey const* b_c2);

/**
 * @brief Generates the canonical encrypted zero for a given MPT token instance.
 *
 * This ciphertext represents a zero balance for a specific account's holding
 * of a token defined by its MPTokenIssuanceID.
 *
 * @param[in]   ctx             A pointer to a valid secp256k1 context.
 * @param[out]  enc_zero_c1     The C1 component of the canonical ciphertext.
 * @param[out]  enc_zero_c2     The C2 component of the canonical ciphertext.
 * @param[in]   pubkey          The ElGamal public key of the account holder.
 * @param[in]   account_id      A pointer to the 20-byte AccountID.
 * @param[in]   mpt_issuance_id A pointer to the 24-byte MPTokenIssuanceID.
 *
 * @return 1 on success, 0 on failure.
 */
SECP256K1_API int
generate_canonical_encrypted_zero(
    secp256k1_context const* ctx,
    secp256k1_pubkey* enc_zero_c1,
    secp256k1_pubkey* enc_zero_c2,
    secp256k1_pubkey const* pubkey,
    unsigned char const* account_id,      // 20 bytes
    unsigned char const* mpt_issuance_id  // 24 bytes
);

/**
 * Generates a cryptographically secure 32-byte scalar (private key).
 * @return 1 on success, 0 on failure.
 */
SECP256K1_API int
generate_random_scalar(
    secp256k1_context const* ctx,
    unsigned char* scalar_bytes);

/**
 * Computes the point M = amount * G.
 * IMPORTANT: This function MUST NOT be called with amount = 0.
 */
SECP256K1_API int
compute_amount_point(
    secp256k1_context const* ctx,
    secp256k1_pubkey* mG,
    uint64_t amount);

/**
 * Builds the challenge hash input for the NON-ZERO amount case.
 * Output buffer must be 253 bytes.
 */
SECP256K1_API void
build_challenge_hash_input_nonzero(
    unsigned char* hash_input,
    secp256k1_pubkey const* c1,
    secp256k1_pubkey const* c2,
    secp256k1_pubkey const* pk,
    secp256k1_pubkey const* mG,
    secp256k1_pubkey const* T1,
    secp256k1_pubkey const* T2,
    unsigned char const* tx_context_id);

/**
 * Builds the challenge hash input for the ZERO amount case.
 * Output buffer must be 220 bytes.
 */
SECP256K1_API void
build_challenge_hash_input_zero(
    unsigned char* hash_input,
    secp256k1_pubkey const* c1,
    secp256k1_pubkey const* c2,
    secp256k1_pubkey const* pk,
    secp256k1_pubkey const* T1,
    secp256k1_pubkey const* T2,
    unsigned char const* tx_context_id);

/**
 * @brief Proves that a commitment (C1, C2) encrypts a specific plaintext
 * 'amount'.
 */
SECP256K1_API int
secp256k1_equality_plaintext_prove(
    secp256k1_context const* ctx,
    unsigned char* proof,
    secp256k1_pubkey const* c1,
    secp256k1_pubkey const* c2,
    secp256k1_pubkey const* pk_recipient,
    uint64_t amount,
    unsigned char const* randomness_r,
    unsigned char const* tx_context_id);

/**
 * @brief Verifies the proof generated by secp256k1_equality_plaintext_prove.
 */
SECP256K1_API int
secp256k1_equality_plaintext_verify(
    secp256k1_context const* ctx,
    unsigned char const* proof,
    secp256k1_pubkey const* c1,
    secp256k1_pubkey const* c2,
    secp256k1_pubkey const* pk_recipient,
    uint64_t amount,
    unsigned char const* tx_context_id);

void
build_pok_challenge(
    unsigned char* e,
    secp256k1_context const* ctx,
    secp256k1_pubkey const* pk,
    secp256k1_pubkey const* T,
    unsigned char const* context_id);

/** Proof of Knowledge of Secret Key for Registration */
int
secp256k1_mpt_pok_sk_prove(
    secp256k1_context const* ctx,
    unsigned char* proof, /* Expected size: 65 bytes */
    secp256k1_pubkey const* pk,
    unsigned char const* sk,
    unsigned char const* context_id);

int
secp256k1_mpt_pok_sk_verify(
    secp256k1_context const* ctx,
    unsigned char const* proof, /* Expected size: 65 bytes */
    secp256k1_pubkey const* pk,
    unsigned char const* context_id);

/**
 * Verifies that (c1, c2) is a valid ElGamal encryption of 'amount'
 * for 'pubkey_Q' using the revealed 'blinding_factor'.
 */
int
secp256k1_elgamal_verify_encryption(
    secp256k1_context const* ctx,
    secp256k1_pubkey const* c1,
    secp256k1_pubkey const* c2,
    secp256k1_pubkey const* pubkey_Q,
    uint64_t amount,
    unsigned char const* blinding_factor);

}  // namespace ripple

#endif
