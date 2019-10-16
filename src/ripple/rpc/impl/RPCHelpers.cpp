//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/OpenLedger.h>
#include <ripple/app/misc/Transaction.h>
#include <ripple/ledger/View.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/AccountID.h>
#include <ripple/protocol/Feature.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/DeliveredAmount.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <boost/algorithm/string/case_conv.hpp>

namespace ripple {
namespace RPC {

boost::optional<AccountID>
accountFromStringStrict(std::string const& account)
{
    boost::optional <AccountID> result;

    auto const publicKey = parseBase58<PublicKey> (
        TokenType::AccountPublic,
        account);

    if (publicKey)
        result = calcAccountID (*publicKey);
    else
        result = parseBase58<AccountID> (account);

    return result;
}

error_code_i
accountFromStringWithCode(
    AccountID& result, std::string const& strIdent, bool bStrict)
{
    if (auto accountID = accountFromStringStrict (strIdent))
    {
        result = *accountID;
        return rpcSUCCESS;
    }

    if (bStrict)
    {
        auto id = deprecatedParseBitcoinAccountID (strIdent);
        return id ? rpcACT_BITCOIN : rpcACT_MALFORMED;
    }

    // We allow the use of the seeds which is poor practice
    // and merely for debugging convenience.
    auto const seed = parseGenericSeed (strIdent);

    if (!seed)
        return rpcBAD_SEED;

    auto const keypair = generateKeyPair (
        KeyType::secp256k1,
        *seed);

    result = calcAccountID (keypair.first);
    return rpcSUCCESS;
}

Json::Value
accountFromString(
    AccountID& result, std::string const& strIdent, bool bStrict)
{

    error_code_i code = accountFromStringWithCode(result, strIdent, bStrict);
    if(code != rpcSUCCESS)
        return rpcError(code);
    else
        return Json::objectValue;
}

bool
getAccountObjects(ReadView const& ledger, AccountID const& account,
    boost::optional<std::vector<LedgerEntryType>> const& typeFilter, uint256 dirIndex,
    uint256 const& entryIndex, std::uint32_t const limit, Json::Value& jvResult)
{
    auto const rootDirIndex = getOwnerDirIndex (account);
    auto found = false;

    if (dirIndex.isZero ())
    {
        dirIndex = rootDirIndex;
        found = true;
    }

    auto dir = ledger.read({ltDIR_NODE, dirIndex});
    if (! dir)
        return false;

    std::uint32_t i = 0;
    auto& jvObjects = (jvResult[jss::account_objects] = Json::arrayValue);
    for (;;)
    {
        auto const& entries = dir->getFieldV256 (sfIndexes);
        auto iter = entries.begin ();

        if (! found)
        {
            iter = std::find (iter, entries.end (), entryIndex);
            if (iter == entries.end ())
                return false;

            found = true;
        }

        for (; iter != entries.end (); ++iter)
        {
            auto const sleNode = ledger.read(keylet::child(*iter));

            auto typeMatchesFilter = [] (
                std::vector<LedgerEntryType> const& typeFilter,
                LedgerEntryType ledgerType)
            {
                auto it = std::find(typeFilter.begin(), typeFilter.end(),
                    ledgerType);
                return it != typeFilter.end();
            };

            if (!typeFilter.has_value() ||
                typeMatchesFilter(typeFilter.value(), sleNode->getType()))
            {
                jvObjects.append (sleNode->getJson (JsonOptions::none));

                if (++i == limit)
                {
                    if (++iter != entries.end ())
                    {
                        jvResult[jss::limit] = limit;
                        jvResult[jss::marker] = to_string (dirIndex) + ',' +
                            to_string (*iter);
                        return true;
                    }

                    break;
                }
            }
        }

        auto const nodeIndex = dir->getFieldU64 (sfIndexNext);
        if (nodeIndex == 0)
            return true;

        dirIndex = getDirNodeIndex (rootDirIndex, nodeIndex);
        dir = ledger.read({ltDIR_NODE, dirIndex});
        if (! dir)
            return true;

        if (i == limit)
        {
            auto const& e = dir->getFieldV256 (sfIndexes);
            if (! e.empty ())
            {
                jvResult[jss::limit] = limit;
                jvResult[jss::marker] = to_string (dirIndex) + ',' +
                    to_string (*e.begin ());
            }

            return true;
        }
    }
}

namespace {

bool
isValidatedOld(LedgerMaster& ledgerMaster, bool standalone)
{
    if (standalone)
        return false;

    return ledgerMaster.getValidatedLedgerAge () >
        Tuning::maxValidatedLedgerAge;
}

template <class T>
Status
ledgerFromRequest(T& ledger, JsonContext& context)
{
    static auto const minSequenceGap = 10;

    ledger.reset();

    auto& params = context.params;
    auto& ledgerMaster = context.ledgerMaster;

    auto indexValue = params[jss::ledger_index];
    auto hashValue = params[jss::ledger_hash];

    // We need to support the legacy "ledger" field.
    auto& legacyLedger = params[jss::ledger];
    if (legacyLedger)
    {
        if (legacyLedger.asString().size () > 12)
            hashValue = legacyLedger;
        else
            indexValue = legacyLedger;
    }

    if (hashValue)
    {
        if (! hashValue.isString ())
            return {rpcINVALID_PARAMS, "ledgerHashNotString"};

        uint256 ledgerHash;
        if (! ledgerHash.SetHex (hashValue.asString ()))
            return {rpcINVALID_PARAMS, "ledgerHashMalformed"};

        ledger = ledgerMaster.getLedgerByHash (ledgerHash);
        if (ledger == nullptr)
            return {rpcLGR_NOT_FOUND, "ledgerNotFound"};
    }
    else if (indexValue.isNumeric())
    {
        ledger = ledgerMaster.getLedgerBySeq (indexValue.asInt ());

        if (ledger == nullptr)
        {
            auto cur = ledgerMaster.getCurrentLedger();
            if (cur->info().seq == indexValue.asInt())
                ledger = cur;
        }

        if (ledger == nullptr)
            return {rpcLGR_NOT_FOUND, "ledgerNotFound"};

        if (ledger->info().seq > ledgerMaster.getValidLedgerIndex() &&
            isValidatedOld(ledgerMaster, context.app.config().standalone()))
        {
            ledger.reset();
            return {rpcNO_NETWORK, "InsufficientNetworkMode"};
        }
    }
    else
    {
        if (isValidatedOld (ledgerMaster, context.app.config().standalone()))
            return {rpcNO_NETWORK, "InsufficientNetworkMode"};

        auto const index = indexValue.asString ();
        if (index == "validated")
        {
            ledger = ledgerMaster.getValidatedLedger ();
            if (ledger == nullptr)
                return {rpcNO_NETWORK, "InsufficientNetworkMode"};

            assert (! ledger->open());
        }
        else
        {
            if (index.empty () || index == "current")
            {
                ledger = ledgerMaster.getCurrentLedger ();
                assert (ledger->open());
            }
            else if (index == "closed")
            {
                ledger = ledgerMaster.getClosedLedger ();
                assert (! ledger->open());
            }
            else
            {
                return {rpcINVALID_PARAMS, "ledgerIndexMalformed"};
            }

            if (ledger == nullptr)
                return {rpcNO_NETWORK, "InsufficientNetworkMode"};

            if (ledger->info().seq + minSequenceGap <
                ledgerMaster.getValidLedgerIndex ())
            {
                ledger.reset ();
                return {rpcNO_NETWORK, "InsufficientNetworkMode"};
            }
        }
    }

    return Status::OK;
}
}  // namespace

template <class T>
Status
ledgerFromRequest(
    T& ledger,
    GRPCContext<rpc::v1::GetAccountInfoRequest>& context)
{
    static auto const minSequenceGap = 10;

    ledger.reset();

    rpc::v1::GetAccountInfoRequest& request = context.params;
    auto& ledgerMaster = context.ledgerMaster;

    using LedgerCase = rpc::v1::LedgerSpecifier::LedgerCase;
    LedgerCase ledgerCase = request.ledger().ledger_case();

    if (ledgerCase == LedgerCase::kHash)
    {
        uint256 ledgerHash = uint256::fromVoid(request.ledger().hash().data());
        if (ledgerHash.size() != request.ledger().hash().size())
            return {rpcINVALID_PARAMS, "ledgerHashMalformed"};

        ledger = ledgerMaster.getLedgerByHash(ledgerHash);
        if (ledger == nullptr)
            return {rpcLGR_NOT_FOUND, "ledgerNotFound"};
    }
    else if (ledgerCase == LedgerCase::kSequence)
    {
        ledger = ledgerMaster.getLedgerBySeq(request.ledger().sequence());

        if (ledger == nullptr)
        {
            auto cur = ledgerMaster.getCurrentLedger();
            if (cur->info().seq == request.ledger().sequence())
                ledger = cur;
        }

        if (ledger == nullptr)
            return {rpcLGR_NOT_FOUND, "ledgerNotFound"};

        if (ledger->info().seq > ledgerMaster.getValidLedgerIndex() &&
            isValidatedOld(ledgerMaster, context.app.config().standalone()))
        {
            ledger.reset();
            return {rpcNO_NETWORK, "InsufficientNetworkMode"};
        }
    }
    else if (
        ledgerCase == LedgerCase::kShortcut ||
        ledgerCase == LedgerCase::LEDGER_NOT_SET)
    {
        if (isValidatedOld(ledgerMaster, context.app.config().standalone()))
            return {rpcNO_NETWORK, "InsufficientNetworkMode"};

        auto const shortcut = request.ledger().shortcut();
        if (shortcut == rpc::v1::LedgerSpecifier::SHORTCUT_VALIDATED)
        {
            ledger = ledgerMaster.getValidatedLedger();
            if (ledger == nullptr)
                return {rpcNO_NETWORK, "InsufficientNetworkMode"};

            assert(!ledger->open());
        }
        else
        {
            // note, if unspecified, defaults to current ledger
            if (shortcut == rpc::v1::LedgerSpecifier::SHORTCUT_UNSPECIFIED ||
                shortcut == rpc::v1::LedgerSpecifier::SHORTCUT_CURRENT)
            {
                ledger = ledgerMaster.getCurrentLedger();
                assert(ledger->open());
            }
            else if (shortcut == rpc::v1::LedgerSpecifier::SHORTCUT_CLOSED)
            {
                ledger = ledgerMaster.getClosedLedger();
                assert(!ledger->open());
            }

            if (ledger == nullptr)
                return {rpcNO_NETWORK, "InsufficientNetworkMode"};

            if (ledger->info().seq + minSequenceGap <
                ledgerMaster.getValidLedgerIndex())
            {
                ledger.reset();
                return {rpcNO_NETWORK, "InsufficientNetworkMode"};
            }
        }
    }

    return Status::OK;
}

// explicit instantiation of above function
template Status
ledgerFromRequest<>(
    std::shared_ptr<ReadView const>&,
    GRPCContext<rpc::v1::GetAccountInfoRequest>&);

bool
isValidated(LedgerMaster& ledgerMaster, ReadView const& ledger,
    Application& app)
{
    if (ledger.open())
        return false;

    if (ledger.info().validated)
        return true;

    auto seq = ledger.info().seq;
    try
    {
        // Use the skip list in the last validated ledger to see if ledger
        // comes before the last validated ledger (and thus has been
        // validated).
        auto hash = ledgerMaster.walkHashBySeq (seq);

        if (!hash || ledger.info().hash != *hash)
        {
            // This ledger's hash is not the hash of the validated ledger
            if (hash)
            {
                assert(hash->isNonZero());
                uint256 valHash = getHashByIndex (seq, app);
                if (valHash == ledger.info().hash)
                {
                    // SQL database doesn't match ledger chain
                    ledgerMaster.clearLedger (seq);
                }
            }
            return false;
        }
    }
    catch (SHAMapMissingNode const&)
    {
        auto stream = app.journal ("RPCHandler").warn();
        JLOG (stream)
            << "Missing SHANode " << std::to_string (seq);
        return false;
    }

    // Mark ledger as validated to save time if we see it again.
    ledger.info().validated = true;
    return true;
}


// The previous version of the lookupLedger command would accept the
// "ledger_index" argument as a string and silently treat it as a request to
// return the current ledger which, while not strictly wrong, could cause a lot
// of confusion.
//
// The code now robustly validates the input and ensures that the only possible
// values for the "ledger_index" parameter are the index of a ledger passed as
// an integer or one of the strings "current", "closed" or "validated".
// Additionally, the code ensures that the value passed in "ledger_hash" is a
// string and a valid hash. Invalid values will return an appropriate error
// code.
//
// In the absence of the "ledger_hash" or "ledger_index" parameters, the code
// assumes that "ledger_index" has the value "current".
//
// Returns a Json::objectValue.  If there was an error, it will be in that
// return value.  Otherwise, the object contains the field "validated" and
// optionally the fields "ledger_hash", "ledger_index" and
// "ledger_current_index", if they are defined.
Status
lookupLedger(std::shared_ptr<ReadView const>& ledger, JsonContext& context,
    Json::Value& result)
{
    if (auto status = ledgerFromRequest (ledger, context))
        return status;

    auto& info = ledger->info();

    if (!ledger->open())
    {
        result[jss::ledger_hash] = to_string (info.hash);
        result[jss::ledger_index] = info.seq;
    }
    else
    {
        result[jss::ledger_current_index] = info.seq;
    }

    result[jss::validated] = isValidated (context.ledgerMaster, *ledger, context.app);
    return Status::OK;
}

Json::Value
lookupLedger(std::shared_ptr<ReadView const>& ledger, JsonContext& context)
{
    Json::Value result;
    if (auto status = lookupLedger (ledger, context, result))
        status.inject (result);

    return result;
}

hash_set<AccountID>
parseAccountIds(Json::Value const& jvArray)
{
    hash_set<AccountID> result;
    for (auto const& jv: jvArray)
    {
        if (! jv.isString())
            return hash_set<AccountID>();
        auto const id =
            parseBase58<AccountID>(jv.asString());
        if (! id)
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
            auto const& hash =
                sle.getFieldH128(sfEmailHash);
            Blob const b (hash.begin(), hash.end());
            std::string md5 = strHex(makeSlice(b));
            boost::to_lower(md5);
            // VFALCO TODO Give a name and move this constant
            //             to a more visible location. Also
            //             shouldn't this be https?
            jv[jss::urlgravatar] = str(boost::format(
                "http://www.gravatar.com/avatar/%s") % md5);
        }
    }
    else
    {
        jv[jss::Invalid] = true;
    }
}

boost::optional<Json::Value>
readLimitField(unsigned int& limit, Tuning::LimitRange const& range,
    JsonContext const& context)
{
    limit = range.rdefault;
    if (auto const& jvLimit = context.params[jss::limit])
    {
        if (! (jvLimit.isUInt() || (jvLimit.isInt() && jvLimit.asInt() >= 0)))
            return RPC::expected_field_error (jss::limit, "unsigned integer");

        limit = jvLimit.asUInt();
        if (! isUnlimited (context.role))
            limit = std::max(range.rmin, std::min(range.rmax, limit));
    }
    return boost::none;
}

boost::optional<Seed>
parseRippleLibSeed(Json::Value const& value)
{
    // ripple-lib encodes seed used to generate an Ed25519 wallet in a
    // non-standard way. While rippled never encode seeds that way, we
    // try to detect such keys to avoid user confusion.
    if (!value.isString())
        return boost::none;

    auto const result = decodeBase58Token(value.asString(), TokenType::None);

    if (result.size() == 18 &&
            static_cast<std::uint8_t>(result[0]) == std::uint8_t(0xE1) &&
            static_cast<std::uint8_t>(result[1]) == std::uint8_t(0x4B))
        return Seed(makeSlice(result.substr(2)));

    return boost::none;
}

boost::optional<Seed>
getSeedFromRPC(Json::Value const& params, Json::Value& error)
{
    // The array should be constexpr, but that makes Visual Studio unhappy.
    static char const* const seedTypes[]
    {
        jss::passphrase.c_str(),
        jss::seed.c_str(),
        jss::seed_hex.c_str()
    };

    // Identify which seed type is in use.
    char const* seedType = nullptr;
    int count = 0;
    for (auto t : seedTypes)
    {
        if (params.isMember (t))
        {
            ++count;
            seedType = t;
        }
    }

    if (count != 1)
    {
        error = RPC::make_param_error (
            "Exactly one of the following must be specified: " +
            std::string(jss::passphrase) + ", " +
            std::string(jss::seed) + " or " +
            std::string(jss::seed_hex));
        return boost::none;
    }

    // Make sure a string is present
    if (! params[seedType].isString())
    {
        error = RPC::expected_field_error (seedType, "string");
        return boost::none;
    }

    auto const fieldContents = params[seedType].asString();

    // Convert string to seed.
    boost::optional<Seed> seed;

    if (seedType == jss::seed.c_str())
        seed = parseBase58<Seed> (fieldContents);
    else if (seedType == jss::passphrase.c_str())
        seed = parseGenericSeed (fieldContents);
    else if (seedType == jss::seed_hex.c_str())
    {
        uint128 s;

        if (s.SetHexExact (fieldContents))
            seed.emplace (Slice(s.data(), s.size()));
    }

    if (!seed)
        error = rpcError (rpcBAD_SEED);

    return seed;
}

std::pair<PublicKey, SecretKey>
keypairForSignature(Json::Value const& params, Json::Value& error)
{
    bool const has_key_type  = params.isMember (jss::key_type);

    // All of the secret types we allow, but only one at a time.
    // The array should be constexpr, but that makes Visual Studio unhappy.
    static char const* const secretTypes[]
    {
        jss::passphrase.c_str(),
        jss::secret.c_str(),
        jss::seed.c_str(),
        jss::seed_hex.c_str()
    };

    // Identify which secret type is in use.
    char const* secretType = nullptr;
    int count = 0;
    for (auto t : secretTypes)
    {
        if (params.isMember (t))
        {
            ++count;
            secretType = t;
        }
    }

    if (count == 0 || secretType == nullptr)
    {
        error = RPC::missing_field_error (jss::secret);
        return { };
    }

    if (count > 1)
    {
        error = RPC::make_param_error (
            "Exactly one of the following must be specified: " +
            std::string(jss::passphrase) + ", " +
            std::string(jss::secret) + ", " +
            std::string(jss::seed) + " or " +
            std::string(jss::seed_hex));
        return { };
    }

    boost::optional<KeyType> keyType;
    boost::optional<Seed> seed;

    if (has_key_type)
    {
        if (! params[jss::key_type].isString())
        {
            error = RPC::expected_field_error (
                jss::key_type, "string");
            return { };
        }

        keyType = keyTypeFromString(params[jss::key_type].asString());

        if (!keyType)
        {
            error = RPC::invalid_field_error(jss::key_type);
            return { };
        }

        if (secretType == jss::secret.c_str())
        {
            error = RPC::make_param_error (
                "The secret field is not allowed if " +
                std::string(jss::key_type) + " is used.");
            return { };
        }
    }

    // ripple-lib encodes seed used to generate an Ed25519 wallet in a
    // non-standard way. While we never encode seeds that way, we try
    // to detect such keys to avoid user confusion.
    if (secretType != jss::seed_hex.c_str())
    {
        seed = RPC::parseRippleLibSeed(params[secretType]);

        if (seed)
        {
            // If the user passed in an Ed25519 seed but *explicitly*
            // requested another key type, return an error.
            if (keyType.value_or(KeyType::ed25519) != KeyType::ed25519)
            {
                error = RPC::make_error (rpcBAD_SEED,
                    "Specified seed is for an Ed25519 wallet.");
                return { };
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
        if (!contains_error (error))
        {
            error = RPC::make_error (rpcBAD_SEED,
                RPC::invalid_field_message (secretType));
        }

        return { };
    }

    if (keyType != KeyType::secp256k1 && keyType != KeyType::ed25519)
        LogicError ("keypairForSignature: invalid key type");

    return generateKeyPair (*keyType, *seed);
}

std::pair<RPC::Status, LedgerEntryType>
chooseLedgerEntryType(Json::Value const& params)
{
    std::pair<RPC::Status, LedgerEntryType> result{ RPC::Status::OK, ltINVALID };
    if (params.isMember(jss::type))
    {
        static
            std::array<std::pair<char const *, LedgerEntryType>, 13> const types
        { {
            { jss::account,         ltACCOUNT_ROOT },
            { jss::amendments,      ltAMENDMENTS },
            { jss::check,           ltCHECK },
            { jss::deposit_preauth, ltDEPOSIT_PREAUTH },
            { jss::directory,       ltDIR_NODE },
            { jss::escrow,          ltESCROW },
            { jss::fee,             ltFEE_SETTINGS },
            { jss::hashes,          ltLEDGER_HASHES },
            { jss::offer,           ltOFFER },
            { jss::payment_channel, ltPAYCHAN },
            { jss::signer_list,     ltSIGNER_LIST },
            { jss::state,           ltRIPPLE_STATE },
            { jss::ticket,          ltTICKET }
            } };

        auto const& p = params[jss::type];
        if (!p.isString())
        {
            result.first = RPC::Status{ rpcINVALID_PARAMS,
                "Invalid field 'type', not string." };
            assert(result.first.type() == RPC::Status::Type::error_code_i);
            return result;
        }

        auto const filter = p.asString();
        auto iter = std::find_if(types.begin(), types.end(),
            [&filter](decltype (types.front())& t)
        {
            return t.first == filter;
        });
        if (iter == types.end())
        {
            result.first = RPC::Status{ rpcINVALID_PARAMS,
                "Invalid field 'type'." };
            assert(result.first.type() == RPC::Status::Type::error_code_i);
            return result;
        }
        result.second = iter->second;
    }
    return result;
}

void
populateAccountRoot(rpc::v1::AccountRoot& proto, STObject const& obj)
{
    if (obj.isFieldPresent(sfAccount))
    {
        AccountID account = obj.getAccountID(sfAccount);
        proto.mutable_account()->set_address(toBase58(account));
    }
    if (obj.isFieldPresent(sfBalance))
    {
        STAmount amount = obj.getFieldAmount(sfBalance);
        proto.mutable_balance()->set_drops(amount.xrp().drops());
    }
    if (obj.isFieldPresent(sfSequence))
    {
        proto.set_sequence(obj.getFieldU32(sfSequence));
    }
    if (obj.isFieldPresent(sfFlags))
    {
        proto.set_flags(obj.getFieldU32(sfFlags));
    }
    if (obj.isFieldPresent(sfOwnerCount))
    {
        proto.set_owner_count(obj.getFieldU32(sfOwnerCount));
    }
    if (obj.isFieldPresent(sfPreviousTxnID))
    {
        auto field = obj.getFieldH256(sfPreviousTxnID);
        proto.set_previous_transaction_id(field.data(), field.size());
    }
    if (obj.isFieldPresent(sfPreviousTxnLgrSeq))
    {
        proto.set_previous_transaction_ledger_sequence(
            obj.getFieldU32(sfPreviousTxnLgrSeq));
    }
    if (obj.isFieldPresent(sfAccountTxnID))
    {
        auto field = obj.getFieldH256(sfAccountTxnID);
        proto.set_account_transaction_id(field.data(), field.size());
    }
    if (obj.isFieldPresent(sfDomain))
    {
        auto field = obj.getFieldH256(sfDomain);
        proto.set_domain(field.data(), field.size());
    }
    if (obj.isFieldPresent(sfEmailHash))
    {
        auto field = obj.getFieldH128(sfEmailHash);
        proto.set_email_hash(field.data(), field.size());
    }
    if (obj.isFieldPresent(sfMessageKey))
    {
        auto field = obj.getFieldVL(sfMessageKey);
        proto.set_message_key(field.data(), field.size());
    }
    if (obj.isFieldPresent(sfRegularKey))
    {
        proto.set_regular_key(toBase58(obj.getAccountID(sfRegularKey)));
    }
    if (obj.isFieldPresent(sfTickSize))
    {
        proto.set_tick_size(obj.getFieldU8(sfTickSize));
    }
    if (obj.isFieldPresent(sfTransferRate))
    {
        proto.set_transfer_rate(obj.getFieldU32(sfTransferRate));
    }
}

void
populateRippleState(rpc::v1::RippleState& proto, STObject const& obj)
{
    if (obj.isFieldPresent(sfBalance))
    {
        STAmount amount = obj.getFieldAmount(sfBalance);
        populateAmount(*proto.mutable_balance(), amount);
    }
    if (obj.isFieldPresent(sfFlags))
    {
        proto.set_flags(obj.getFieldU32(sfFlags));
    }
    if (obj.isFieldPresent(sfLowLimit))
    {
        STAmount amount = obj.getFieldAmount(sfLowLimit);
        populateAmount(*proto.mutable_low_limit(), amount);
    }
    if (obj.isFieldPresent(sfHighLimit))
    {
        STAmount amount = obj.getFieldAmount(sfHighLimit);
        populateAmount(*proto.mutable_high_limit(), amount);
    }
    if (obj.isFieldPresent(sfLowNode))
    {
        proto.set_low_node(obj.getFieldU64(sfLowNode));
    }
    if (obj.isFieldPresent(sfHighNode))
    {
        proto.set_high_node(obj.getFieldU64(sfHighNode));
    }
    if (obj.isFieldPresent(sfLowQualityIn))
    {
        proto.set_low_quality_in(obj.getFieldU32(sfLowQualityIn));
    }
    if (obj.isFieldPresent(sfLowQualityOut))
    {
        proto.set_low_quality_out(obj.getFieldU32(sfLowQualityOut));
    }
    if (obj.isFieldPresent(sfHighQualityIn))
    {
        proto.set_high_quality_in(obj.getFieldU32(sfHighQualityIn));
    }
    if (obj.isFieldPresent(sfHighQualityOut))
    {
        proto.set_high_quality_out(obj.getFieldU32(sfHighQualityOut));
    }
}

void
populateOffer(rpc::v1::Offer& proto, STObject const& obj)
{
    if (obj.isFieldPresent(sfAccount))
    {
        AccountID account = obj.getAccountID(sfAccount);
        proto.set_account(toBase58(account));
    }
    if (obj.isFieldPresent(sfSequence))
    {
        proto.set_sequence(obj.getFieldU32(sfSequence));
    }
    if (obj.isFieldPresent(sfFlags))
    {
        proto.set_flags(obj.getFieldU32(sfFlags));
    }
    if (obj.isFieldPresent(sfTakerPays))
    {
        STAmount amount = obj.getFieldAmount(sfTakerPays);
        populateAmount(*proto.mutable_taker_pays(), amount);
    }
    if (obj.isFieldPresent(sfTakerGets))
    {
        STAmount amount = obj.getFieldAmount(sfTakerGets);
        populateAmount(*proto.mutable_taker_gets(), amount);
    }
    if (obj.isFieldPresent(sfBookDirectory))
    {
        auto field = obj.getFieldVL(sfBookDirectory);
        proto.set_book_directory(field.data(), field.size());
    }
    if (obj.isFieldPresent(sfBookNode))
    {
        proto.set_book_node(obj.getFieldU64(sfBookNode));
    }
    if (obj.isFieldPresent(sfExpiration))
    {
        proto.set_expiration(obj.getFieldU32(sfExpiration));
    }
}

void
populateSignerList(rpc::v1::SignerList& proto, STObject const& obj)
{
    proto.set_flags(obj.getFieldU32(sfFlags));

    auto prevTxnID = obj.getFieldH256(sfPreviousTxnID);
    proto.set_previous_txn_id(prevTxnID.data(), prevTxnID.size());

    proto.set_previous_transaction_ledger_sequence(
        obj.getFieldU32(sfPreviousTxnLgrSeq));

    proto.set_owner_node(obj.getFieldU64(sfOwnerNode));

    proto.set_signer_list_id(obj.getFieldU32(sfSignerListID));

    proto.set_signer_quorum(obj.getFieldU32(sfSignerQuorum));

    STArray const& signerEntries = obj.getFieldArray(sfSignerEntries);

    for (auto it = signerEntries.begin(); it != signerEntries.end(); ++it)
    {
        rpc::v1::SignerEntry& signerEntryProto = *proto.add_signer_entries();

        signerEntryProto.mutable_account()->set_address(
            toBase58(it->getAccountID(sfAccount)));
        signerEntryProto.set_signer_weight(it->getFieldU16(sfSignerWeight));
    }
}

void
populateQueueData(
    rpc::v1::QueueData& proto,
    std::map<TxSeq, TxQ::AccountTxDetails const> const& txs)
{
    if (!txs.empty())
    {
        proto.set_txn_count(txs.size());
        proto.set_lowest_sequence(txs.begin()->first);
        proto.set_highest_sequence(txs.rbegin()->first);

        boost::optional<bool> anyAuthChanged(false);
        boost::optional<XRPAmount> totalSpend(0);

        for (auto const& [txSeq, txDetails] : txs)
        {
            rpc::v1::QueuedTransaction& qt = *proto.add_transactions();

            qt.set_sequence(txSeq);
            qt.set_fee_level(txDetails.feeLevel.fee());
            if (txDetails.lastValid)
                qt.set_last_ledger_sequence(*txDetails.lastValid);

            if (txDetails.consequences)
            {
                qt.mutable_fee()->set_drops(
                    txDetails.consequences->fee.drops());
                auto spend = txDetails.consequences->potentialSpend +
                    txDetails.consequences->fee;
                qt.mutable_max_spend_drops()->set_drops(spend.drops());
                if (totalSpend)
                    *totalSpend += spend;
                auto authChanged =
                    txDetails.consequences->category == TxConsequences::blocker;
                if (authChanged)
                    anyAuthChanged.emplace(authChanged);
                qt.set_auth_change(authChanged);
            }
            else
            {
                if (anyAuthChanged && !*anyAuthChanged)
                    anyAuthChanged.reset();
                totalSpend.reset();
            }
        }

        if (anyAuthChanged)
            proto.set_auth_change_queued(*anyAuthChanged);
        if (totalSpend)
            proto.mutable_max_spend_drops_total()->set_drops(
                (*totalSpend).drops());
    }
}

void
populateDirectoryNode(rpc::v1::DirectoryNode& proto, STObject const& obj)
{
    if (obj.isFieldPresent(sfOwner))
    {
        AccountID ownerAccount = obj.getAccountID(sfAccount);
        proto.set_owner(toBase58(ownerAccount));
    }
    if (obj.isFieldPresent(sfTakerPaysCurrency))
    {
        uint160 tpCurr = obj.getFieldH160(sfTakerPaysCurrency);
        proto.mutable_taker_pays_currency()->set_code(
            tpCurr.data(), tpCurr.size());
    }
    if (obj.isFieldPresent(sfTakerPaysIssuer))
    {
        uint160 tpIss = obj.getFieldH160(sfTakerPaysIssuer);
        proto.set_taker_pays_issuer(tpIss.data(), tpIss.size());
    }
    if (obj.isFieldPresent(sfTakerGetsCurrency))
    {
        uint160 tgCurr = obj.getFieldH160(sfTakerGetsCurrency);
        proto.mutable_taker_gets_currency()->set_code(
            tgCurr.data(), tgCurr.size());
    }
    if (obj.isFieldPresent(sfTakerGetsIssuer))
    {
        uint160 tgIss = obj.getFieldH160(sfTakerGetsIssuer);
        proto.set_taker_gets_issuer(tgIss.data(), tgIss.size());
    }
    if (obj.isFieldPresent(sfIndexes))
    {
        const STVector256& vec = obj.getFieldV256(sfIndexes);
        for (size_t i = 0; i < vec.size(); ++i)
        {
            uint256 const& elt = vec[i];
            proto.add_indexes(elt.data(), elt.size());
        }
    }
    if (obj.isFieldPresent(sfRootIndex))
    {
        uint256 rootIndex = obj.getFieldH256(sfRootIndex);
        proto.set_root_index(rootIndex.data(), rootIndex.size());
    }
    if (obj.isFieldPresent(sfIndexNext))
    {
        proto.set_index_next(obj.getFieldU64(sfIndexNext));
    }
    if (obj.isFieldPresent(sfIndexPrevious))
    {
        proto.set_index_previous(obj.getFieldU64(sfIndexPrevious));
    }
}

void
populateLedgerEntryType(rpc::v1::AffectedNode& proto, std::uint16_t lgrType)
{
    switch (lgrType)
    {
        case ltACCOUNT_ROOT:
            proto.set_ledger_entry_type(
                rpc::v1::LEDGER_ENTRY_TYPE_ACCOUNT_ROOT);
            break;
        case ltDIR_NODE:
            proto.set_ledger_entry_type(
                rpc::v1::LEDGER_ENTRY_TYPE_DIRECTORY_NODE);
            break;
        case ltRIPPLE_STATE:
            proto.set_ledger_entry_type(
                rpc::v1::LEDGER_ENTRY_TYPE_RIPPLE_STATE);
            break;
        case ltSIGNER_LIST:
            proto.set_ledger_entry_type(rpc::v1::LEDGER_ENTRY_TYPE_SIGNER_LIST);
            break;
        case ltOFFER:
            proto.set_ledger_entry_type(rpc::v1::LEDGER_ENTRY_TYPE_OFFER);
            break;
        case ltLEDGER_HASHES:
            proto.set_ledger_entry_type(
                rpc::v1::LEDGER_ENTRY_TYPE_LEDGER_HASHES);
            break;
        case ltAMENDMENTS:
            proto.set_ledger_entry_type(rpc::v1::LEDGER_ENTRY_TYPE_AMENDMENTS);
            break;
        case ltFEE_SETTINGS:
            proto.set_ledger_entry_type(
                rpc::v1::LEDGER_ENTRY_TYPE_FEE_SETTINGS);
            break;
        case ltESCROW:
            proto.set_ledger_entry_type(rpc::v1::LEDGER_ENTRY_TYPE_ESCROW);
            break;
        case ltPAYCHAN:
            proto.set_ledger_entry_type(rpc::v1::LEDGER_ENTRY_TYPE_PAY_CHANNEL);
            break;
        case ltCHECK:
            proto.set_ledger_entry_type(rpc::v1::LEDGER_ENTRY_TYPE_CHECK);
            break;
        case ltDEPOSIT_PREAUTH:
            proto.set_ledger_entry_type(
                rpc::v1::LEDGER_ENTRY_TYPE_DEPOSIT_PREAUTH);
            break;
    }
}

template <class T>
void
populateFields(T& proto, STObject const& obj, std::uint16_t type)
{
    if (type == ltACCOUNT_ROOT)
    {
        RPC::populateAccountRoot(*proto.mutable_account_root(), obj);
    }
    else if (type == ltRIPPLE_STATE)
    {
        RPC::populateRippleState(*proto.mutable_ripple_state(), obj);
    }
    else if (type == ltOFFER)
    {
        RPC::populateOffer(*proto.mutable_offer(), obj);
    }
    else if (type == ltDIR_NODE)
    {
        RPC::populateDirectoryNode(*proto.mutable_directory_node(), obj);
    }
    else
    {
        // Ledger object not supported by protobuf/grpc yet
    }
}

void
populateMeta(rpc::v1::Meta& proto, std::shared_ptr<TxMeta> txMeta)
{
    proto.set_transaction_index(txMeta->getIndex());

    populateTransactionResultType(
        *proto.mutable_transaction_result(), txMeta->getResultTER());
    proto.mutable_transaction_result()->set_result(
        transToken(txMeta->getResultTER()));

    STArray& nodes = txMeta->getNodes();
    for (auto it = nodes.begin(); it != nodes.end(); ++it)
    {
        STObject& obj = *it;
        rpc::v1::AffectedNode* node = proto.add_affected_nodes();

        // ledger index
        uint256 ledgerIndex = obj.getFieldH256(sfLedgerIndex);
        node->set_ledger_index(ledgerIndex.data(), ledgerIndex.size());

        // ledger entry type
        std::uint16_t lgrType = obj.getFieldU16(sfLedgerEntryType);
        populateLedgerEntryType(*node, lgrType);

        // modified node
        if (obj.getFName() == sfModifiedNode)
        {
            // final fields
            if (obj.isFieldPresent(sfFinalFields))
            {
                STObject& finalFields =
                    obj.getField(sfFinalFields).downcast<STObject>();

                rpc::v1::LedgerObject* finalFieldsProto =
                    node->mutable_modified_node()->mutable_final_fields();

                populateFields(*finalFieldsProto, finalFields, lgrType);
            }
            // previous fields
            if (obj.isFieldPresent(sfPreviousFields))
            {
                STObject& prevFields =
                    obj.getField(sfPreviousFields).downcast<STObject>();

                rpc::v1::LedgerObject* prevFieldsProto =
                    node->mutable_modified_node()->mutable_previous_fields();

                populateFields(*prevFieldsProto, prevFields, lgrType);
            }

            // prev txn id and prev txn ledger seq
            uint256 prevTxnId = obj.getFieldH256(sfPreviousTxnID);
            node->mutable_modified_node()->set_previous_transaction_id(
                prevTxnId.data(), prevTxnId.size());

            node->mutable_modified_node()
                ->set_previous_transaction_ledger_sequence(
                    obj.getFieldU32(sfPreviousTxnLgrSeq));
        }
        // created node
        else if (obj.getFName() == sfCreatedNode)
        {
            // new fields
            if (obj.isFieldPresent(sfNewFields))
            {
                STObject& newFields =
                    obj.getField(sfNewFields).downcast<STObject>();

                rpc::v1::LedgerObject* newFieldsProto =
                    node->mutable_created_node()->mutable_new_fields();

                populateFields(*newFieldsProto, newFields, lgrType);
            }
        }
        // deleted node
        else if (obj.getFName() == sfDeletedNode)
        {
            // final fields
            if (obj.isFieldPresent(sfFinalFields))
            {
                STObject& finalFields =
                    obj.getField(sfFinalFields).downcast<STObject>();

                rpc::v1::LedgerObject* finalFieldsProto =
                    node->mutable_deleted_node()->mutable_final_fields();

                populateFields(*finalFieldsProto, finalFields, lgrType);
            }
        }
    }
}

void
populateAmount(rpc::v1::CurrencyAmount& proto, STAmount const& amount)
{
    if (amount.native())
    {
        proto.mutable_xrp_amount()->set_drops(amount.xrp().drops());
    }
    else
    {
        rpc::v1::IssuedCurrencyAmount* issued =
            proto.mutable_issued_currency_amount();
        Issue const& issue = amount.issue();
        Currency currency = issue.currency;
        issued->mutable_currency()->set_name(to_string(issue.currency));
        issued->mutable_currency()->set_code(currency.data(), currency.size());
        issued->set_value(to_string(amount.iou()));
        issued->mutable_issuer()->set_address(toBase58(issue.account));
    }
}

void
populateTransaction(
    rpc::v1::Transaction& proto,
    std::shared_ptr<STTx const> txnSt)
{
    AccountID account = txnSt->getAccountID(sfAccount);
    proto.mutable_account()->set_address(toBase58(account));

    STAmount amount = txnSt->getFieldAmount(sfAmount);
    populateAmount(*proto.mutable_payment()->mutable_amount(), amount);

    AccountID accountDest = txnSt->getAccountID(sfDestination);
    proto.mutable_payment()->mutable_destination()->set_address(
        toBase58(accountDest));

    STAmount fee = txnSt->getFieldAmount(sfFee);
    proto.mutable_fee()->set_drops(fee.xrp().drops());

    proto.set_sequence(txnSt->getFieldU32(sfSequence));

    Blob signingPubKey = txnSt->getFieldVL(sfSigningPubKey);
    proto.set_signing_public_key(signingPubKey.data(), signingPubKey.size());

    proto.set_flags(txnSt->getFieldU32(sfFlags));

    proto.set_last_ledger_sequence(txnSt->getFieldU32(sfLastLedgerSequence));

    Blob blob = txnSt->getFieldVL(sfTxnSignature);
    proto.set_signature(blob.data(), blob.size());

    if (txnSt->isFieldPresent(sfSourceTag))
    {
        proto.set_source_tag(txnSt->getFieldU32(sfSourceTag));
    }

    if (txnSt->isFieldPresent(sfAccountTxnID))
    {
        auto field = txnSt->getFieldH256(sfAccountTxnID);
        proto.set_account_transaction_id(field.data(), field.size());
    }

    if (txnSt->isFieldPresent(sfMemos))
    {
        auto memos = txnSt->getFieldArray(sfMemos);
        for (auto it = memos.begin(); it != memos.end(); ++it)
        {
            rpc::v1::Memo* elt = proto.add_memos();
            auto memo = it->getField(sfMemo).downcast<STObject>();
            if (memo.isFieldPresent(sfMemoData))
            {
                auto memoData = memo.getFieldVL(sfMemoData);
                elt->set_memo_data(memoData.data(), memoData.size());
            }
            if (memo.isFieldPresent(sfMemoFormat))
            {
                auto memoFormat = memo.getFieldVL(sfMemoFormat);
                elt->set_memo_format(memoFormat.data(), memoFormat.size());
            }
            if (memo.isFieldPresent(sfMemoType))
            {
                auto memoType = memo.getFieldVL(sfMemoType);
                elt->set_memo_type(memoType.data(), memoType.size());
            }
        }
    }

    if (txnSt->isFieldPresent(sfSigners))
    {
        auto signers = txnSt->getFieldArray(sfSigners);

        for (auto it = signers.begin(); it != signers.end(); ++it)
        {
            rpc::v1::Signer* elt = proto.add_signers();
            auto signer = it->getField(sfSigner).downcast<STObject>();
            if (signer.isFieldPresent(sfAccount))
            {
                elt->mutable_account()->set_address(
                    toBase58(signer.getAccountID(sfAccount)));
            }
            if (signer.isFieldPresent(sfTxnSignature))
            {
                auto sig = signer.getFieldVL(sfTxnSignature);
                elt->set_transaction_signature(sig.data(), sig.size());
            }
            if (signer.isFieldPresent(sfSigningPubKey))
            {
                auto pubKey = signer.getFieldVL(sfSigningPubKey);
                elt->set_signing_public_key(pubKey.data(), pubKey.size());
            }
        }
    }

    if (safe_cast<TxType>(txnSt->getFieldU16(sfTransactionType)) ==
        TxType::ttPAYMENT)
    {
        if (txnSt->isFieldPresent(sfSendMax))
        {
            STAmount const& sendMax = txnSt->getFieldAmount(sfSendMax);
            populateAmount(
                *proto.mutable_payment()->mutable_send_max(), sendMax);
        }

        if (txnSt->isFieldPresent(sfInvoiceID))
        {
            auto invoice = txnSt->getFieldH256(sfInvoiceID);
            proto.mutable_payment()->set_invoice_id(
                invoice.data(), invoice.size());
        }

        if (txnSt->isFieldPresent(sfDestinationTag))
        {
            proto.mutable_payment()->set_destination_tag(
                txnSt->getFieldU32(sfDestinationTag));
        }

        // populate path data
        STPathSet const& pathset = txnSt->getFieldPathSet(sfPaths);
        for (auto it = pathset.begin(); it < pathset.end(); ++it)
        {
            STPath const& path = *it;

            rpc::v1::Path* protoPath = proto.mutable_payment()->add_paths();

            for (auto it2 = path.begin(); it2 != path.end(); ++it2)
            {
                rpc::v1::PathElement* protoElement = protoPath->add_elements();
                STPathElement const& elt = *it2;

                if (elt.isOffer())
                {
                    if (elt.hasCurrency())
                    {
                        Currency const& currency = elt.getCurrency();
                        protoElement->mutable_currency()->set_name(
                            to_string(currency));
                    }
                    if (elt.hasIssuer())
                    {
                        AccountID const& issuer = elt.getIssuerID();
                        protoElement->mutable_issuer()->set_address(
                            toBase58(issuer));
                    }
                }
                else
                {
                    AccountID const& pathAccount = elt.getAccountID();
                    protoElement->mutable_account()->set_address(
                        toBase58(pathAccount));
                }
            }
        }
    }
}

void
populateTransactionResultType(rpc::v1::TransactionResult& proto, TER result)
{
    if (isTecClaim(result))
    {
        proto.set_result_type(rpc::v1::TransactionResult::RESULT_TYPE_TEC);
    }
    if (isTefFailure(result))
    {
        proto.set_result_type(rpc::v1::TransactionResult::RESULT_TYPE_TEF);
    }
    if (isTelLocal(result))
    {
        proto.set_result_type(rpc::v1::TransactionResult::RESULT_TYPE_TEL);
    }
    if (isTemMalformed(result))
    {
        proto.set_result_type(rpc::v1::TransactionResult::RESULT_TYPE_TEM);
    }
    if (isTerRetry(result))
    {
        proto.set_result_type(rpc::v1::TransactionResult::RESULT_TYPE_TER);
    }
    if (isTesSuccess(result))
    {
        proto.set_result_type(rpc::v1::TransactionResult::RESULT_TYPE_TES);
    }
}

beast::SemanticVersion const firstVersion("1.0.0");
beast::SemanticVersion const goodVersion("1.0.0");
beast::SemanticVersion const lastVersion("1.0.0");

unsigned int getAPIVersionNumber(Json::Value const& jv)
{
    static Json::Value const minVersion (RPC::ApiMinimumSupportedVersion);
    static Json::Value const maxVersion (RPC::ApiMaximumSupportedVersion);
    static Json::Value const invalidVersion (RPC::APIInvalidVersion);

    Json::Value requestedVersion(RPC::APIVersionIfUnspecified);
    if(jv.isObject())
    {
        requestedVersion = jv.get (jss::api_version, requestedVersion);
    }
    if( !(requestedVersion.isInt() || requestedVersion.isUInt()) ||
        requestedVersion < minVersion || requestedVersion > maxVersion)
    {
        requestedVersion = invalidVersion;
    }
    return requestedVersion.asUInt();
}

} // RPC
} // ripple
