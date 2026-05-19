#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/GRPCHandlers.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/handlers/ledger/LedgerEntryHelpers.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/json/json_errors.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <grpcpp/support/status.h>
#include <org/xrpl/rpc/v1/get_ledger_entry.pb.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace xrpl {

using FunctionType = std::function<Expected<uint256, json::Value>(
    json::Value const&,
    json::StaticString const,
    unsigned const apiVersion)>;

static Expected<uint256, json::Value>
parseFixed(
    Keylet const& keylet,
    json::Value const& params,
    json::StaticString const& fieldName,
    unsigned const apiVersion);

// Helper function to return FunctionType for objects that have a fixed
// location. That is, they don't take parameters to compute the index.
// e.g. amendments, fees, negative UNL, etc.
static FunctionType
fixed(Keylet const& keylet)
{
    return [keylet](
               json::Value const& params,
               json::StaticString const fieldName,
               unsigned const apiVersion) -> Expected<uint256, json::Value> {
        return parseFixed(keylet, params, fieldName, apiVersion);
    };
}

static Expected<uint256, json::Value>
parseObjectID(
    json::Value const& params,
    json::StaticString const fieldName,
    std::string const& expectedType = "hex string or object")
{
    if (auto const uNodeIndex = LedgerEntryHelpers::parse<uint256>(params))
    {
        return *uNodeIndex;
    }
    return LedgerEntryHelpers::invalidFieldError("malformedRequest", fieldName, expectedType);
}

static Expected<uint256, json::Value>
parseIndex(json::Value const& params, json::StaticString const fieldName, unsigned const apiVersion)
{
    if (apiVersion > 2u && params.isString())
    {
        std::string const index = params.asString();
        if (index == jss::amendments.cStr())
            return keylet::amendments().key;
        if (index == jss::fee.cStr())
            return keylet::fees().key;
        if (index == jss::nunl)
            return keylet::negativeUNL().key;
        if (index == jss::hashes)
        {
            // Note this only finds the "short" skip list. Use "hashes":index to
            // get the long list.
            return keylet::skip().key;
        }
    }
    return parseObjectID(params, fieldName, "hex string");
}

static Expected<uint256, json::Value>
parseAccountRoot(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (auto const account = LedgerEntryHelpers::parse<AccountID>(params))
    {
        return keylet::account(*account).key;
    }

    return LedgerEntryHelpers::invalidFieldError("malformedAddress", fieldName, "AccountID");
}

auto const parseAmendments = fixed(keylet::amendments());

static Expected<uint256, json::Value>
parseAMM(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName);
    }

    if (auto const value = LedgerEntryHelpers::hasRequired(params, {jss::asset, jss::asset2});
        !value)
    {
        return Unexpected(value.error());
    }

    auto const asset = LedgerEntryHelpers::requiredAsset(params, jss::asset, "malformedRequest");
    if (!asset)
        return Unexpected(asset.error());

    auto const asset2 = LedgerEntryHelpers::requiredAsset(params, jss::asset2, "malformedRequest");
    if (!asset2)
        return Unexpected(asset2.error());

    return keylet::amm(*asset, *asset2).key;
}

static Expected<uint256, json::Value>
parseBridge(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!params.isMember(jss::bridge))
    {
        return Unexpected(LedgerEntryHelpers::missingFieldError(jss::bridge));
    }

    if (params[jss::bridge].isString())
    {
        return parseObjectID(params, fieldName);
    }

    auto const bridge = LedgerEntryHelpers::parseBridgeFields(params[jss::bridge]);
    if (!bridge)
        return Unexpected(bridge.error());

    auto const account = LedgerEntryHelpers::requiredAccountID(
        params, jss::bridge_account, "malformedBridgeAccount");
    if (!account)
        return Unexpected(account.error());

    STXChainBridge::ChainType const chainType =
        STXChainBridge::srcChain(account.value() == bridge->lockingChainDoor());
    if (account.value() != bridge->door(chainType))
        return LedgerEntryHelpers::malformedError("malformedRequest", "");

    return keylet::bridge(*bridge, chainType).key;
}

static Expected<uint256, json::Value>
parseCheck(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    return parseObjectID(params, fieldName, "hex string");
}

