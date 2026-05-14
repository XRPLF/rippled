/** @file
 *  Implements the `validation_create` admin RPC command, which generates a
 *  secp256k1 key pair for use as a validator's signing identity.
 */

#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/tokens.h>

#include <optional>

namespace xrpl {

/** Resolve the seed for validator key generation from RPC params.
 *
 *  If `params` contains a `secret` field, interprets it via
 *  `parseGenericSeed()`, which accepts Base58 family seeds, passphrase
 *  strings (hashed via SHA-512 Half to 128 bits), and RFC 1751 mnemonic
 *  word lists. If `secret` is absent, generates 128 bits of
 *  cryptographically secure random entropy via `randomSeed()`.
 *
 *  @param params The JSON request parameters object.
 *  @return The resolved `Seed` on success, or `std::nullopt` if `secret`
 *      was provided but could not be parsed by `parseGenericSeed()`.
 *  @note The `Seed` destructor zeroes its 16-byte buffer, so key material
 *      does not linger in memory after the returned object is destroyed.
 */
static std::optional<Seed>
validationSeed(json::Value const& params)
{
    if (!params.isMember(jss::secret))
        return randomSeed();

    return parseGenericSeed(params[jss::secret].asString());
}

/** Handle the `validation_create` RPC command.
 *
 *  Generates a secp256k1 key pair suitable for use as a validator's signing
 *  identity. The returned object contains four fields covering the full range
 *  of operational needs:
 *
 *  - `validation_public_key` — Base58 `NodePublic` token; published in UNLs
 *    and trusted by peers during consensus.
 *  - `validation_private_key` — Base58 `NodePrivate` token; stored locally
 *    to sign validation messages.
 *  - `validation_seed` — Base58 `FamilySeed` token; used in the
 *    `[validation_seed]` config entry (older deployment style).
 *  - `validation_key` — RFC 1751 mnemonic; human-readable seed backup.
 *
 *  When `secret` is omitted, a random seed is generated. When `secret` is
 *  provided, it is parsed via `parseGenericSeed()` (Base58, passphrase, or
 *  RFC 1751). The `NodePublic`/`NodePrivate` token types are structurally
 *  distinct from `AccountPublic`/`AccountPrivate`, making it impossible at
 *  the Base58 layer to confuse a validator key for an account key.
 *
 *  @param context The RPC dispatch context; `context.params` may contain an
 *      optional `secret` field (string).
 *  @return A JSON object with the four key-material fields on success, or an
 *      `rpcBAD_SEED` error object if the supplied `secret` could not be
 *      parsed.
 *  @note This command is restricted to `Role::ADMIN`. Asking an untrusted
 *      server to generate a validator seed would allow that server to log
 *      or substitute a seed it controls, subverting the validator's identity.
 *  @see doWalletPropose() for account key generation (supports ed25519,
 *      entropy warnings, and account ID derivation).
 */
json::Value
doValidationCreate(RPC::JsonContext& context)
{
    json::Value obj(json::ValueType::Object);

    auto seed = validationSeed(context.params);

    if (!seed)
        return rpcError(RpcBadSeed);

    auto const privateKey = generateSecretKey(KeyType::Secp256k1, *seed);

    obj[jss::validation_public_key] =
        toBase58(TokenType::NodePublic, derivePublicKey(KeyType::Secp256k1, privateKey));

    obj[jss::validation_private_key] = toBase58(TokenType::NodePrivate, privateKey);

    obj[jss::validation_seed] = toBase58(*seed);
    obj[jss::validation_key] = seedAs1751(*seed);

    return obj;
}

}  // namespace xrpl
