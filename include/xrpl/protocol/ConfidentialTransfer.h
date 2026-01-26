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
// Helper struct to bundle the ElGamal Public Key and the associated Ciphertext
struct ConfidentialRecipient
{
    Slice const publicKey;
    Slice const encryptedAmount;
};

inline void
incrementConfidentialVersion(STObject& mptoken)
{
    // Retrieve current version and increment.
    // Unsigned integer overflow is defined behavior in C++ (wraps to 0),
    // which is acceptable here.
    mptoken[sfConfidentialBalanceVersion] =
        mptoken[~sfConfidentialBalanceVersion].value_or(0u) + 1u;
}

void
addCommonZKPFields(
    Serializer& s,
    std::uint16_t txType,
    AccountID const& account,
    std::uint32_t sequence,
    uint192 const& issuanceID);

uint256
getSendContextHash(
    AccountID const& account,
    std::uint32_t sequence,
    uint192 const& issuanceID,
    AccountID const& destination,
    std::uint32_t version);

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

std::optional<Buffer>
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

NotTEC
checkEncryptedAmountFormat(STObject const& object);

TER
verifyRevealedAmount(
    std::uint64_t const amount,
    Slice const& blindingFactor,
    ConfidentialRecipient const& holder,
    ConfidentialRecipient const& issuer,
    std::optional<ConfidentialRecipient> const& auditor);

constexpr std::size_t
getConfidentialRecipientCount(bool hasAuditor)
{
    return hasAuditor ? 4 : 3;
}

std::size_t
getMultiCiphertextEqualityProofSize(std::size_t nRecipients);

TER
verifyMultiCiphertextEqualityProof(
    Slice const& proof,
    std::vector<ConfidentialRecipient> const& recipients,
    std::size_t const nRecipients,
    uint256 const& contextHash);

TER
verifyClawbackEqualityProof(
    uint64_t const amount,
    Slice const& proof,
    Slice const& pubKeySlice,
    Slice const& ciphertext,
    uint256 const& contextHash);

// generates a 32 byte randomness factor to be used in encryption and proofs
Buffer
generateBlindingFactor();

TER
verifyPedersenLinkage(
    Slice const& proof,
    Slice const& encAmt,
    Slice const& pubKeySlice,
    Slice const& pcmSlice,
    uint256 const& contextHash);

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

/**
 * @brief Proves the link between an ElGamal ciphertext and a Pedersen
 * commitment.
 * * Formal Statement: Knowledge of (m, r, rho) such that:
 * C1 = r*G, C2 = m*G + r*Pk, and PCm = m*G + rho*H.
 * * @param ctx         Pointer to a secp256k1 context object.
 * @param proof       [OUT] Pointer to 195-byte buffer for the proof output.
 * @param c1          Pointer to the ElGamal C1 point (r*G).
 * @param c2          Pointer to the ElGamal C2 point (m*G + r*Pk).
 * @param pk          Pointer to the recipient's public key.
 * @param pcm         Pointer to the Pedersen Commitment (m*G + rho*H).
 * @param amount      The plaintext amount (m).
 * @param r           The 32-byte secret ElGamal blinding factor.
 * @param rho         The 32-byte secret Pedersen blinding factor.
 * @param context_id  32-byte unique transaction context identifier.
 * @return 1 on success, 0 on failure.
 */
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
    unsigned char const* context_id);

/**
 * @brief Verifies the link proof between ElGamal and Pedersen commitments.
 * * @return 1 if the proof is valid, 0 otherwise.
 */
int
secp256k1_elgamal_pedersen_link_verify(
    secp256k1_context const* ctx,
    unsigned char const* proof,
    secp256k1_pubkey const* c1,
    secp256k1_pubkey const* c2,
    secp256k1_pubkey const* pk,
    secp256k1_pubkey const* pcm,
    unsigned char const* context_id);

/**
 * Compute a Pedersen Commitment: PC = m*G + rho*H
 * Returns 1 on success, 0 on failure.
 */
int
secp256k1_mpt_pedersen_commit(
    secp256k1_context const* ctx,
    secp256k1_pubkey* commitment,
    uint64_t amount,
    unsigned char const* blinding_factor_rho /* 32 bytes */
);

// Multi-proof for same plaintexts
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
    unsigned char const* tx_id);

size_t
secp256k1_mpt_prove_same_plaintext_multi_size(size_t n);

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
    unsigned char const* tx_id);

int
secp256k1_mpt_verify_same_plaintext_multi(
    secp256k1_context const* ctx,
    unsigned char const* proof,
    size_t proof_len,
    size_t n,
    secp256k1_pubkey const* R,
    secp256k1_pubkey const* S,
    secp256k1_pubkey const* Pk,
    unsigned char const* tx_id);
}  // namespace ripple

#endif