static Expected<uint256, json::Value>
parseCredential(
    json::Value const& cred,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!cred.isObject())
    {
        return parseObjectID(cred, fieldName);
    }

    auto const subject =
        LedgerEntryHelpers::requiredAccountID(cred, jss::subject, "malformedRequest");
    if (!subject)
        return Unexpected(subject.error());

    auto const issuer =
        LedgerEntryHelpers::requiredAccountID(cred, jss::issuer, "malformedRequest");
    if (!issuer)
        return Unexpected(issuer.error());

    auto const credType = LedgerEntryHelpers::requiredHexBlob(
        cred, jss::credential_type, kMaxCredentialTypeLength, "malformedRequest");
    if (!credType)
        return Unexpected(credType.error());

    return keylet::credential(*subject, *issuer, Slice(credType->data(), credType->size())).key;
}

static Expected<uint256, json::Value>
parseDelegate(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName);
    }

    auto const account =
        LedgerEntryHelpers::requiredAccountID(params, jss::account, "malformedAddress");
    if (!account)
        return Unexpected(account.error());

    auto const authorize =
        LedgerEntryHelpers::requiredAccountID(params, jss::authorize, "malformedAddress");
    if (!authorize)
        return Unexpected(authorize.error());

    return keylet::delegate(*account, *authorize).key;
}

static Expected<STArray, json::Value>
parseAuthorizeCredentials(json::Value const& jv)
{
    if (!jv.isArray())
    {
        return LedgerEntryHelpers::invalidFieldError(
            "malformedAuthorizedCredentials", jss::authorized_credentials, "array");
    }

    std::uint32_t const n = jv.size();
    if (n > kMaxCredentialsArraySize)
    {
        return Unexpected(
            LedgerEntryHelpers::malformedError(
                "malformedAuthorizedCredentials",
                "Invalid field '" + std::string(jss::authorized_credentials) +
                    "', array too long."));
    }

    if (n == 0)
    {
        return Unexpected(
            LedgerEntryHelpers::malformedError(
                "malformedAuthorizedCredentials",
                "Invalid field '" + std::string(jss::authorized_credentials) + "', array empty."));
    }

    STArray arr(sfAuthorizeCredentials, n);
    for (auto const& jo : jv)
    {
        if (!jo.isObject())
        {
            return LedgerEntryHelpers::invalidFieldError(
                "malformedAuthorizedCredentials", jss::authorized_credentials, "array of objects");
        }

        if (auto const value = LedgerEntryHelpers::hasRequired(
                jo, {jss::issuer, jss::credential_type}, "malformedAuthorizedCredentials");
            !value)
        {
            return Unexpected(value.error());
        }

        auto const issuer = LedgerEntryHelpers::requiredAccountID(
            jo, jss::issuer, "malformedAuthorizedCredentials");
        if (!issuer)
            return Unexpected(issuer.error());

        auto const credentialType = LedgerEntryHelpers::requiredHexBlob(
            jo, jss::credential_type, kMaxCredentialTypeLength, "malformedAuthorizedCredentials");
        if (!credentialType)
            return Unexpected(credentialType.error());

        auto credential = STObject::makeInnerObject(sfCredential);
        credential.setAccountID(sfIssuer, *issuer);
        credential.setFieldVL(sfCredentialType, *credentialType);
        arr.pushBack(std::move(credential));
    }

    return arr;
}

static Expected<uint256, json::Value>
parseDepositPreauth(
    json::Value const& dp,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!dp.isObject())
    {
        return parseObjectID(dp, fieldName);
    }

    if ((dp.isMember(jss::authorized) == dp.isMember(jss::authorized_credentials)))
    {
        return LedgerEntryHelpers::malformedError(
            "malformedRequest",
            "Must have exactly one of `authorized` and "
            "`authorized_credentials`.");
    }

    auto const owner = LedgerEntryHelpers::requiredAccountID(dp, jss::owner, "malformedOwner");
    if (!owner)
    {
        return Unexpected(owner.error());
    }

    if (dp.isMember(jss::authorized))
    {
        if (auto const authorized = LedgerEntryHelpers::parse<AccountID>(dp[jss::authorized]))
        {
            return keylet::depositPreauth(*owner, *authorized).key;
        }
        return LedgerEntryHelpers::invalidFieldError(
            "malformedAuthorized", jss::authorized, "AccountID");
    }

    auto const& ac(dp[jss::authorized_credentials]);
    auto const arr = parseAuthorizeCredentials(ac);
    if (!arr.has_value())
        return Unexpected(arr.error());

    auto const& sorted = credentials::makeSorted(arr.value());
    if (sorted.empty())
    {
        // TODO: this error message is bad/inaccurate
        return LedgerEntryHelpers::invalidFieldError(
            "malformedAuthorizedCredentials", jss::authorized_credentials, "array");
    }

    return keylet::depositPreauth(*owner, sorted).key;
}

