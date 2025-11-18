#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/LedgerToJson.h>
#include <xrpld/app/ledger/OpenLedger.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/paths/TrustLine.h>
#include <xrpld/app/rdb/RelationalDatabase.h>
#include <xrpld/app/tx/detail/NFTokenUtils.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/DeliveredAmount.h>
#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/nftPageMask.h>
#include <xrpl/resource/Fees.h>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>

namespace ripple {
namespace RPC {

std::optional<AccountID>
accountFromStringStrict(std::string const& account)
{
    std::optional<AccountID> result;

    auto const publicKey =
        parseBase58<PublicKey>(TokenType::AccountPublic, account);

    if (publicKey)
        result = calcAccountID(*publicKey);
    else
        result = parseBase58<AccountID>(account);

    return result;
}

error_code_i
accountFromStringWithCode(
    AccountID& result,
    std::string const& strIdent,
    bool bStrict)
{
    if (auto accountID = accountFromStringStrict(strIdent))
    {
        result = *accountID;
        return rpcSUCCESS;
    }

    if (bStrict)
        return rpcACT_MALFORMED;

    // We allow the use of the seeds which is poor practice
    // and merely for debugging convenience.
    auto const seed = parseGenericSeed(strIdent);

    if (!seed)
        return rpcBAD_SEED;

    auto const keypair = generateKeyPair(KeyType::secp256k1, *seed);

    result = calcAccountID(keypair.first);
    return rpcSUCCESS;
}

Json::Value
accountFromString(AccountID& result, std::string const& strIdent, bool bStrict)
{
    error_code_i code = accountFromStringWithCode(result, strIdent, bStrict);
    if (code != rpcSUCCESS)
        return rpcError(code);
    else
        return Json::objectValue;
}

std::uint64_t
getStartHint(std::shared_ptr<SLE const> const& sle, AccountID const& accountID)
{
    if (sle->getType() == ltRIPPLE_STATE)
    {
        if (sle->getFieldAmount(sfLowLimit).getIssuer() == accountID)
            return sle->getFieldU64(sfLowNode);
        else if (sle->getFieldAmount(sfHighLimit).getIssuer() == accountID)
            return sle->getFieldU64(sfHighNode);
    }

    if (!sle->isFieldPresent(sfOwnerNode))
        return 0;

    return sle->getFieldU64(sfOwnerNode);
}

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
    else if (sle->isFieldPresent(sfAccount))
    {
        // If there's an sfAccount present, also test the sfDestination, if
        // present. This will match objects such as Escrows (ltESCROW), Payment
        // Channels (ltPAYCHAN), and Checks (ltCHECK) because those are added to
        // the Destination account's directory. It intentionally EXCLUDES
        // NFToken Offers (ltNFTOKEN_OFFER). NFToken Offers are NOT added to the
        // Destination account's directory.
        return sle->getAccountID(sfAccount) == accountID ||
            (sle->isFieldPresent(sfDestination) &&
             sle->getAccountID(sfDestination) == accountID);
    }
    else if (sle->getType() == ltSIGNER_LIST)
    {
        Keylet const accountSignerList = keylet::signers(accountID);
        return sle->key() == accountSignerList.key;
    }
    else if (sle->getType() == ltNFTOKEN_OFFER)
    {
        // Do not check the sfDestination field. NFToken Offers are NOT added to
        // the Destination account's directory.
        return sle->getAccountID(sfOwner) == accountID;
    }

