#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>

#include <utility/mpt_utility.h>

#include <secp256k1.h>
#include <secp256k1_mpt.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace xrpl {
namespace test {
namespace jtx {

// Standalone confidential-voting crypto helpers for tests. Ballots use a tally
// key that no ledger account owns, so these build proofs directly against the
// merged mpt-crypto primitives rather than the per-account MPTTester helpers.
namespace ballot {

struct Keypair
{
    Buffer priv;  // 32 bytes
    Buffer pub;   // 33 bytes
};

inline Keypair
generateKeypair()
{
    Keypair kp{Buffer(kEcPrivKeyLength), Buffer(kEcPubKeyLength)};
    if (mpt_generate_keypair(kp.priv.data(), kp.pub.data()) != 0)
        Throw<std::runtime_error>("mpt_generate_keypair failed");
    return kp;
}

inline Buffer
randomScalar()
{
    Buffer out(kEcBlindingFactorLength);
    if (mpt_generate_blinding_factor(out.data()) != 0)
        Throw<std::runtime_error>("mpt_generate_blinding_factor failed");
    return out;
}

inline Buffer
encrypt(std::uint64_t value, Slice const& pubKey, Slice const& randomness)
{
    auto out = encryptAmount(value, pubKey, randomness);
    if (!out)
        Throw<std::runtime_error>("encryptAmount failed");
    return *out;
}

inline Buffer
pedersen(std::uint64_t value, Slice const& blinding)
{
    Buffer out(kEcPedersenCommitmentLength);
    if (mpt_get_pedersen_commitment(value, blinding.data(), out.data()) != 0)
        Throw<std::runtime_error>("mpt_get_pedersen_commitment failed");
    return out;
}

// R = a + b (mod curve order).
inline Buffer
scalarAdd(Slice const& a, Slice const& b)
{
    Buffer out(kEcScalarLength);
    secp256k1_mpt_scalar_add(out.data(), a.data(), b.data());
    return out;
}

// Aggregated Bulletproof range proof over m committed option values.
inline Buffer
rangeProof(
    std::vector<std::uint64_t> const& values,
    std::vector<Buffer> const& blindings,
    uint256 const& contextHash)
{
    auto* const ctx = mpt_secp256k1_context();
    secp256k1_pubkey h;
    secp256k1_mpt_get_h_generator(ctx, &h);

    std::vector<std::uint8_t> flat(values.size() * kEcScalarLength);
    for (std::size_t i = 0; i < blindings.size(); ++i)
        std::memcpy(flat.data() + (i * kEcScalarLength), blindings[i].data(), kEcScalarLength);

    Buffer proof(kEcDoubleBulletproofLength * values.size());
    std::size_t proofLen = proof.size();
    if (secp256k1_bulletproof_prove_agg(
            ctx,
            proof.data(),
            &proofLen,
            values.data(),
            flat.data(),
            values.size(),
            &h,
            contextHash.data()) == 0)
    {
        Throw<std::runtime_error>("secp256k1_bulletproof_prove_agg failed");
    }

    return Buffer(proof.data(), proofLen);
}

// Schnorr proof of possession of the tally private key, bound to a context.
inline Buffer
schnorrProof(Keypair const& kp, uint256 const& contextHash)
{
    Buffer proof(kEcSchnorrProofLength);
    if (mpt_get_convert_proof(kp.pub.data(), kp.priv.data(), contextHash.data(), proof.data()) != 0)
        Throw<std::runtime_error>("mpt_get_convert_proof failed");
    return proof;
}

// Decryption-correctness (clawback) proof: proves ciphertext decrypts to amount
// under the tally key.
inline Buffer
decryptionProof(
    Keypair const& kp,
    std::uint64_t amount,
    Slice const& ciphertext,
    uint256 const& contextHash)
{
    Buffer proof(kEcClawbackProofLength);
    if (mpt_get_clawback_proof(
            kp.priv.data(),
            kp.pub.data(),
            contextHash.data(),
            amount,
            ciphertext.data(),
            proof.data()) != 0)
    {
        Throw<std::runtime_error>("mpt_get_clawback_proof failed");
    }
    return proof;
}

inline std::optional<std::uint64_t>
decrypt(Slice const& ciphertext, Slice const& priv, std::uint64_t high)
{
    std::uint64_t out = 0;
    if (mpt_decrypt_amount(ciphertext.data(), priv.data(), &out, 0, high) != 0)
        return std::nullopt;
    return out;
}

// Per-option ciphertext<->commitment linkage proof. Proves each mirror
// ciphertext (tally first, then optional auditor/voter, all sharing the option
// randomness r) encrypts the value committed in PC_m = value*G + r*H. Uses the
// canonical vacuous balance witness (sk_A = 1, rho_b = 1) the verifier expects.
inline Buffer
linkageProof(
    std::uint64_t value,
    Slice const& r,
    std::vector<Slice> const& pubKeys,
    std::vector<Buffer> const& ciphertexts,  // one 66-byte ct per key, shared C1
    Slice const& commitment,
    uint256 const& contextHash)
{
    auto* const ctx = mpt_secp256k1_context();
    std::size_t const n = pubKeys.size();

    auto const first = makeEcPair(ciphertexts[0]);
    if (!first)
        Throw<std::runtime_error>("linkageProof: bad tally ciphertext");
    secp256k1_pubkey const c1 = first->c1;

    std::vector<secp256k1_pubkey> c2v(n);
    std::vector<secp256k1_pubkey> pkv(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        auto const pair = makeEcPair(ciphertexts[i]);
        if (!pair)
            Throw<std::runtime_error>("linkageProof: bad mirror ciphertext");
        c2v[i] = pair->c2;
        if (secp256k1_ec_pubkey_parse(ctx, &pkv[i], pubKeys[i].data(), pubKeys[i].size()) != 1)
            Throw<std::runtime_error>("linkageProof: bad pubkey");
    }

    secp256k1_pubkey pcm;
    if (secp256k1_ec_pubkey_parse(ctx, &pcm, commitment.data(), commitment.size()) != 1)
        Throw<std::runtime_error>("linkageProof: bad commitment");

    Buffer skA(kEcScalarLength);
    skA.data()[kEcScalarLength - 1] = 1;
    Buffer rhoB(kEcScalarLength);
    rhoB.data()[kEcScalarLength - 1] = 1;
    secp256k1_pubkey pkA;
    if (secp256k1_ec_pubkey_create(ctx, &pkA, skA.data()) != 1)
        Throw<std::runtime_error>("linkageProof: pk_A");
    secp256k1_pubkey pcb;  // PC_b = 0*G + 1*H = H
    if (secp256k1_mpt_get_h_generator(ctx, &pcb) != 1)
        Throw<std::runtime_error>("linkageProof: H");

    Buffer proof(kEcSendSigmaProofLength);
    if (secp256k1_compact_standard_prove(
            ctx,
            proof.data(),
            value,
            /*balance=*/0,
            r.data(),
            skA.data(),
            rhoB.data(),
            n,
            &c1,
            c2v.data(),
            pkv.data(),
            &pcm,
            &pkA,
            &pcb,
            &pkA,  // B1 = G
            &pkA,  // B2 = G
            contextHash.data()) != 1)
    {
        Throw<std::runtime_error>("secp256k1_compact_standard_prove failed");
    }
    return proof;
}

}  // namespace ballot

// A cast's per-option payload, ready to be serialized into a BallotCastVote.
struct BallotCast
{
    std::vector<Buffer> ciphertexts;         // one ElGamal ciphertext per option
    std::vector<Buffer> commitments;         // one Pedersen commitment per option
    std::vector<Buffer> linkageProofs;       // one compact-sigma proof per option
    std::vector<Buffer> auditorCiphertexts;  // empty unless auditor configured
    std::vector<Buffer> voterCiphertexts;    // empty unless voter-recoverable
    Buffer proof;                            // aggregated range proof
    Buffer blinding;                         // aggregate ElGamal randomness R
};

// Build a cast that assigns the full weight to option `choice` and zero to the
// rest, with real range proof and aggregate randomness.
inline BallotCast
makeBallotCast(
    ballot::Keypair const& tally,
    std::uint8_t optionCount,
    std::uint8_t choice,
    std::uint64_t weight,
    uint256 const& contextHash,
    std::optional<ballot::Keypair> const& auditor = std::nullopt,
    std::optional<ballot::Keypair> const& voter = std::nullopt)
{
    BallotCast cast;
    std::vector<std::uint64_t> values(optionCount, 0);
    values[choice] = weight;

    // A single per-option scalar r is the ElGamal randomness AND the Pedersen
    // blinding, so the commitment PC = value*G + r*H links directly to the
    // ciphertext for the compact-sigma linkage proof.
    std::vector<Buffer> blindings;
    Buffer aggregate(kEcScalarLength);  // zero-initialized

    for (std::uint8_t i = 0; i < optionCount; ++i)
    {
        auto const r = ballot::randomScalar();
        blindings.push_back(r);

        cast.commitments.push_back(ballot::pedersen(values[i], r));
        cast.ciphertexts.push_back(ballot::encrypt(values[i], tally.pub, r));
        if (auditor)
            cast.auditorCiphertexts.push_back(ballot::encrypt(values[i], auditor->pub, r));
        if (voter)
            cast.voterCiphertexts.push_back(ballot::encrypt(values[i], voter->pub, r));

        aggregate = ballot::scalarAdd(aggregate, r);
    }

    cast.blinding = aggregate;
    cast.proof = ballot::rangeProof(values, blindings, contextHash);

    // Per-option linkage proof over every mirror key present (tally first).
    for (std::uint8_t i = 0; i < optionCount; ++i)
    {
        std::vector<Slice> keys{tally.pub};
        std::vector<Buffer> cts{cast.ciphertexts[i]};
        if (auditor)
        {
            keys.push_back(auditor->pub);
            cts.push_back(cast.auditorCiphertexts[i]);
        }
        if (voter)
        {
            keys.push_back(voter->pub);
            cts.push_back(cast.voterCiphertexts[i]);
        }
        cast.linkageProofs.push_back(
            ballot::linkageProof(
                values[i], blindings[i], keys, cts, cast.commitments[i], contextHash));
    }
    return cast;
}

// Serialize a ciphertext vector as a JSON array of BallotOption inner objects.
inline json::Value
ballotOptionArray(
    std::vector<Buffer> const& ciphertexts,
    std::vector<Buffer> const* commitments,
    std::vector<Buffer> const* proofs = nullptr)
{
    json::Value arr(json::ValueType::Array);
    for (std::size_t i = 0; i < ciphertexts.size(); ++i)
    {
        json::Value inner;
        inner[sfEncryptedVote.jsonName] = strHex(ciphertexts[i]);
        if (commitments)
            inner[sfAmountCommitment.jsonName] = strHex((*commitments)[i]);
        if (proofs)
            inner[sfZKProof.jsonName] = strHex((*proofs)[i]);
        json::Value elem;
        elem[sfBallotOption.jsonName] = inner;
        arr.append(elem);
    }
    return arr;
}

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