static Expected<uint256, json::Value>
parseDID(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    auto const account = LedgerEntryHelpers::parse<AccountID>(params);
    if (!account)
    {
        return LedgerEntryHelpers::invalidFieldError("malformedAddress", fieldName, "AccountID");
    }

    return keylet::did(*account).key;
}

static Expected<uint256, json::Value>
parseDirectoryNode(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName);
    }

    if (params.isMember(jss::sub_index) &&
        (!params[jss::sub_index].isConvertibleTo(json::ValueType::UInt) ||
         params[jss::sub_index].isBool()))
    {
        return LedgerEntryHelpers::invalidFieldError("malformedRequest", jss::sub_index, "number");
    }

    if (params.isMember(jss::owner) == params.isMember(jss::dir_root))
    {
        return LedgerEntryHelpers::malformedError(
            "malformedRequest", "Must have exactly one of `owner` and `dir_root` fields.");
    }

    std::uint64_t const uSubIndex = params.get(jss::sub_index, 0).asUInt();

    if (params.isMember(jss::dir_root))
    {
        if (auto const uDirRoot = LedgerEntryHelpers::parse<uint256>(params[jss::dir_root]))
        {
            return keylet::page(*uDirRoot, uSubIndex).key;
        }

        return LedgerEntryHelpers::invalidFieldError("malformedDirRoot", jss::dir_root, "hash");
    }

    if (params.isMember(jss::owner))
    {
        auto const ownerID = LedgerEntryHelpers::parse<AccountID>(params[jss::owner]);
        if (!ownerID)
        {
            return LedgerEntryHelpers::invalidFieldError(
                "malformedAddress", jss::owner, "AccountID");
        }

        return keylet::page(keylet::ownerDir(*ownerID), uSubIndex).key;
    }

    return LedgerEntryHelpers::malformedError("malformedRequest", "");
}

static Expected<uint256, json::Value>
parseEscrow(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName);
    }

    auto const id = LedgerEntryHelpers::requiredAccountID(params, jss::owner, "malformedOwner");
    if (!id)
        return Unexpected(id.error());
    auto const seq = LedgerEntryHelpers::requiredUInt32(params, jss::seq, "malformedSeq");
    if (!seq)
        return Unexpected(seq.error());

    return keylet::escrow(*id, *seq).key;
}

auto const parseFeeSettings = fixed(keylet::fees());

static Expected<uint256, json::Value>
parseFixed(
    Keylet const& keylet,
    json::Value const& params,
    json::StaticString const& fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!params.isBool())
    {
        return parseObjectID(params, fieldName, "hex string");
    }
    if (!params.asBool())
    {
        return LedgerEntryHelpers::invalidFieldError("invalidParams", fieldName, "true");
    }

    return keylet.key;
}

static Expected<uint256, json::Value>
parseLedgerHashes(
    json::Value const& params,
    json::StaticString const fieldName,
    unsigned const apiVersion)
{
    if (params.isUInt() || params.isInt())
    {
        // If the index doesn't parse as a UInt, throw
        auto const index = params.asUInt();

        // Return the "long" skip list for the given ledger index.
        auto const keylet = keylet::skip(index);
        return keylet.key;
    }
    // Return the key in `params` or the "short" skip list, which contains
    // hashes since the last flag ledger.
    return parseFixed(keylet::skip(), params, fieldName, apiVersion);
}

static Expected<uint256, json::Value>
parseLoanBroker(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName, "hex string");
    }

    auto const id = LedgerEntryHelpers::requiredAccountID(params, jss::owner, "malformedOwner");
    if (!id)
        return Unexpected(id.error());
    auto const seq = LedgerEntryHelpers::requiredUInt32(params, jss::seq, "malformedSeq");
    if (!seq)
        return Unexpected(seq.error());

    return keylet::loanbroker(*id, *seq).key;
}

static Expected<uint256, json::Value>
parseLoan(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName, "hex string");
    }

    auto const id =
        LedgerEntryHelpers::requiredUInt256(params, jss::loan_broker_id, "malformedBroker");
    if (!id)
        return Unexpected(id.error());
    auto const seq = LedgerEntryHelpers::requiredUInt32(params, jss::loan_seq, "malformedSeq");
    if (!seq)
        return Unexpected(seq.error());

    return keylet::loan(*id, *seq).key;
}

