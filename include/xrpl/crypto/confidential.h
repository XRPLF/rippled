#pragma once

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace xrpl {
namespace confidential {

/** Byte sizes fixed by XLS-0096 / compact-sigma encoding. */
inline constexpr std::size_t kScalarBytes = 32;
inline constexpr std::size_t kCompressedPointBytes = 33;
inline constexpr std::size_t kCiphertextBytes = 66;
inline constexpr std::size_t kSchnorrRegisterProofBytes = 64;
inline constexpr std::size_t kSendSigmaProofBytes = 192;
inline constexpr std::size_t kConvertBackSigmaProofBytes = 128;
inline constexpr std::size_t kAuditorEqualitySigmaProofBytes = 128;
inline constexpr std::size_t kClawbackSigmaProofBytes = 64;
// Bulletproofs (Bünz et al., 2017/1066) for n=64: 2 log2(nm)+4 points + 5 scalars.
inline constexpr std::size_t kSingleBulletproofBytes = 688;      // m=1: 16*33 + 5*32
inline constexpr std::size_t kAggregatedBulletproofBytes = 754;  // m=2: 18*33 + 5*32
inline constexpr std::size_t kSendZkProofBytes =
    kSendSigmaProofBytes + kAggregatedBulletproofBytes;
inline constexpr std::size_t kConvertBackZkProofBytes =
    kConvertBackSigmaProofBytes + kSingleBulletproofBytes;

using Scalar = std::array<std::uint8_t, kScalarBytes>;
using CompressedPoint = std::array<std::uint8_t, kCompressedPointBytes>;
using CiphertextBytes = std::array<std::uint8_t, kCiphertextBytes>;
using SchnorrRegisterProof = std::array<std::uint8_t, kSchnorrRegisterProofBytes>;
using SendSigmaProof = std::array<std::uint8_t, kSendSigmaProofBytes>;
using ConvertBackSigmaProof = std::array<std::uint8_t, kConvertBackSigmaProofBytes>;
using AuditorEqualitySigmaProof =
    std::array<std::uint8_t, kAuditorEqualitySigmaProofBytes>;
using ClawbackSigmaProof = std::array<std::uint8_t, kClawbackSigmaProofBytes>;

/** Fiat–Shamir domain tags from Updated_ConfidentialMPT_20260612.md. */
inline constexpr std::string_view kTagSchnorrRegister = "CMPT_POK_SK_REGISTER";
inline constexpr std::string_view kTagSendSigma = "CMPT_SEND_SIGMA";
inline constexpr std::string_view kTagConvertBackSigma = "CMPT_CONVERTBACK_SIGMA";
inline constexpr std::string_view kTagAuditorEqualitySigma =
    "CMPT_AUDITOR_EQ_SIGMA";
inline constexpr std::string_view kTagClawbackSigma = "CMPT_CLAWBACK_SIGMA";
inline constexpr std::string_view kTagEncZero = "EncZero";

/** EC-ElGamal ciphertext: C1 || C2, each a 33-byte compressed point. */
struct Ciphertext
{
    CompressedPoint c1{};
    CompressedPoint c2{};
};

/** Strict scalar parse: exactly 32 bytes and in [1, n-1] (secp256k1_ec_seckey_verify). */
[[nodiscard]] bool
parseScalar(Slice in, Scalar& out) noexcept;

/** Serialize a scalar (always 32 bytes). */
[[nodiscard]] bool
serializeScalar(Scalar const& in, Slice out) noexcept;

/** Map a public amount into a 32-byte big-endian scalar (may be zero). */
[[nodiscard]] Scalar
amountToScalar(std::uint64_t amount) noexcept;

[[nodiscard]] bool
addScalars(Scalar const& a, Scalar const& b, Scalar& out) noexcept;

[[nodiscard]] bool
subScalars(Scalar const& a, Scalar const& b, Scalar& out) noexcept;

/** Compressed secp256k1 point: 0x02/0x03 || X, on-curve via pubkey_parse. */
[[nodiscard]] bool
parseCompressedPoint(Slice in, CompressedPoint& out) noexcept;

[[nodiscard]] bool
serializeCompressedPoint(CompressedPoint const& in, Slice out) noexcept;

/** P = k·G using secp256k1_ec_pubkey_create. Fails if k is not a valid seckey. */
[[nodiscard]] bool
pointMulBase(Scalar const& k, CompressedPoint& out) noexcept;

/** R = P + Q via secp256k1_ec_pubkey_combine. */
[[nodiscard]] bool
pointAdd(
    CompressedPoint const& p,
    CompressedPoint const& q,
    CompressedPoint& out) noexcept;

/** R = P - Q via negate + combine. */
[[nodiscard]] bool
pointSub(
    CompressedPoint const& p,
    CompressedPoint const& q,
    CompressedPoint& out) noexcept;

/** R = k·P via secp256k1_ec_pubkey_tweak_mul. */
[[nodiscard]] bool
pointMul(
    CompressedPoint const& p,
    Scalar const& k,
    CompressedPoint& out) noexcept;

/** R = P + k·G via secp256k1_ec_pubkey_tweak_add. */
[[nodiscard]] bool
pointAddMulBase(
    CompressedPoint const& p,
    Scalar const& k,
    CompressedPoint& out) noexcept;

[[nodiscard]] bool
parseCiphertext(Slice in, Ciphertext& out) noexcept;

[[nodiscard]] bool
serializeCiphertext(Ciphertext const& in, Slice out) noexcept;

/** Encrypt amount under pk with randomness r: (r·G, amount·G + r·pk). */
[[nodiscard]] bool
elgamalEncrypt(
    CompressedPoint const& pk,
    std::uint64_t amount,
    Scalar const& r,
    Ciphertext& out) noexcept;

/** Homomorphic add: (C1+D1, C2+D2). */
[[nodiscard]] bool
elgamalAdd(Ciphertext const& a, Ciphertext const& b, Ciphertext& out) noexcept;

/**
 * Homomorphic subtract after re-randomizing the minuend.
 *
 * Adding Enc(0; r) first keeps an exact full-balance debit representable:
 * secp256k1's public-key API cannot serialize the point at infinity.
 */
[[nodiscard]] bool
elgamalSub(
    Ciphertext const& a,
    Ciphertext const& b,
    CompressedPoint const& pk,
    Scalar const& r,
    Ciphertext& out) noexcept;

/** Re-randomize: CT ⊕ Enc(0; r). Used when applying Send credits. */
[[nodiscard]] bool
elgamalRerandomize(
    Ciphertext const& in,
    CompressedPoint const& pk,
    Scalar const& r,
    Ciphertext& out) noexcept;

/**
 * Canonical EncZero(Acct, Issuer, Curr) under holder pk.
 * r = SHA512Half("EncZero" || Acct || Issuer || Curr) reduced into [1, n-1].
 */
[[nodiscard]] bool
encZero(
    CompressedPoint const& pk,
    Slice account,
    Slice issuer,
    Slice currency,
    Ciphertext& out) noexcept;

/**
 * Provisional NUMS Pedersen generator H.
 * SPEC OMISSION: consensus NUMS derivation is not fixed in the available
 * documents; try-and-increment from SHA512Half("CMPT_NUMS_H"||counter).
 */
[[nodiscard]] CompressedPoint const&
pedersenNumsGenerator() noexcept;

/** PC = m·G + r·H (r reused as ElGamal randomness for amount linkage). */
[[nodiscard]] bool
pedersenCommit(
    std::uint64_t amount,
    Scalar const& blinding,
    CompressedPoint& out) noexcept;

[[nodiscard]] bool
pedersenCommitScalar(
    Scalar const& amount,
    Scalar const& blinding,
    CompressedPoint& out) noexcept;

/**
 * TransactionContextID = SHA512Half(preimage).
 * Caller supplies the exact Updated field concatenation
 * TxType||Account||MPTokenIssuanceID||SequenceOrTicket||TxSpecific.
 */
[[nodiscard]] uint256
transactionContextID(Slice preimage) noexcept;

/** Helpers that assemble the eprint TxSpecific layouts (big-endian). */
[[nodiscard]] uint256
transactionContextIDSend(
    std::uint16_t txType,
    Slice account,
    Slice issuanceId,
    std::uint32_t sequenceOrTicket,
    Slice receiver,
    std::uint32_t spendingVersion) noexcept;

[[nodiscard]] uint256
transactionContextIDConvertBack(
    std::uint16_t txType,
    Slice account,
    Slice issuanceId,
    std::uint32_t sequenceOrTicket,
    std::uint32_t spendingVersion) noexcept;

[[nodiscard]] uint256
transactionContextIDConvert(
    std::uint16_t txType,
    Slice account,
    Slice issuanceId,
    std::uint32_t sequenceOrTicket) noexcept;

[[nodiscard]] uint256
transactionContextIDClawback(
    std::uint16_t txType,
    Slice account,
    Slice issuanceId,
    std::uint32_t sequenceOrTicket,
    Slice holder) noexcept;

/** TxSpecific := Holder || TargetAuditorVersion (auditor migration). */
[[nodiscard]] uint256
transactionContextIDMigrateAuditor(
    std::uint16_t txType,
    Slice account,
    Slice issuanceId,
    std::uint32_t sequenceOrTicket,
    Slice holder,
    std::uint32_t targetAuditorVersion) noexcept;

/**
 * Compact Schnorr PoK for key registration: π = (e || s), 64 bytes.
 * SPEC INCONSISTENCY: eprint serializes (T||s) = 65 bytes; XLS lists 64.
 * Compact (e,s) matches the XLS size and clawback/send compact pattern.
 */
[[nodiscard]] bool
proveSchnorrRegister(
    Scalar const& sk,
    CompressedPoint const& pk,
    Slice contextId,
    SchnorrRegisterProof& out) noexcept;

[[nodiscard]] bool
verifySchnorrRegister(
    CompressedPoint const& pk,
    Slice contextId,
    Slice proof) noexcept;

struct SendSigmaPublicInput
{
    std::vector<CompressedPoint> recipientKeys;  // P_1 .. P_n
    CompressedPoint senderKey{};                 // P_A
    CompressedPoint c1{};
    std::vector<CompressedPoint> c2;  // C_{2,1} .. C_{2,n}
    CompressedPoint amountCommitment{};
    CompressedPoint balanceCommitment{};
    CompressedPoint balanceC1{};  // B_1
    CompressedPoint balanceC2{};  // B_2
};

struct SendSigmaWitness
{
    Scalar amount{};     // m
    Scalar randomness{}; // r (shared ElGamal / amount-commitment blinding)
    Scalar balance{};    // b
    Scalar balanceBlind{}; // ρ
    Scalar senderSk{};   // sk_A
};

[[nodiscard]] bool
proveSendSigma(
    SendSigmaPublicInput const& pub,
    SendSigmaWitness const& wit,
    Slice contextId,
    SendSigmaProof& out) noexcept;

[[nodiscard]] bool
verifySendSigma(
    SendSigmaPublicInput const& pub,
    Slice contextId,
    Slice proof) noexcept;

struct ConvertBackSigmaPublicInput
{
    CompressedPoint holderKey{};
    CompressedPoint balanceC1{};
    CompressedPoint balanceC2{};
    CompressedPoint balanceCommitment{};
};

struct ConvertBackSigmaWitness
{
    Scalar balance{};
    Scalar balanceBlind{};
    Scalar holderSk{};
};

[[nodiscard]] bool
proveConvertBackSigma(
    ConvertBackSigmaPublicInput const& pub,
    ConvertBackSigmaWitness const& wit,
    Slice contextId,
    ConvertBackSigmaProof& out) noexcept;

[[nodiscard]] bool
verifyConvertBackSigma(
    ConvertBackSigmaPublicInput const& pub,
    Slice contextId,
    Slice proof) noexcept;

struct ClawbackSigmaPublicInput
{
    CompressedPoint issuerKey{};
    Ciphertext issuerBalance{};
    std::uint64_t revealedAmount = 0;
};

[[nodiscard]] bool
proveClawbackSigma(
    ClawbackSigmaPublicInput const& pub,
    Scalar const& issuerSk,
    Slice contextId,
    ClawbackSigmaProof& out) noexcept;

[[nodiscard]] bool
verifyClawbackSigma(
    ClawbackSigmaPublicInput const& pub,
    Slice contextId,
    Slice proof) noexcept;

/**
 * Compact equality sigma for auditor migration: π = (e || z_x || z_r || z_m),
 * 128 bytes. Proves pk_I = x·G, C1_A = r·G, C2_I = m·G + x·C1_I,
 * C2_A = m·G + r·pk_A under one Fiat–Shamir challenge.
 */
struct AuditorEqualitySigmaPublicInput
{
    CompressedPoint issuerKey{};       // pk_I
    Ciphertext issuerCiphertext{};     // (C1_I, C2_I)
    CompressedPoint auditorKey{};      // pending auditor pk_A
    Ciphertext auditorCiphertext{};    // (C1_A, C2_A)
};

struct AuditorEqualitySigmaWitness
{
    Scalar issuerSk{};    // x
    Scalar randomness{};  // r (fresh auditor ciphertext randomness)
    Scalar amount{};      // m
};

[[nodiscard]] bool
proveAuditorEqualitySigma(
    AuditorEqualitySigmaPublicInput const& pub,
    AuditorEqualitySigmaWitness const& wit,
    Slice contextId,
    AuditorEqualitySigmaProof& out) noexcept;

[[nodiscard]] bool
verifyAuditorEqualitySigma(
    AuditorEqualitySigmaPublicInput const& pub,
    Slice contextId,
    Slice proof) noexcept;

/** First 32 bytes of a compact sigma proof (the Fiat–Shamir challenge e). */
[[nodiscard]] bool
extractSigmaChallenge(Slice proof, Scalar& e) noexcept;

/**
 * Single 64-bit Bulletproof that PC commits to v in [0, 2^64).
 * Proof layout (688 bytes): A||S||T1||T2||(L_i||R_i)*6||tau_x||mu||t||a||b
 * Generators and transcript are derived as NUMS from SHA512Half; the 2017
 * paper defines the protocol, not the XRPL-specific generator strings.
 */
[[nodiscard]] bool
proveBulletproofSingle(
    CompressedPoint const& commitment,
    std::uint64_t value,
    Scalar const& blinding,
    std::array<std::uint8_t, kSingleBulletproofBytes>& out) noexcept;

[[nodiscard]] bool
verifyBulletproofSingle(CompressedPoint const& commitment, Slice proof) noexcept;

/**
 * Aggregated Bulletproof for two 64-bit values (Send: m and b-m).
 * Proof layout (754 bytes): A||S||T1||T2||(L_i||R_i)*7||tau_x||mu||t||a||b
 */
[[nodiscard]] bool
proveBulletproofAggregated(
    CompressedPoint const& commitment0,
    CompressedPoint const& commitment1,
    std::uint64_t value0,
    std::uint64_t value1,
    Scalar const& blinding0,
    Scalar const& blinding1,
    std::array<std::uint8_t, kAggregatedBulletproofBytes>& out) noexcept;

[[nodiscard]] bool
verifyBulletproofAggregated(
    CompressedPoint const& commitment0,
    CompressedPoint const& commitment1,
    Slice proof) noexcept;

/**
 * Send range proof for m in [1, 2^64) and b-m in [0, 2^64).
 *
 * Positivity is enforced by proving m-1 against PC_m-G.
 */
[[nodiscard]] bool
proveBulletproofSend(
    CompressedPoint const& amountCommitment,
    CompressedPoint const& remainingCommitment,
    std::uint64_t amount,
    std::uint64_t remaining,
    Scalar const& amountBlinding,
    Scalar const& remainingBlinding,
    std::array<std::uint8_t, kAggregatedBulletproofBytes>& out) noexcept;

[[nodiscard]] bool
verifyBulletproofSend(
    CompressedPoint const& amountCommitment,
    CompressedPoint const& remainingCommitment,
    Slice proof) noexcept;

}  // namespace confidential
}  // namespace xrpl
