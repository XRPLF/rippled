#pragma once

#include <xrpl/json/json_value.h>

namespace xrpl {

/** Generate a complete XRPL account identity from caller-supplied or random seed material.
 *
 *  Implements the core logic of the `wallet_propose` admin RPC command without
 *  requiring a full RPC dispatch context, so it can be called directly from
 *  unit tests. The companion `doWalletPropose()` adapter in `WalletPropose.cpp`
 *  simply forwards `context.params` here.
 *
 *  Seed resolution order:
 *  1. XrplLib-encoded Ed25519 seed detected in `passphrase` or `seed` fields
 *     (non-standard 0xE1 0x4B base58 prefix). Forces `key_type` to `ed25519`;
 *     returns `rpcBAD_SEED` if the caller explicitly requested a conflicting type.
 *  2. Explicit `passphrase`, `seed`, or `seed_hex` field, parsed via
 *     `getSeedFromRPC()`. The three fields are mutually exclusive.
 *  3. `randomSeed()` — used when no seed material is provided at all.
 *
 *  Key type defaults to `secp256k1` when unspecified.
 *
 *  On success the returned JSON object contains: `master_seed` (base58),
 *  `master_seed_hex`, `master_key` (RFC 1751 mnemonic), `account_id` (base58check
 *  address), `public_key` (base58 with AccountPublic prefix), `public_key_hex`,
 *  and `key_type`. A `warning` field is appended when the caller supplied a
 *  passphrase that is not itself an encoding of the seed: strong ("vulnerable to
 *  brute-force attacks") when Shannon-entropy estimate is below 80 bits, softer
 *  advisory otherwise.
 *
 *  @param params JSON object with optional fields: `key_type` ("secp256k1" or
 *      "ed25519"), `passphrase`, `seed`, `seed_hex`. Providing more than one of
 *      the three seed-input fields yields an error from `getSeedFromRPC()`.
 *  @return A JSON result object on success, or a JSON error object on failure.
 *  @note The passphrase entropy warning is suppressed when the passphrase value
 *      matches the seed's own base58, hex, or RFC 1751 encoding — the caller is
 *      presenting an existing seed as a string, not a brain-wallet phrase.
 *  @see `KeyGeneration_test.cpp` exercises this function directly with known
 *      constant vectors for both key types and both warning tiers.
 */
json::Value
walletPropose(json::Value const& params);

}  // namespace xrpl