static Expected<uint256, json::Value>
parseMPToken(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName);
    }

    auto const mptIssuanceID =
        LedgerEntryHelpers::requiredUInt192(params, jss::mpt_issuance_id, "malformedMPTIssuanceID");
    if (!mptIssuanceID)
        return Unexpected(mptIssuanceID.error());

    auto const account =
        LedgerEntryHelpers::requiredAccountID(params, jss::account, "malformedAccount");
    if (!account)
        return Unexpected(account.error());

    return keylet::mptoken(*mptIssuanceID, *account).key;
}

static Expected<uint256, json::Value>
parseMPTokenIssuance(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    auto const mptIssuanceID = LedgerEntryHelpers::parse<uint192>(params);
    if (!mptIssuanceID)
    {
        return LedgerEntryHelpers::invalidFieldError(
            "malformedMPTokenIssuance", fieldName, "Hash192");
    }

    return keylet::mptIssuance(*mptIssuanceID).key;
}

static Expected<uint256, json::Value>
parseNFTokenOffer(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    return parseObjectID(params, fieldName, "hex string");
}

static Expected<uint256, json::Value>
parseNFTokenPage(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    return parseObjectID(params, fieldName, "hex string");
}

auto const parseNegativeUNL = fixed(keylet::negativeUNL());

static Expected<uint256, json::Value>
parseOffer(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName);
    }

    auto const id = LedgerEntryHelpers::requiredAccountID(params, jss::account, "malformedAddress");
    if (!id)
        return Unexpected(id.error());

    auto const seq = LedgerEntryHelpers::requiredUInt32(params, jss::seq, "malformedRequest");
    if (!seq)
        return Unexpected(seq.error());

    return keylet::offer(*id, *seq).key;
}

static Expected<uint256, json::Value>
parseOracle(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName);
    }

    auto const id = LedgerEntryHelpers::requiredAccountID(params, jss::account, "malformedAccount");
    if (!id)
        return Unexpected(id.error());

    auto const seq =
        LedgerEntryHelpers::requiredUInt32(params, jss::oracle_document_id, "malformedDocumentID");
    if (!seq)
        return Unexpected(seq.error());

    return keylet::oracle(*id, *seq).key;
}

static Expected<uint256, json::Value>
parsePayChannel(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    return parseObjectID(params, fieldName, "hex string");
}

static Expected<uint256, json::Value>
parsePermissionedDomain(
    json::Value const& pd,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (pd.isString())
    {
        return parseObjectID(pd, fieldName);
    }

    if (!pd.isObject())
    {
        return LedgerEntryHelpers::invalidFieldError(
            "malformedRequest", fieldName, "hex string or object");
    }

    auto const account =
        LedgerEntryHelpers::requiredAccountID(pd, jss::account, "malformedAddress");
    if (!account)
        return Unexpected(account.error());

    auto const seq = LedgerEntryHelpers::requiredUInt32(pd, jss::seq, "malformedRequest");
    if (!seq)
        return Unexpected(seq.error());

    return keylet::permissionedDomain(*account, pd[jss::seq].asUInt()).key;
}

static Expected<uint256, json::Value>
parseRippleState(
    json::Value const& jvRippleState,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    Currency uCurrency;

    if (!jvRippleState.isObject())
    {
        return parseObjectID(jvRippleState, fieldName);
    }

    if (auto const value =
            LedgerEntryHelpers::hasRequired(jvRippleState, {jss::currency, jss::accounts});
        !value)
    {
        return Unexpected(value.error());
    }

    if (!jvRippleState[jss::accounts].isArray() || jvRippleState[jss::accounts].size() != 2)
    {
        return LedgerEntryHelpers::invalidFieldError(
            "malformedRequest", jss::accounts, "length-2 array of Accounts");
    }

    auto const id1 = LedgerEntryHelpers::parse<AccountID>(jvRippleState[jss::accounts][0u]);
    auto const id2 = LedgerEntryHelpers::parse<AccountID>(jvRippleState[jss::accounts][1u]);
    if (!id1 || !id2)
    {
        return LedgerEntryHelpers::invalidFieldError(
            "malformedAddress", jss::accounts, "array of Accounts");
    }
    if (id1 == id2)
    {
        return LedgerEntryHelpers::malformedError(
            "malformedRequest", "Cannot have a trustline to self.");
    }

    if (!jvRippleState[jss::currency].isString() || jvRippleState[jss::currency] == "" ||
        !toCurrency(uCurrency, jvRippleState[jss::currency].asString()))
    {
        return LedgerEntryHelpers::invalidFieldError(
            "malformedCurrency", jss::currency, "Currency");
    }

    return keylet::line(*id1, *id2, uCurrency).key;
}

