/** @file
 *  Implements the `wallet_propose` admin RPC command.
 *
 *  Generates a complete XRPL account identity — seed, key pair, and account
 *  address — from a caller-supplied or randomly-generated seed. Exposed only
 *  to admin-role clients; never reachable by untrusted users.
 */
#include <xrpld/rpc/handlers/admin/keygen/WalletPropose.h>

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/tokens.h>

#include <cmath>
#include <map>
#include <optional>
#include <string>

namespace xrpl {

/** Estimate the Shannon entropy of a string in bits.
 *
 *  Computes the per-symbol Shannon entropy from character frequencies and
 *  multiplies by the string length to yield a total bit estimate. The result
 *  is floored to be conservative — callers use this to gate brute-force
 *  vulnerability warnings, so over-estimating entropy would suppress a
 *  legitimate warning.
 *
 *  @param input The string whose entropy is estimated.
 *  @return Estimated entropy in bits, floored to the nearest integer.
 *  @note This is a statistical estimate, not a cryptographic strength
 *      measurement. It is only meaningful for human-chosen passphrases;
 *      a randomly-generated seed will always score well regardless.
 */
double
estimateEntropy(std::string const& input)
{
    std::map<int, double> freq;

    for (auto const& c : input)
        freq[c]++;

    double se = 0.0;

    for (auto const& [_, f] : freq)
    {
        (void)_;
        auto x = f / input.length();
        se += (x)*log2(x);
    }

    // Floor because it is better to be conservative when warning about
    // low-entropy passphrases.
    return std::floor(-se * input.length());
}

/** RPC dispatch entry point for the `wallet_propose` command.
 *
 *  Extracts the request parameters from the JSON context and delegates to
 *  `walletPropose()`. Separating dispatch from logic allows the core
 *  function to be called directly in tests without a full RPC context.
 *
 *  Accepts an optional `passphrase`, `seed`, or `seed_hex` field (mutually
 *  exclusive); and an optional `key_type` ("secp256k1" or "ed25519").
 *
 *  @param context The RPC dispatch context carrying `params`.
 *  @return A JSON object with key-generation results, or an error object.
 */
json::Value
doWalletPropose(RPC::JsonContext& context)
{
    return walletPropose(context.params);
}

/** Generate a complete XRPL account identity from parameters.
 *
 *  Resolves a seed via a priority-ordered chain: XrplLib-encoded Ed25519 seed
 *  detection (in `passphrase` or `seed` fields) → `getSeedFromRPC()` for
 *  explicit `passphrase` / `seed` / `seed_hex` → `randomSeed()` as the
 *  fallback. Key type defaults to `secp256k1` for historical compatibility
 *  when none is specified.
 *
 *  On success, returns a JSON object with seven fields:
 *  - `master_seed`     — base58-encoded seed (canonical backup format)
 *  - `master_seed_hex` — raw hex of the seed bytes
 *  - `master_key`      — RFC 1751 mnemonic encoding
 *  - `account_id`      — base58check account address
 *  - `public_key`      — base58-encoded public key with AccountPublic prefix
 *  - `public_key_hex`  — raw hex public key
 *  - `key_type`        — resolved algorithm name ("secp256k1" or "ed25519")
 *
 *  A `warning` field is appended when a user-supplied passphrase is detected
 *  that is not itself an encoding of the seed: a strong warning when entropy
 *  is below 80 bits, and a softer advisory otherwise (any brain-wallet
 *  derivation is weaker than a random seed). The warning is suppressed when
 *  the passphrase matches the seed's own 1751, base58, or hex encoding, and
 *  when a XrplLib-encoded seed is detected.
 *
 *  @param params JSON object containing optional fields: `key_type`,
 *      `passphrase`, `seed`, `seed_hex`. The three seed-input fields are
 *      mutually exclusive; supplying more than one yields an error from
 *      `getSeedFromRPC()`.
 *  @return A JSON result object on success, or a JSON error object on failure.
 *  @note Supplying a XrplLib-encoded Ed25519 seed together with
 *      `key_type: "secp256k1"` returns `rpcBAD_SEED` rather than silently
 *      deriving the wrong key. XrplLib seeds are unambiguously Ed25519.
 *  @see walletPropose() is called directly by `KeyGeneration_test.cpp` to
 *      validate known constant vectors for both key types and both warning
 *      tiers.
 */
json::Value
walletPropose(json::Value const& params)
{
    std::optional<KeyType> keyType;
    std::optional<Seed> seed;
    bool libSeed = false;

    if (params.isMember(jss::key_type))
    {
        if (!params[jss::key_type].isString())
        {
            return RPC::expectedFieldError(jss::key_type, "string");
        }

        keyType = keyTypeFromString(params[jss::key_type].asString());

        if (!keyType)
            return rpcError(RpcInvalidParams);
    }

    // XrplLib encodes seeds for Ed25519 wallets with a non-standard base58
    // prefix (0xE1 0x4B), producing an 18-byte form rippled never emits.
    // Detect it in passphrase/seed fields first so we can enforce the
    // Ed25519-only constraint and avoid confusing users who migrate keys.
    {
        if (params.isMember(jss::passphrase))
        {
            seed = RPC::parseXrplLibSeed(params[jss::passphrase]);
        }
        else if (params.isMember(jss::seed))
        {
            seed = RPC::parseXrplLibSeed(params[jss::seed]);
        }

        if (seed)
        {
            libSeed = true;

            // If the user *explicitly* requests a key type other than
            // Ed25519 we return an error.
            if (keyType.value_or(KeyType::Ed25519) != KeyType::Ed25519)
                return rpcError(RpcBadSeed);

            keyType = KeyType::Ed25519;
        }
    }

    if (!seed)
    {
        if (params.isMember(jss::passphrase) || params.isMember(jss::seed) ||
            params.isMember(jss::seed_hex))
        {
            json::Value err;

            seed = RPC::getSeedFromRPC(params, err);

            if (!seed)
                return err;
        }
        else
        {
            seed = randomSeed();
        }
    }

    if (!keyType)
        keyType = KeyType::Secp256k1;

    auto const publicKey = generateKeyPair(*keyType, *seed).first;

    json::Value obj(json::ValueType::Object);

    auto const seed1751 = seedAs1751(*seed);
    auto const seedHex = strHex(*seed);
    auto const seedBase58 = toBase58(*seed);

    obj[jss::master_seed] = seedBase58;
    obj[jss::master_seed_hex] = seedHex;
    obj[jss::master_key] = seed1751;
    obj[jss::account_id] = toBase58(calcAccountID(publicKey));
    obj[jss::public_key] = toBase58(TokenType::AccountPublic, publicKey);
    obj[jss::key_type] = to_string(*keyType);
    obj[jss::public_key_hex] = strHex(publicKey);

    // Warn about brain-wallet weakness when a user passphrase was used.
    // Skip if the passphrase is just an alternate encoding of the seed
    // itself — the user is passing a valid seed as a string, not inventing
    // a memorable phrase, so entropy is already adequate.
    if (!libSeed && params.isMember(jss::passphrase))
    {
        auto const passphrase = params[jss::passphrase].asString();

        if (passphrase != seed1751 && passphrase != seedBase58 && passphrase != seedHex)
        {
            // 80 bits is the threshold: below it the passphrase is
            // acutely vulnerable to brute-force; at or above it we still
            // warn because any deterministic derivation is weaker than a
            // truly random seed.
            if (estimateEntropy(passphrase) < 80.0)
            {
                obj[jss::warning] =
                    "This wallet was generated using a user-supplied "
                    "passphrase that has low entropy and is vulnerable "
                    "to brute-force attacks.";
            }
            else
            {
                obj[jss::warning] =
                    "This wallet was generated using a user-supplied "
                    "passphrase. It may be vulnerable to brute-force "
                    "attacks.";
            }
        }
    }

    return obj;
}

}  // namespace xrpl
