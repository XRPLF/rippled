/** @file
 *  Utility belt for the XRPL RPC layer.
 *
 *  Centralises cross-cutting concerns shared by nearly every paginated or
 *  cryptographic RPC handler: ledger object ownership tests (`getStartHint`,
 *  `isRelatedToAccount`), pagination limit management (`readLimitField`), seed
 *  and keypair derivation (`parseXrplLibSeed`, `getSeedFromRPC`,
 *  `keypairForSignature`), ledger entry type selection
 *  (`chooseLedgerEntryType`, `isAccountObjectsValidType`), and order-book
 *  asset parsing (`parseSubUnsubJson`).
 */
#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/DeliveredAmount.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/Status.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/tokens.h>

#include <boost/algorithm/string/predicate.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>

namespace xrpl::RPC {

/** Extract the owner-directory node index used to resume paginated enumeration.
 *
 *  For trust lines (`ltRIPPLE_STATE`), the SLE records two node indices — one
 *  for the low-limit party (`sfLowNode`) and one for the high-limit party
 *  (`sfHighNode`).  The correct side is selected by comparing each limit's
 *  issuer against `accountID`.  All other account-owned object types store a
 *  single `sfOwnerNode`; if that field is absent the function returns 0,
 *  meaning "start from the beginning of the directory".
 *
 *  @param sle The ledger entry at the pagination marker position.
 *  @param accountID The account whose owner directory is being traversed.
 *  @return The 64-bit directory-node hint, or 0 if none is available.
 */
std::uint64_t
getStartHint(std::shared_ptr<SLE const> const& sle, AccountID const& accountID)
{
    if (sle->getType() == ltRIPPLE_STATE)
    {
        if (sle->getFieldAmount(sfLowLimit).getIssuer() == accountID)
        {
            return sle->getFieldU64(sfLowNode);
        }
        if (sle->getFieldAmount(sfHighLimit).getIssuer() == accountID)
        {
            return sle->getFieldU64(sfHighNode);
        }
    }

    if (!sle->isFieldPresent(sfOwnerNode))
        return 0;

    return sle->getFieldU64(sfOwnerNode);
}

/** Test whether a ledger entry belongs to a given account's owner directory.
 *
 *  The check is type-aware because different SLE types encode ownership
 *  differently:
 *
 *  - **Trust lines** (`ltRIPPLE_STATE`): owned by both parties; returns true
 *      for either the low-limit or high-limit issuer.
 *  - **Objects with `sfAccount`**: owned by that account.  Escrows, payment
 *      channels, and checks additionally appear in the *destination* account's
 *      directory, so `sfDestination` is tested as a fallback when present.
 *  - **NFToken Offers** (`ltNFTOKEN_OFFER`): only `sfOwner` is tested.
 *      NFToken Offers are **not** added to the destination's directory, so
 *      `sfDestination` must not be used here even though the field exists.
 *  - **Signer lists** (`ltSIGNER_LIST`): identified by comparing the SLE key
 *      against `keylet::signers(accountID)` — no `sfAccount` field is present.
 *
 *  @param ledger The ledger view (unused at runtime but required by the
 *      interface for consistency with other ownership predicates).
 *  @param sle The ledger entry to test.
 *  @param accountID The account to test ownership against.
 *  @return `true` if the entry appears in `accountID`'s owner directory.
 *  @note This function is the ownership gate for `account_objects` pagination.
 *      Always call it before resuming a paginated walk to prevent
 *      cross-account directory traversal with a forged marker.
 */
bool
isRelatedToAccount(
    ReadView const& ledger,
    std::shared_ptr<SLE const> const& sle,
    AccountID const& accountID)
{
    if (sle->getType() == ltRIPPLE_STATE)
    {
        return (sle->getFieldAmount(sfLowLimit).getIssuer() == accountID) ||
            (sle->getFieldAmount(sfHighLimit).getIssuer() == accountID);
    }
    if (sle->isFieldPresent(sfAccount))
    {
        return sle->getAccountID(sfAccount) == accountID ||
            (sle->isFieldPresent(sfDestination) && sle->getAccountID(sfDestination) == accountID);
    }
    if (sle->getType() == ltSIGNER_LIST)
    {
        Keylet const accountSignerList = keylet::signers(accountID);
        return sle->key() == accountSignerList.key;
    }
    if (sle->getType() == ltNFTOKEN_OFFER)
    {
        return sle->getAccountID(sfOwner) == accountID;
    }

    return false;
}

/** Decode a JSON array of Base58-encoded account addresses into a set of IDs.
 *
 *  Iterates the array and parses each element as a Base58Check-encoded
 *  `AccountID`.  On the first element that is not a string or cannot be
 *  decoded, the function returns an **empty** set — callers treat an empty
 *  result as a parse failure and return `rpcACT_MALFORMED`.
 *
 *  @param jvArray A JSON array whose elements are Base58-encoded account
 *      address strings.
 *  @return The decoded IDs, or an empty set if any element is invalid.
 */
hash_set<AccountID>
parseAccountIds(json::Value const& jvArray)
{
    hash_set<AccountID> result;
    for (auto const& jv : jvArray)
    {
        if (!jv.isString())
            return hash_set<AccountID>();
        auto const id = parseBase58<AccountID>(jv.asString());
        if (!id)
            return hash_set<AccountID>();
        result.insert(*id);
    }
    return result;
}

/** Read and clamp the `limit` field from an RPC request.
 *
 *  Applies the per-command `Tuning::LimitRange`: if `limit` is absent or
 *  null, `range.rDefault` is used.  For non-admin connections
 *  (`!isUnlimited(context.role)`), the value is clamped to
 *  `[range.rmin, range.rmax]`, preventing both accidental and malicious
 *  bandwidth exhaustion.  Admin / unlimited connections bypass clamping and
 *  may request arbitrarily large result sets.
 *
 *  @param limit Output parameter populated with the resolved limit value.
 *  @param range The allowed range and default for this command.
 *  @param context The RPC context supplying `params` and `role`.
 *  @return `std::nullopt` on success; a JSON error object if the field is
 *      present but not a non-negative integer or equals zero.
 */
std::optional<json::Value>
readLimitField(unsigned int& limit, Tuning::LimitRange const& range, JsonContext const& context)
{
    limit = range.rDefault;
    if (!context.params.isMember(jss::limit) || context.params[jss::limit].isNull())
        return std::nullopt;

    auto const& jvLimit = context.params[jss::limit];
    if (!jvLimit.isUInt() && (!jvLimit.isInt() || jvLimit.asInt() < 0))
        return RPC::expectedFieldError(jss::limit, "unsigned integer");

    limit = jvLimit.asUInt();
    if (limit == 0)
        return RPC::invalidFieldError(jss::limit);

    if (!isUnlimited(context.role))
        limit = std::max(range.rmin, std::min(range.rmax, limit));

    return std::nullopt;
}

/** Detect and unwrap an xrpl.js-style Ed25519 seed.
 *
 *  The xrpl.js client library encodes Ed25519 seeds with a non-standard
 *  two-byte header (`0xE1 0x4B`) prepended to the 16-byte seed material,
 *  producing an 18-byte payload after Base58 decoding.  xrpld never emits
 *  seeds in this form, but silently recognises them so that users who
 *  copy-paste from a JavaScript wallet do not receive a confusing error.
 *
 *  @param value A JSON value expected to hold a Base58-encoded string.
 *  @return The unwrapped 16-byte `Seed` if the xrpl.js encoding is detected;
 *      `std::nullopt` otherwise (including when `value` is not a string).
 */
std::optional<Seed>
parseXrplLibSeed(json::Value const& value)
{
    if (!value.isString())
        return std::nullopt;

    auto const result = decodeBase58Token(value.asString(), TokenType::None);

    if (result.size() == 18 && static_cast<std::uint8_t>(result[0]) == std::uint8_t(0xE1) &&
        static_cast<std::uint8_t>(result[1]) == std::uint8_t(0x4B))
        return Seed(makeSlice(result.substr(2)));

    return std::nullopt;
}

/** Parse a seed from exactly one of `passphrase`, `seed`, or `seed_hex`.
 *
 *  Uses a static dispatch table of `(field name, parser lambda)` pairs.
 *  Counts how many of the three fields are present before invoking any
 *  parser, so the error message can list all valid alternatives when the
 *  count is wrong.
 *
 *  - `passphrase`: arbitrary string; decoded via `parseGenericSeed`.
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
getSeedFromRPC(json::Value const& params, json::Value& error)
{
    using string_to_seed_t = std::function<std::optional<Seed>(std::string const&)>;
    using seed_match_t = std::pair<char const*, string_to_seed_t>;

    static seed_match_t const kSEED_TYPES[]{
        {jss::passphrase.cStr(), [](std::string const& s) { return parseGenericSeed(s); }},
        {jss::seed.cStr(), [](std::string const& s) { return parseBase58<Seed>(s); }},
        {jss::seed_hex.cStr(), [](std::string const& s) {
             uint128 i;
             if (i.parseHex(s))
                 return std::optional<Seed>(Slice(i.data(), i.size()));
             return std::optional<Seed>{};
         }}};

    seed_match_t const* seedType = nullptr;
    int count = 0;
    for (auto const& t : kSEED_TYPES)
    {
        if (params.isMember(t.first))
        {
            ++count;
            seedType = &t;
        }
    }

    if (count != 1)
    {
        error = RPC::makeParamError(
            "Exactly one of the following must be specified: " + std::string(jss::passphrase) +
            ", " + std::string(jss::seed) + " or " + std::string(jss::seed_hex));
        return std::nullopt;
    }

    auto const& param = params[seedType->first];
    if (!param.isString())
    {
        error = RPC::expectedFieldError(seedType->first, "string");
        return std::nullopt;
    }

    auto const fieldContents = param.asString();

    std::optional<Seed> seed = seedType->second(fieldContents);

    if (!seed)
        error = rpcError(RpcBadSeed);

    return seed;
}

/** Derive a signing keypair from RPC credential parameters.
 *
 *  Reconciles several overlapping input conventions:
 *
 *  1. **Legacy `secret` path** — a bare string seed implying secp256k1; mutually
 *     exclusive with `key_type`.
 *  2. **Structured path** — one of `passphrase`, `seed`, or `seed_hex` combined
 *     with an explicit `key_type` (`secp256k1` or `ed25519`).
 *  3. **xrpl.js Ed25519 encoding** — a non-standard 18-byte Base58 payload with
 *     header `0xE1 0x4B`; auto-detected via `parseXrplLibSeed` and forces
 *     `KeyType::Ed25519`.  If the xrpl.js seed is detected but `key_type`
 *     explicitly requests a different curve, the function returns an error.
 *
 *  Exactly one of `passphrase`, `secret`, `seed`, or `seed_hex` must appear.
 *  `key_type` is optional but, when present, forbids the `secret` field.
 *
 *  @param params The JSON request object containing credential fields.
 *  @param error Populated with a structured error value on failure; API v2+
 *      receives `rpcBAD_KEY_TYPE` for unrecognised key types, while v1 receives
 *      the older `invalid_field_error` form.
 *  @param apiVersion The negotiated API version; governs error code selection.
 *  @return The `(PublicKey, SecretKey)` pair, or `std::nullopt` on any error.
 */
std::optional<std::pair<PublicKey, SecretKey>>
keypairForSignature(json::Value const& params, json::Value& error, unsigned int apiVersion)
{
    bool const hasKeyType = params.isMember(jss::key_type);

    static char const* const kSECRET_TYPES[]{
        jss::passphrase.cStr(), jss::secret.cStr(), jss::seed.cStr(), jss::seed_hex.cStr()};

    char const* secretType = nullptr;
    int count = 0;
    for (auto t : kSECRET_TYPES)
    {
        if (params.isMember(t))
        {
            ++count;
            secretType = t;
        }
    }

    if (count == 0 || secretType == nullptr)
    {
        error = RPC::missingFieldError(jss::secret);
        return {};
    }

    if (count > 1)
    {
        error = RPC::makeParamError(
            "Exactly one of the following must be specified: " + std::string(jss::passphrase) +
            ", " + std::string(jss::secret) + ", " + std::string(jss::seed) + " or " +
            std::string(jss::seed_hex));
        return {};
    }

    std::optional<KeyType> keyType;
    std::optional<Seed> seed;

    if (hasKeyType)
    {
        if (!params[jss::key_type].isString())
        {
            error = RPC::expectedFieldError(jss::key_type, "string");
            return {};
        }

        keyType = keyTypeFromString(params[jss::key_type].asString());

        if (!keyType)
        {
            if (apiVersion > 1u)
            {
                error = RPC::makeError(RpcBadKeyType);
            }
            else
            {
                error = RPC::invalidFieldError(jss::key_type);
            }
            return {};
        }

        // using strcmp because pointer equality on jss:: constants can fail under
        // MSVC (https://developercommunity.visualstudio.com/t/assigning-constexpr-char--to-static-cha/10021357)
        if (strcmp(secretType, jss::secret.cStr()) == 0)
        {
            error = RPC::makeParamError(
                "The secret field is not allowed if " + std::string(jss::key_type) + " is used.");
            return {};
        }
    }

    // using strcmp because pointer equality on jss:: constants can fail under
    // MSVC (https://developercommunity.visualstudio.com/t/assigning-constexpr-char--to-static-cha/10021357)
    if (strcmp(secretType, jss::seed_hex.cStr()) != 0)
    {
        seed = RPC::parseXrplLibSeed(params[secretType]);

        if (seed)
        {
            if (keyType.value_or(KeyType::Ed25519) != KeyType::Ed25519)
            {
                error = RPC::makeError(RpcBadSeed, "Specified seed is for an Ed25519 wallet.");
                return {};
            }

            keyType = KeyType::Ed25519;
        }
    }

    if (!keyType)
        keyType = KeyType::Secp256k1;

    if (!seed)
    {
        if (hasKeyType)
        {
            seed = getSeedFromRPC(params, error);
        }
        else
        {
            if (!params[jss::secret].isString())
            {
                error = RPC::expectedFieldError(jss::secret, "string");
                return {};
            }

            seed = parseGenericSeed(params[jss::secret].asString());
        }
    }

    if (!seed)
    {
        if (!containsError(error))
        {
            error = RPC::makeError(RpcBadSeed, RPC::invalidFieldMessage(secretType));
        }

        return {};
    }

    if (keyType != KeyType::Secp256k1 && keyType != KeyType::Ed25519)
        logicError("keypairForSignature: invalid key type");

    return generateKeyPair(*keyType, *seed);
}

/** Map an RPC `type` string to the corresponding `LedgerEntryType`.
 *
 *  The lookup table is built at compile time by expanding
 *  `ledger_entries.macro` via the X-macro pattern.  Each entry exposes both a
 *  canonical name (e.g. `"RippleState"`) and an RPC name (e.g. `"state"`).
 *  Matching accepts either form: the canonical name is compared
 *  **case-insensitively** (so `"ripplestate"` works); the RPC name is compared
 *  **case-sensitively**.
 *
 *  If the `type` field is absent, `ltANY` is returned, signalling that all
 *  object types should be included.  Newly added ledger entry types become
 *  automatically matchable without any change here — only the macro file needs
 *  updating.
 *
 *  @param params The JSON request object; may contain an optional `"type"`
 *      string field.
 *  @return A pair of `(Status, LedgerEntryType)`.  On success the status is
 *      `kOK`; on failure it carries `rpcINVALID_PARAMS` and the type is
 *      `ltANY` (0).
 *  @see isAccountObjectsValidType
 */
std::pair<RPC::Status, LedgerEntryType>
chooseLedgerEntryType(json::Value const& params)
{
    std::pair<RPC::Status, LedgerEntryType> result{RPC::Status::kOK, ltANY};
    if (params.isMember(jss::type))
    {
        static constexpr auto kTYPES =
            std::to_array<std::tuple<char const*, char const*, LedgerEntryType>>({
#pragma push_macro("LEDGER_ENTRY")
#undef LEDGER_ENTRY

#define LEDGER_ENTRY(tag, value, name, rpcName, ...) {jss::name, jss::rpcName, tag},

#include <xrpl/protocol/detail/ledger_entries.macro>

#undef LEDGER_ENTRY
#pragma pop_macro("LEDGER_ENTRY")
            });

        auto const& p = params[jss::type];
        if (!p.isString())
        {
            result.first = RPC::Status{RpcInvalidParams, "Invalid field 'type', not string."};
            XRPL_ASSERT(
                result.first.type() == RPC::Status::Type::ErrorCodeI,
                "xrpl::RPC::chooseLedgerEntryType : first valid result type");
            return result;
        }

        auto const filter = p.asString();
        auto const iter = std::ranges::find_if(kTYPES, [&filter](decltype(kTYPES.front())& t) {
            return boost::iequals(std::get<0>(t), filter) || std::get<1>(t) == filter;
        });
        if (iter == kTYPES.end())
        {
            result.first = RPC::Status{RpcInvalidParams, "Invalid field 'type'."};
            XRPL_ASSERT(
                result.first.type() == RPC::Status::Type::ErrorCodeI,
                "xrpl::RPC::chooseLedgerEntryType : second valid result "
                "type");
            return result;
        }
        result.second = std::get<2>(*iter);
    }
    return result;
}

/** Guard `account_objects` against global consensus types that are never
 *  owned by an account.
 *
 *  Returns `false` for `ltAMENDMENTS`, `ltDIR_NODE`, `ltFEE_SETTINGS`,
 *  `ltLEDGER_HASHES`, and `ltNEGATIVE_UNL` — types that live in the ledger
 *  but cannot appear in any account's owner directory.  All other types
 *  (including future ones) return `true`; the allow-list-by-exclusion design
 *  means newly added account-owned types are valid without any change here.
 *
 *  @param type The `LedgerEntryType` to validate.
 *  @return `false` if `type` can never be owned by an account; `true`
 *      otherwise.
 *  @see chooseLedgerEntryType
 */
bool
isAccountObjectsValidType(LedgerEntryType const& type)
{
    switch (type)
    {
        case LedgerEntryType::ltAMENDMENTS:
        case LedgerEntryType::ltDIR_NODE:
        case LedgerEntryType::ltFEE_SETTINGS:
        case LedgerEntryType::ltLEDGER_HASHES:
        case LedgerEntryType::ltNEGATIVE_UNL:
            return false;
        default:
            return true;
    }
}

/** Decode one side of an order-book subscription asset from JSON.
 *
 *  Parses the `taker_pays` or `taker_gets` object from a `subscribe` /
 *  `unsubscribe` request into an `Asset` (either an IOU `Issue` or an MPT
 *  issuance ID).  The `name` parameter drives field-specific error codes so
 *  callers can distinguish a malformed source asset from a malformed
 *  destination asset in user-facing responses:
 *
 *  - `taker_pays` errors → `RpcSrcIsrMalformed` / `RpcSrcCurMalformed`
 *  - `taker_gets` errors → `RpcDstIsrMalformed` / `RpcDstAmtMalformed`
 *
 *  For IOU assets, `currency` is mandatory and `issuer` is optional.  The
 *  issuer validation rejects XRP-currency + issuer combinations, non-string
 *  issuers, unparseable account IDs, and the `noAccount()` sentinel — any of
 *  these returns the issuer error code.  Presence of `mpt_issuance_id`
 *  alongside `currency` or `issuer` is explicitly rejected with
 *  `rpcINVALID_PARAMS`.
 *
 *  @param asset Output populated with the decoded asset on success.
 *  @param params The full JSON object containing the `name` sub-object.
 *  @param name Either `jss::taker_pays` or `jss::taker_gets`.
 *  @param j Journal for diagnostic logging on parse failures.
 *  @return `RpcSuccess` on success; an appropriate `ErrorCodeI` otherwise.
 */
ErrorCodeI
parseSubUnsubJson(
    Asset& asset,
    json::Value const& params,
    json::StaticString const& name,
    beast::Journal j)
{
    auto const& jv = params[name];
    auto const [issuerError, assetError] = [&]() {
        if (name == jss::taker_pays)
            return std::make_pair(RpcSrcIsrMalformed, RpcSrcCurMalformed);
        return std::make_pair(RpcDstIsrMalformed, RpcDstAmtMalformed);
    }();

    if (jv.isMember(jss::mpt_issuance_id) &&
        (jv.isMember(jss::currency) || jv.isMember(jss::issuer)))
    {
        JLOG(j.info()) << boost::format("Bad %s currency or MPT.") % name.cStr();
        return RpcInvalidParams;
    }

    if (jv.isMember(jss::currency))
    {
        Issue issue = xrpIssue();
        if (!jv.isMember(jss::currency) ||
            !toCurrency(issue.currency, jv[jss::currency].asString()))
        {
            JLOG(j.info()) << boost::format("Bad %s currency.") % name.cStr();
            return assetError;
        }

        // Reject: non-string issuer, bad account ID, noAccount() sentinel,
        // or currency/issuer XOR mismatch (e.g. XRP with an issuer).
        if (((jv.isMember(jss::issuer)) &&
             (!jv[jss::issuer].isString() || !toIssuer(issue.account, jv[jss::issuer].asString())))
            || (!issue.currency != !issue.account) || noAccount() == issue.account)
        {
            JLOG(j.info()) << boost::format("Bad %s issuer.") % name.cStr();
            return issuerError;
        }
        asset = issue;
    }
    else if (jv.isMember(jss::mpt_issuance_id))
    {
        MPTID mptid;
        if (!mptid.parseHex(jv[jss::mpt_issuance_id].asString()))
            return assetError;
        asset = mptid;
    }
    else
    {
        JLOG(j.info()) << boost::format("Neither %s currency or MPT is present.") % name.cStr();
        return assetError;
    }

    return RpcSuccess;
}

}  // namespace xrpl::RPC