static Expected<uint256, json::Value>
parseSignerList(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    return parseObjectID(params, fieldName, "hex string");
}

static Expected<uint256, json::Value>
parseTicket(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName);
    }

    auto const id = LedgerEntryHelpers::requiredAccountID(params, jss::account, "malformedAddress");
    if (!id)
        return Unexpected(id.error());

    auto const seq =
        LedgerEntryHelpers::requiredUInt32(params, jss::ticket_seq, "malformedRequest");
    if (!seq)
        return Unexpected(seq.error());

    return getTicketIndex(*id, *seq);
}

static Expected<uint256, json::Value>
parseVault(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName);
    }

    auto const id = LedgerEntryHelpers::requiredAccountID(params, jss::owner, "malformedOwner");
    if (!id)
        return Unexpected(id.error());

    auto const seq = LedgerEntryHelpers::requiredUInt32(params, jss::seq, "malformedRequest");
    if (!seq)
        return Unexpected(seq.error());

    return keylet::vault(*id, *seq).key;
}

static Expected<uint256, json::Value>
parseXChainOwnedClaimID(
    json::Value const& claimId,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!claimId.isObject())
    {
        return parseObjectID(claimId, fieldName);
    }

    auto const bridgeSpec = LedgerEntryHelpers::parseBridgeFields(claimId);
    if (!bridgeSpec)
        return Unexpected(bridgeSpec.error());

    auto const seq = LedgerEntryHelpers::requiredUInt32(
        claimId, jss::xchain_owned_claim_id, "malformedXChainOwnedClaimID");
    if (!seq)
    {
        return Unexpected(seq.error());
    }

    Keylet keylet = keylet::xChainClaimID(*bridgeSpec, *seq);
    return keylet.key;
}

static Expected<uint256, json::Value>
parseXChainOwnedCreateAccountClaimID(
    json::Value const& claimId,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!claimId.isObject())
    {
        return parseObjectID(claimId, fieldName);
    }

    auto const bridgeSpec = LedgerEntryHelpers::parseBridgeFields(claimId);
    if (!bridgeSpec)
        return Unexpected(bridgeSpec.error());

    auto const seq = LedgerEntryHelpers::requiredUInt32(
        claimId,
        jss::xchain_owned_create_account_claim_id,
        "malformedXChainOwnedCreateAccountClaimID");
    if (!seq)
    {
        return Unexpected(seq.error());
    }

    Keylet keylet = keylet::xChainCreateAccountClaimID(*bridgeSpec, *seq);
    return keylet.key;
}

struct LedgerEntry
{
    json::StaticString fieldName;
    FunctionType parseFunction;
    LedgerEntryType expectedType;
};

