#pragma once

#include <xrpld/app/misc/TxQ.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Status.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/proto/org/xrpl/rpc/v1/xrp_ledger.pb.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/server/NetworkOPs.h>

#include <optional>

namespace xrpl {

class ReadView;

namespace RPC {

struct JsonContext;

/** Extract the owner-directory node index used to resume paginated enumeration.
 *
 *  For trust lines (`ltRIPPLE_STATE`), the SLE records two node indices — one
 *  for the low-limit party (`sfLowNode`) and one for the high-limit party
 *  (`sfHighNode`). The correct side is selected by comparing each limit's
 *  issuer against `accountID`. All other account-owned object types store a
 *  single `sfOwnerNode`; if that field is absent the function returns 0,
 *  meaning "start from the beginning of the directory".
 *
 *  @param sle The ledger entry at the pagination marker position.
 *  @param accountID The account whose owner directory is being traversed.
 *  @return The 64-bit directory-node hint, or 0 if none is available.
 */
std::uint64_t
getStartHint(std::shared_ptr<SLE const> const& sle, AccountID const& accountID);

/** Test whether a ledger entry belongs to a given account's owner directory.
 *
 *  The check is type-aware because different SLE types encode ownership
 *  differently:
 *
 *  - **Trust lines** (`ltRIPPLE_STATE`): owned by both parties; returns `true`
 *      for either the low-limit or high-limit issuer.
 *  - **Objects with `sfAccount`**: owned by that account. Escrows, payment
 *      channels, and checks additionally appear in the *destination* account's
 *      directory, so `sfDestination` is tested as a fallback when present.
 *  - **NFToken Offers** (`ltNFTOKEN_OFFER`): only `sfOwner` is tested.
 *      NFToken Offers are **not** added to the destination's directory, so
 *      `sfDestination` must not be used here even though the field exists.
 *  - **Signer lists** (`ltSIGNER_LIST`): identified by comparing the SLE key
 *      against `keylet::signers(accountID)` — no `sfAccount` field is present.
 *
 *  @param ledger The ledger view containing the entry (required for interface
 *      consistency with other ownership predicates).
 *  @param sle The ledger entry to test.
 *  @param accountID The account to test ownership against.
 *  @return `true` if the entry appears in `accountID`'s owner directory.
 *  @note This is the ownership gate for `account_objects` pagination. Always
 *      call it before resuming a paginated walk to prevent cross-account
 *      directory traversal with a forged marker.
 */
bool
isRelatedToAccount(
    ReadView const& ledger,
    std::shared_ptr<SLE const> const& sle,
    AccountID const& accountID);

/** Decode a JSON array of Base58-encoded account addresses into a set of IDs.
 *
 *  On the first element that is not a string or cannot be decoded as a
 *  Base58Check `AccountID`, the function returns an **empty** set. Callers
 *  treat an empty result as a parse failure and return `rpcACT_MALFORMED`.
 *
 *  @param jvArray A JSON array whose elements are Base58-encoded account
 *      address strings.
 *  @return The decoded IDs, or an empty set if any element is invalid.
 */
hash_set<AccountID>
parseAccountIds(json::Value const& jvArray);

/** Read and clamp the `limit` field from an RPC request.
 *
 *  Applies the per-command `Tuning::LimitRange`: if `limit` is absent or
 *  null, `range.rDefault` is used. For non-admin connections
 *  (`!isUnlimited(context.role)`), the value is clamped to
 *  `[range.rmin, range.rmax]`, preventing both accidental and malicious
 *  bandwidth exhaustion. Admin/unlimited connections bypass clamping and
 *  may request arbitrarily large result sets.
 *
 *  @param limit Output parameter populated with the resolved limit value.
 *  @param range The allowed range and default for this command.
 *  @param context The RPC context supplying `params` and `role`.
 *  @return `std::nullopt` on success; a JSON error object if the field is
 *      present but not a non-negative integer, or equals zero.
 *  @note The return convention is inverted relative to most `std::optional`
 *      uses: `std::nullopt` means success and a populated value is an error.
 *      This matches the error-composition pattern used throughout RPC handlers.
 */
std::optional<json::Value>
readLimitField(unsigned int& limit, Tuning::LimitRange const& range, JsonContext const& context);

/** Parse a seed from exactly one of `passphrase`, `seed`, or `seed_hex`.
 *
 *  Uses a static dispatch table of `(field name, parser)` pairs.  Exactly one
 *  of the three fields must be present; supplying zero or more than one is a
 *  hard parameter error that populates `error` and returns `std::nullopt`.
 *
 *  - `passphrase`: arbitrary string decoded via `parseGenericSeed`.
 *  - `seed`: Base58Check-encoded seed.
 *  - `seed_hex`: 32-hex-digit (128-bit) seed.
 *
 *  @param params The JSON request object.
 *  @param error Populated with a descriptive error when the return value is
 *      `std::nullopt`.
 *  @return The decoded `Seed`, or `std::nullopt` if the field is missing,
 *      ambiguous (more than one present), or malformed.
 */
std::optional<Seed>
getSeedFromRPC(json::Value const& params, json::Value& error);

/** Detect and unwrap an xrpl.js-style Ed25519 seed.
 *
 *  The xrpl.js client library encodes Ed25519 seeds with a non-standard
 *  two-byte header (`0xE1 0x4B`) prepended to the 16-byte seed material,
 *  producing an 18-byte payload after Base58 decoding. xrpld never emits
 *  seeds in this form, but silently recognises them so that users who
 *  copy-paste from a JavaScript wallet do not receive a confusing error.
 *
 *  @param params A JSON value expected to hold a Base58-encoded string.
 *  @return The unwrapped 16-byte `Seed` if the xrpl.js encoding is detected;
 *      `std::nullopt` otherwise (including when `params` is not a string).
 */
std::optional<Seed>
parseXrplLibSeed(json::Value const& params);

/** Map an RPC `type` string to the corresponding `LedgerEntryType`.
 *
 *  The lookup table is built at compile time by expanding
 *  `ledger_entries.macro` via the X-macro pattern. Each entry exposes both a
 *  canonical name (e.g. `"RippleState"`) and an RPC name (e.g. `"state"`).
 *  Matching accepts either form: the canonical name is compared
 *  **case-insensitively**; the RPC name is compared **case-sensitively**.
 *
 *  If the `type` field is absent, `ltANY` is returned, signalling that all
 *  object types should be included. Newly added ledger entry types registered
 *  in the macro become automatically matchable without any change here.
 *
 *  @param params The JSON request object; may contain an optional `"type"`
 *      string field.
 *  @return A pair of `(Status, LedgerEntryType)`. On success the status is
 *      `Status::kOK`; on failure it carries `rpcINVALID_PARAMS` and the
 *      type is `ltANY`.
 *  @see isAccountObjectsValidType
 */
std::pair<RPC::Status, LedgerEntryType>
chooseLedgerEntryType(json::Value const& params);

/** Guard `account_objects` against global ledger types that cannot be account-owned.
 *
 *  Returns `false` for `ltAMENDMENTS`, `ltDIR_NODE`, `ltFEE_SETTINGS`,
 *  `ltLEDGER_HASHES`, and `ltNEGATIVE_UNL` — types that live in the ledger
 *  but cannot appear in any account's owner directory. All other types
 *  (including future ones) return `true`; the deny-list design means newly
 *  added account-owned types are valid without any change here.
 *
 *  @param type The `LedgerEntryType` to validate.
 *  @return `false` if `type` can never be owned by an account; `true`
 *      otherwise.
 *  @see chooseLedgerEntryType
 */
bool
isAccountObjectsValidType(LedgerEntryType const& type);

/** Derive a signing keypair from RPC credential parameters.
 *
 *  Reconciles several overlapping input conventions:
 *
 *  1. **Legacy `secret` path** — a bare string seed implying secp256k1;
 *     mutually exclusive with `key_type`.
 *  2. **Structured path** — one of `passphrase`, `seed`, or `seed_hex`
 *     combined with an explicit `key_type` (`secp256k1` or `ed25519`).
 *  3. **xrpl.js Ed25519 encoding** — a non-standard 18-byte Base58 payload
 *     auto-detected via `parseXrplLibSeed`, which forces `KeyType::Ed25519`.
 *     If the xrpl.js seed is detected but `key_type` explicitly requests a
 *     different curve, an error is returned.
 *
 *  Exactly one of `passphrase`, `secret`, `seed`, or `seed_hex` must appear.
 *  `key_type` is optional but, when present, forbids the `secret` field.
 *
 *  @param params The JSON request object containing credential fields.
 *  @param error Populated with a structured error value on failure. API v2+
 *      receives `rpcBAD_KEY_TYPE` for unrecognised key types; v1 receives
 *      the older `invalid_field_error` form for backward compatibility.
 *  @param apiVersion The negotiated API version; governs error code selection.
 *  @return The `(PublicKey, SecretKey)` pair, or `std::nullopt` on any error.
 */
std::optional<std::pair<PublicKey, SecretKey>>
keypairForSignature(
    json::Value const& params,
    json::Value& error,
    unsigned int apiVersion = kAPI_VERSION_IF_UNSPECIFIED);

/** Decode one side of an order-book subscription asset from JSON.
 *
 *  Parses the `taker_pays` or `taker_gets` object from a `subscribe` /
 *  `unsubscribe` request into an `Asset` (either an IOU `Issue` or an MPT
 *  issuance ID). The `name` parameter drives field-specific error codes so
 *  callers can distinguish a malformed source asset from a malformed
 *  destination asset:
 *
 *  - `taker_pays` errors → `RpcSrcIsrMalformed` / `RpcSrcCurMalformed`
 *  - `taker_gets` errors → `RpcDstIsrMalformed` / `RpcDstAmtMalformed`
 *
 *  A caller may specify either a classic IOU via `currency`/`issuer` fields,
 *  or an MPT via `mpt_issuance_id`, but never both in the same object.
 *  Mixed parameters are rejected with `rpcINVALID_PARAMS`.
 *
 *  @param asset Output populated with the decoded asset on success.
 *  @param jv The JSON sub-object for the `name` side of the book.
 *  @param name Either `jss::taker_pays` or `jss::taker_gets`; selects the
 *      error codes emitted for each failure path.
 *  @param j Journal for diagnostic logging on parse failures.
 *  @return `RpcSuccess` on success; an appropriate `ErrorCodeI` otherwise.
 */
ErrorCodeI
parseSubUnsubJson(
    Asset& asset,
    json::Value const& jv,
    json::StaticString const& name,
    beast::Journal j);

}  // namespace RPC

}  // namespace xrpl