    return false;
}

hash_set<AccountID>
parseAccountIds(Json::Value const& jvArray)
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

void
injectSLE(Json::Value& jv, SLE const& sle)
{
    jv = sle.getJson(JsonOptions::none);
    if (sle.getType() == ltACCOUNT_ROOT)
    {
        if (sle.isFieldPresent(sfEmailHash))
        {
            auto const& hash = sle.getFieldH128(sfEmailHash);
            Blob const b(hash.begin(), hash.end());
            std::string md5 = strHex(makeSlice(b));
            boost::to_lower(md5);
            // VFALCO TODO Give a name and move this constant
            //             to a more visible location. Also
            //             shouldn't this be https?
            jv[jss::urlgravatar] =
                str(boost::format("http://www.gravatar.com/avatar/%s") % md5);
        }
    }
    else
    {
        jv[jss::Invalid] = true;
    }
}

std::optional<Json::Value>
readLimitField(
    unsigned int& limit,
    Tuning::LimitRange const& range,
    JsonContext const& context)
{
    limit = range.rdefault;
    if (!context.params.isMember(jss::limit) ||
        context.params[jss::limit].isNull())
        return std::nullopt;

    auto const& jvLimit = context.params[jss::limit];
    if (!(jvLimit.isUInt() || (jvLimit.isInt() && jvLimit.asInt() >= 0)))
        return RPC::expected_field_error(jss::limit, "unsigned integer");

    limit = jvLimit.asUInt();
    if (limit == 0)
        return RPC::invalid_field_error(jss::limit);

    if (!isUnlimited(context.role))
        limit = std::max(range.rmin, std::min(range.rmax, limit));

    return std::nullopt;
}

std::optional<Seed>
parseRippleLibSeed(Json::Value const& value)
{
    // ripple-lib encodes seed used to generate an Ed25519 wallet in a
    // non-standard way. While rippled never encode seeds that way, we
    // try to detect such keys to avoid user confusion.
    if (!value.isString())
        return std::nullopt;

    auto const result = decodeBase58Token(value.asString(), TokenType::None);

    if (result.size() == 18 &&
        static_cast<std::uint8_t>(result[0]) == std::uint8_t(0xE1) &&
        static_cast<std::uint8_t>(result[1]) == std::uint8_t(0x4B))
        return Seed(makeSlice(result.substr(2)));

    return std::nullopt;
}

std::optional<Seed>
getSeedFromRPC(Json::Value const& params, Json::Value& error)
{
    using string_to_seed_t =
        std::function<std::optional<Seed>(std::string const&)>;
    using seed_match_t = std::pair<char const*, string_to_seed_t>;

    static seed_match_t const seedTypes[]{
        {jss::passphrase.c_str(),
         [](std::string const& s) { return parseGenericSeed(s); }},
        {jss::seed.c_str(),
         [](std::string const& s) { return parseBase58<Seed>(s); }},
        {jss::seed_hex.c_str(), [](std::string const& s) {
             uint128 i;
             if (i.parseHex(s))
                 return std::optional<Seed>(Slice(i.data(), i.size()));
             return std::optional<Seed>{};
         }}};

    // Identify which seed type is in use.
    seed_match_t const* seedType = nullptr;
    int count = 0;
    for (auto const& t : seedTypes)
    {
        if (params.isMember(t.first))
        {
            ++count;
            seedType = &t;
        }
    }

    if (count != 1)
    {
        error = RPC::make_param_error(
            "Exactly one of the following must be specified: " +
            std::string(jss::passphrase) + ", " + std::string(jss::seed) +
            " or " + std::string(jss::seed_hex));
        return std::nullopt;
    }

    // Make sure a string is present
    auto const& param = params[seedType->first];
    if (!param.isString())
    {
        error = RPC::expected_field_error(seedType->first, "string");
        return std::nullopt;
    }

    auto const fieldContents = param.asString();

    // Convert string to seed.
    std::optional<Seed> seed = seedType->second(fieldContents);

    if (!seed)
        error = rpcError(rpcBAD_SEED);

    return seed;
}

std::optional<std::pair<PublicKey, SecretKey>>
keypairForSignature(
    Json::Value const& params,
    Json::Value& error,
    unsigned int apiVersion)
{
    bool const has_key_type = params.isMember(jss::key_type);

    // All of the secret types we allow, but only one at a time.
    static char const* const secretTypes[]{
        jss::passphrase.c_str(),
        jss::secret.c_str(),
        jss::seed.c_str(),
        jss::seed_hex.c_str()};

    // Identify which secret type is in use.
    char const* secretType = nullptr;
    int count = 0;
    for (auto t : secretTypes)
    {
        if (params.isMember(t))
        {
            ++count;
            secretType = t;
        }
    }

    if (count == 0 || secretType == nullptr)
    {
        error = RPC::missing_field_error(jss::secret);
        return {};
    }

    if (count > 1)
    {
        error = RPC::make_param_error(
            "Exactly one of the following must be specified: " +
            std::string(jss::passphrase) + ", " + std::string(jss::secret) +
            ", " + std::string(jss::seed) + " or " +
            std::string(jss::seed_hex));
        return {};
    }

    std::optional<KeyType> keyType;
    std::optional<Seed> seed;

    if (has_key_type)
    {
        if (!params[jss::key_type].isString())
        {
            error = RPC::expected_field_error(jss::key_type, "string");
            return {};
        }

        keyType = keyTypeFromString(params[jss::key_type].asString());

        if (!keyType)
        {
            if (apiVersion > 1u)
                error = RPC::make_error(rpcBAD_KEY_TYPE);
            else
                error = RPC::invalid_field_error(jss::key_type);
            return {};
        }

        // using strcmp as pointers may not match (see
        // https://developercommunity.visualstudio.com/t/assigning-constexpr-char--to-static-cha/10021357?entry=problem)
        if (strcmp(secretType, jss::secret.c_str()) == 0)
        {
            error = RPC::make_param_error(
                "The secret field is not allowed if " +
                std::string(jss::key_type) + " is used.");
            return {};
        }
    }

    // ripple-lib encodes seed used to generate an Ed25519 wallet in a
    // non-standard way. While we never encode seeds that way, we try
    // to detect such keys to avoid user confusion.
    // using strcmp as pointers may not match (see
    // https://developercommunity.visualstudio.com/t/assigning-constexpr-char--to-static-cha/10021357?entry=problem)
    if (strcmp(secretType, jss::seed_hex.c_str()) != 0)
    {
        seed = RPC::parseRippleLibSeed(params[secretType]);

        if (seed)
        {
            // If the user passed in an Ed25519 seed but *explicitly*
            // requested another key type, return an error.
            if (keyType.value_or(KeyType::ed25519) != KeyType::ed25519)
            {
                error = RPC::make_error(
                    rpcBAD_SEED, "Specified seed is for an Ed25519 wallet.");
                return {};
            }

            keyType = KeyType::ed25519;
        }
    }

    if (!keyType)
        keyType = KeyType::secp256k1;

    if (!seed)
    {
        if (has_key_type)
            seed = getSeedFromRPC(params, error);
        else
        {
            if (!params[jss::secret].isString())
            {
                error = RPC::expected_field_error(jss::secret, "string");
                return {};
            }

            seed = parseGenericSeed(params[jss::secret].asString());
        }
    }

    if (!seed)
    {
        if (!contains_error(error))
        {
            error = RPC::make_error(
                rpcBAD_SEED, RPC::invalid_field_message(secretType));
        }

        return {};
    }

    if (keyType != KeyType::secp256k1 && keyType != KeyType::ed25519)
        LogicError("keypairForSignature: invalid key type");

    return generateKeyPair(*keyType, *seed);
}

std::pair<RPC::Status, LedgerEntryType>
chooseLedgerEntryType(Json::Value const& params)
{
    std::pair<RPC::Status, LedgerEntryType> result{RPC::Status::OK, ltANY};
    if (params.isMember(jss::type))
    {
        static constexpr auto types = std::to_array<
            std::tuple<char const*, char const*, LedgerEntryType>>({
#pragma push_macro("LEDGER_ENTRY")
#undef LEDGER_ENTRY

#define LEDGER_ENTRY(tag, value, name, rpcName, ...) \
    {jss::name, jss::rpcName, tag},

#include <xrpl/protocol/detail/ledger_entries.macro>

#undef LEDGER_ENTRY
#pragma pop_macro("LEDGER_ENTRY")
        });

        auto const& p = params[jss::type];
        if (!p.isString())
        {
            result.first = RPC::Status{
                rpcINVALID_PARAMS, "Invalid field 'type', not string."};
            XRPL_ASSERT(
                result.first.type() == RPC::Status::Type::error_code_i,
                "ripple::RPC::chooseLedgerEntryType : first valid result type");
            return result;
        }

        // Use the passed in parameter to find a ledger type based on matching
        // against the canonical name (case-insensitive) or the RPC name
        // (case-sensitive).
        auto const filter = p.asString();
        auto const iter =
            std::ranges::find_if(types, [&filter](decltype(types.front())& t) {
                return boost::iequals(std::get<0>(t), filter) ||
                    std::get<1>(t) == filter;
            });
        if (iter == types.end())
        {
            result.first =
                RPC::Status{rpcINVALID_PARAMS, "Invalid field 'type'."};
            XRPL_ASSERT(
                result.first.type() == RPC::Status::Type::error_code_i,
                "ripple::RPC::chooseLedgerEntryType : second valid result "
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

}  // namespace RPC
}  // namespace ripple