// {
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
//   ...
// }
json::Value
doLedgerEntry(RPC::JsonContext& context)
{
    static auto kLedgerEntryParsers = std::to_array<LedgerEntry>({
#pragma push_macro("LEDGER_ENTRY")
#undef LEDGER_ENTRY

#define LEDGER_ENTRY(tag, value, name, rpcName, fields) {jss::rpcName, parse##name, tag},

#include <xrpl/protocol/detail/ledger_entries.macro>

#undef LEDGER_ENTRY
#pragma pop_macro("LEDGER_ENTRY")
        {.fieldName = jss::index, .parseFunction = parseIndex, .expectedType = ltANY},
        // aliases
        {.fieldName = jss::account_root,
         .parseFunction = parseAccountRoot,
         .expectedType = ltACCOUNT_ROOT},
        {.fieldName = jss::ripple_state,
         .parseFunction = parseRippleState,
         .expectedType = ltRIPPLE_STATE},
    });

    auto const hasMoreThanOneMember = [&]() {
        int count = 0;

        for (auto const& ledgerEntry : kLedgerEntryParsers)
        {
            if (context.params.isMember(ledgerEntry.fieldName))
            {
                count++;
                if (count > 1)  // Early exit if more than one is found
                    return true;
            }
        }
        return false;  // Return false if <= 1 is found
    }();

    if (hasMoreThanOneMember)
    {
        return RPC::makeParamError("Too many fields provided.");
    }

    std::shared_ptr<ReadView const> lpLedger;
    auto jvResult = RPC::lookupLedger(lpLedger, context);

    if (!lpLedger)
        return jvResult;

    uint256 uNodeIndex;
    LedgerEntryType expectedType = ltANY;

    try
    {
        bool found = false;
        for (auto const& ledgerEntry : kLedgerEntryParsers)
        {
            if (context.params.isMember(ledgerEntry.fieldName))
            {
                expectedType = ledgerEntry.expectedType;
                // `Bridge` is the only type that involves two fields at the
                // `ledger_entry` param level.
                // So that parser needs to have the whole `params` field.
                // All other parsers only need the one field name's info.
                json::Value const& params = ledgerEntry.fieldName == jss::bridge
                    ? context.params
                    : context.params[ledgerEntry.fieldName];
                auto const result =
                    ledgerEntry.parseFunction(params, ledgerEntry.fieldName, context.apiVersion);
                if (!result)
                    return result.error();

                uNodeIndex = result.value();
                found = true;
                break;
            }
        }
        if (!found)
        {
            if (context.apiVersion < 2u)
            {
                jvResult[jss::error] = "unknownOption";
                return jvResult;
            }
            return RPC::makeParamError("No ledger_entry params provided.");
        }
    }
    catch (json::Error const& e)
    {
        if (context.apiVersion > 1u)
        {
            // For apiVersion 2 onwards, any parsing failures that throw
            // this exception return an invalidParam error.
            return RPC::makeError(RpcInvalidParams);
        }

        throw;
    }

    // Return the computed index regardless of whether the node exists.
    jvResult[jss::index] = to_string(uNodeIndex);

    if (uNodeIndex.isZero())
    {
        RPC::injectError(RpcEntryNotFound, jvResult);
        return jvResult;
    }

    auto const sleNode = lpLedger->read(keylet::unchecked(uNodeIndex));

    bool bNodeBinary = false;
    if (context.params.isMember(jss::binary))
        bNodeBinary = context.params[jss::binary].asBool();

    if (!sleNode)
    {
        // Not found.
        RPC::injectError(RpcEntryNotFound, jvResult);
        return jvResult;
    }

    if ((expectedType != ltANY) && (expectedType != sleNode->getType()))
    {
        RPC::injectError(RpcUnexpectedLedgerType, jvResult);
        return jvResult;
    }

    if (bNodeBinary)
    {
        Serializer s;

        sleNode->add(s);

        jvResult[jss::node_binary] = strHex(s.peekData());
    }
    else
    {
        jvResult[jss::node] = sleNode->getJson(JsonOptions::Values::None);
    }

    return jvResult;
}

std::pair<org::xrpl::rpc::v1::GetLedgerEntryResponse, grpc::Status>
doLedgerEntryGrpc(RPC::GRPCContext<org::xrpl::rpc::v1::GetLedgerEntryRequest>& context)
{
    org::xrpl::rpc::v1::GetLedgerEntryRequest const& request = context.params;
    org::xrpl::rpc::v1::GetLedgerEntryResponse response;
    grpc::Status const status = grpc::Status::OK;

    std::shared_ptr<ReadView const> ledger;
    if (auto status = RPC::ledgerFromRequest(ledger, context))
    {
        grpc::Status errorStatus;
        if (status.toErrorCode() == RpcInvalidParams)
        {
            errorStatus = grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, status.message());
        }
        else
        {
            errorStatus = grpc::Status(grpc::StatusCode::NOT_FOUND, status.message());
        }
        return {response, errorStatus};
    }

    auto const key = uint256::fromVoidChecked(request.key());
    if (!key)
    {
        grpc::Status const errorStatus{grpc::StatusCode::INVALID_ARGUMENT, "index malformed"};
        return {response, errorStatus};
    }

    auto const sleNode = ledger->read(keylet::unchecked(*key));
    if (!sleNode)
    {
        grpc::Status const errorStatus{grpc::StatusCode::NOT_FOUND, "object not found"};
        return {response, errorStatus};
    }

    Serializer s;
    sleNode->add(s);

    auto& stateObject = *response.mutable_ledger_object();
    stateObject.set_data(s.peekData().data(), s.getLength());
    stateObject.set_key(request.key());
    *(response.mutable_ledger()) = request.ledger();
    return {response, status};
}
}  // namespace xrpl
