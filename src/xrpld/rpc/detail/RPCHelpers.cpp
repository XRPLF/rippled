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
#include <optional>
#include <tuple>
#include <utility>

namespace xrpl::RPC {

std::uint64_t
getStartHint(SLE::const_ref sle, AccountID const& accountID)
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

bool
isRelatedToAccount(ReadView const& ledger, SLE::const_ref sle, AccountID const& accountID)
{
    if (sle->getType() == ltRIPPLE_STATE)
    {
        return (sle->getFieldAmount(sfLowLimit).getIssuer() == accountID) ||
            (sle->getFieldAmount(sfHighLimit).getIssuer() == accountID);
    }
    if (sle->isFieldPresent(sfAccount))
    {
        // If there's an sfAccount present, also test the sfDestination, if
        // present. This will match objects such as Escrows (ltESCROW), Payment
        // Channels (ltPAYCHAN), and Checks (ltCHECK) because those are added to
        // the Destination account's directory. It intentionally EXCLUDES
        // NFToken Offers (ltNFTOKEN_OFFER). NFToken Offers are NOT added to the
        // Destination account's directory.
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
        // Do not check the sfDestination field. NFToken Offers are NOT added to
        // the Destination account's directory.
        return sle->getAccountID(sfOwner) == accountID;
    }

    return false;
}

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

std::optional<Seed>
parseXrplLibSeed(json::Value const& value)
{
    // XrplLib encodes seed used to generate an Ed25519 wallet in a
    // non-standard way. While xrpld never encode seeds that way, we
    // try to detect such keys to avoid user confusion.
    if (!value.isString())
        return std::nullopt;

    auto const result = decodeBase58Token(value.asString(), TokenType::None);

    if (result.size() == 18 && static_cast<std::uint8_t>(result[0]) == std::uint8_t(0xE1) &&
        static_cast<std::uint8_t>(result[1]) == std::uint8_t(0x4B))
        return Seed(makeSlice(result.substr(2)));

    return std::nullopt;
}

std::optional<Seed>
getSeedFromRPC(json::Value const& params, json::Value& error)
{
    using string_to_seed_t = std::function<std::optional<Seed>(std::string const&)>;
    using seed_match_t = std::pair<char const*, string_to_seed_t>;

    static seed_match_t const kSeedTypes[]{
        {jss::passphrase.cStr(), [](std::string const& s) { return parseGenericSeed(s); }},
        {jss::seed.cStr(), [](std::string const& s) { return parseBase58<Seed>(s); }},
        {jss::seed_hex.cStr(), [](std::string const& s) {
             uint128 i;
             if (i.parseHex(s))
                 return std::optional<Seed>(Slice(i.data(), i.size()));
             return std::optional<Seed>{};
         }}};

    // Identify which seed type is in use.
    seed_match_t const* seedType = nullptr;
    int count = 0;
    for (auto const& t : kSeedTypes)
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

    // Make sure a string is present
    auto const& param = params[seedType->first];
    if (!param.isString())
    {
        error = RPC::expectedFieldError(seedType->first, "string");
        return std::nullopt;
    }

    auto const fieldContents = param.asString();

    // Convert string to seed.
    std::optional<Seed> seed = seedType->second(fieldContents);

    if (!seed)
        error = rpcError(RpcBadSeed);

    return seed;
}

std::optional<std::pair<PublicKey, SecretKey>>
keypairForSignature(json::Value const& params, json::Value& error, unsigned int apiVersion)
{
    bool const hasKeyType = params.isMember(jss::key_type);

    // All of the secret types we allow, but only one at a time.
    static char const* const kSecretTypes[]{
        jss::passphrase.cStr(), jss::secret.cStr(), jss::seed.cStr(), jss::seed_hex.cStr()};

    // Identify which secret type is in use.
    char const* secretType = nullptr;
    int count = 0;
    for (auto t : kSecretTypes)
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

        // using strcmp as pointers may not match (see
        // https://developercommunity.visualstudio.com/t/assigning-constexpr-char--to-static-cha/10021357?entry=problem)
        if (strcmp(secretType, jss::secret.cStr()) == 0)
        {
            error = RPC::makeParamError(
                "The secret field is not allowed if " + std::string(jss::key_type) + " is used.");
            return {};
        }
    }

    // XrplLib encodes seed used to generate an Ed25519 wallet in a
    // non-standard way. While we never encode seeds that way, we try
    // to detect such keys to avoid user confusion.
    // using strcmp as pointers may not match (see
    // https://developercommunity.visualstudio.com/t/assigning-constexpr-char--to-static-cha/10021357?entry=problem)
    if (strcmp(secretType, jss::seed_hex.cStr()) != 0)
    {
        seed = RPC::parseXrplLibSeed(params[secretType]);

        if (seed)
        {
            // If the user passed in an Ed25519 seed but *explicitly*
            // requested another key type, return an error.
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

std::pair<RPC::Status, LedgerEntryType>
chooseLedgerEntryType(json::Value const& params)
{
    std::pair<RPC::Status, LedgerEntryType> result{RPC::Status::kOK, ltANY};
    if (params.isMember(jss::type))
    {
        static constexpr auto kTypes =
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

        // Use the passed in parameter to find a ledger type based on matching
        // against the canonical name (case-insensitive) or the RPC name
        // (case-sensitive).
        auto const filter = p.asString();
        auto const iter = std::ranges::find_if(kTypes, [&filter](decltype(kTypes.front())& t) {
            return boost::iequals(std::get<0>(t), filter) || std::get<1>(t) == filter;
        });
        if (iter == kTypes.end())
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
        // Parse mandatory currency.
        if (!jv.isMember(jss::currency) ||
            !toCurrency(issue.currency, jv[jss::currency].asString()))
        {
            JLOG(j.info()) << boost::format("Bad %s currency.") % name.cStr();
            return assetError;
        }

        // Parse optional issuer.
        if (((jv.isMember(jss::issuer)) &&
             (!jv[jss::issuer].isString() || !toIssuer(issue.account, jv[jss::issuer].asString())))
            // Don't allow illegal issuers.
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
