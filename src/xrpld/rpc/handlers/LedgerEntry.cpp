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

#include <xrpld/app/misc/CredentialHelpers.h>
#include <xrpld/ledger/ReadView.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/GRPCHandlers.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/handlers/LedgerEntryHelpers.h>

#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/json/json_errors.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/jss.h>

#include <functional>

namespace ripple {

static Expected<uint256, Json::Value>
parseObjectID(
    Json::Value const& params,
    Json::StaticString const& fieldName,
    std::string const& expectedType = "hex string or object")
{
    if (auto const uNodeIndex = parse<uint256>(params))
    {
        return *uNodeIndex;
    }
    return invalidFieldError("malformedRequest", fieldName, expectedType);
}

static Expected<uint256, Json::Value>
parseIndex(Json::Value const& params, Json::StaticString const& fieldName)
{
    return parseObjectID(params, fieldName, "hex string");
}

static Expected<uint256, Json::Value>
parseAccountRoot(Json::Value const& params, Json::StaticString const& fieldName)
{
    if (auto const account = parse<AccountID>(params))
    {
        return keylet::account(*account).key;
    }

    return invalidFieldError("malformedAddress", fieldName, "AccountID");
}

static Expected<uint256, Json::Value>
parseAmendments(Json::Value const& params, Json::StaticString const& fieldName)
{
    return parseObjectID(params, fieldName, "hex string");
}

static Expected<uint256, Json::Value>
parseAMM(Json::Value const& params, Json::StaticString const& fieldName)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName);
    }

    if (auto const value = hasRequired(params, {jss::asset, jss::asset2});
        !value)
    {
        return Unexpected(value.error());
    }

    try
    {
        auto const issue = issueFromJson(params[jss::asset]);
        auto const issue2 = issueFromJson(params[jss::asset2]);
        return keylet::amm(issue, issue2).key;
    }
    catch (std::runtime_error const&)
    {
        return malformedError("malformedRequest", "");
    }
}

static Expected<uint256, Json::Value>
parseBridge(Json::Value const& params, Json::StaticString const& fieldName)
{
    if (!params.isMember(jss::bridge))
    {
        return Unexpected(missingFieldError(jss::bridge));
    }

    if (params[jss::bridge].isString())
    {
        return parseObjectID(params, fieldName);
    }

    auto const bridge = parseBridgeFields(params[jss::bridge]);
    if (!bridge)
        return Unexpected(bridge.error());

    auto const account = requiredAccountID(
        params, jss::bridge_account, "malformedBridgeAccount");
    if (!account)
        return Unexpected(account.error());

    STXChainBridge::ChainType const chainType =
        STXChainBridge::srcChain(account.value() == bridge->lockingChainDoor());
    if (account.value() != bridge->door(chainType))
        return malformedError("malformedRequest", "");

    return keylet::bridge(*bridge, chainType).key;
}

static Expected<uint256, Json::Value>
parseCheck(Json::Value const& params, Json::StaticString const& fieldName)
{
    return parseObjectID(params, fieldName, "hex string");
}

static Expected<uint256, Json::Value>
parseCredential(Json::Value const& cred, Json::StaticString const& fieldName)
{
    if (!cred.isObject())
    {
        return parseObjectID(cred, fieldName);
    }

    auto const subject =
        requiredAccountID(cred, jss::subject, "malformedRequest");
    if (!subject)
        return Unexpected(subject.error());

    auto const issuer =
        requiredAccountID(cred, jss::issuer, "malformedRequest");
    if (!issuer)
        return Unexpected(issuer.error());

    auto const credType = requiredHexBlob(
        cred,
        jss::credential_type,
        maxCredentialTypeLength,
        "malformedRequest");
    if (!credType)
        return Unexpected(credType.error());

    return keylet::credential(
               *subject, *issuer, Slice(credType->data(), credType->size()))
        .key;
}

static Expected<uint256, Json::Value>
parseDelegate(Json::Value const& params, Json::StaticString const& fieldName)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName);
    }

    auto const account =
        requiredAccountID(params, jss::account, "malformedAddress");
    if (!account)
        return Unexpected(account.error());

    auto const authorize =
        requiredAccountID(params, jss::authorize, "malformedAddress");
    if (!authorize)
        return Unexpected(authorize.error());

    return keylet::delegate(*account, *authorize).key;
}

static Expected<STArray, Json::Value>
parseAuthorizeCredentials(Json::Value const& jv)
{
    if (!jv.isArray())
        return invalidFieldError(
            "malformedAuthorizedCredentials",
            jss::authorized_credentials,
            "array");
    STArray arr(sfAuthorizeCredentials, jv.size());
    for (auto const& jo : jv)
    {
        if (!jo.isObject())
            return invalidFieldError(
                "malformedAuthorizedCredentials",
                jss::authorized_credentials,
                "array");
        if (auto const value = hasRequired(
                jo,
                {jss::issuer, jss::credential_type},
                "malformedAuthorizedCredentials");
            !value)
        {
            return Unexpected(value.error());
        }

        auto const issuer = requiredAccountID(
            jo, jss::issuer, "malformedAuthorizedCredentials");
        if (!issuer)
            return Unexpected(issuer.error());

        auto const credentialType = requiredHexBlob(
            jo,
            jss::credential_type,
            maxCredentialTypeLength,
            "malformedAuthorizedCredentials");
        if (!credentialType)
            return Unexpected(credentialType.error());

        auto credential = STObject::makeInnerObject(sfCredential);
        credential.setAccountID(sfIssuer, *issuer);
        credential.setFieldVL(sfCredentialType, *credentialType);
        arr.push_back(std::move(credential));
    }

    return arr;
}

static Expected<uint256, Json::Value>
parseDepositPreauth(Json::Value const& dp, Json::StaticString const& fieldName)
{
    if (!dp.isObject())
    {
        return parseObjectID(dp, fieldName);
    }

    if ((dp.isMember(jss::authorized) ==
         dp.isMember(jss::authorized_credentials)))
    {
        return malformedError(
            "malformedRequest",
            "Must have exactly one of `authorized` and "
            "`authorized_credentials`.");
    }

    auto const owner = requiredAccountID(dp, jss::owner, "malformedOwner");
    if (!owner)
    {
        return Unexpected(owner.error());
    }

    if (dp.isMember(jss::authorized))
    {
        if (auto const authorized = parse<AccountID>(dp[jss::authorized]))
        {
            return keylet::depositPreauth(*owner, *authorized).key;
        }
        return invalidFieldError(
            "malformedAuthorized", jss::authorized, "AccountID");
    }

    auto const& ac(dp[jss::authorized_credentials]);
    auto const arr = parseAuthorizeCredentials(ac);
    if (!arr)
        return Unexpected(arr.error());
    if (arr->empty() || (arr->size() > maxCredentialsArraySize))
    {
        return invalidFieldError(
            "malformedAuthorizedCredentials",
            jss::authorized_credentials,
            "array");
    }

    auto const& sorted = credentials::makeSorted(*arr);
    if (sorted.empty())
    {
        // TODO: this error message is bad/inaccurate
        return invalidFieldError(
            "malformedAuthorizedCredentials",
            jss::authorized_credentials,
            "array");
    }

    return keylet::depositPreauth(*owner, sorted).key;
}

static Expected<uint256, Json::Value>
parseDID(Json::Value const& params, Json::StaticString const& fieldName)
{
    auto const account = parse<AccountID>(params);
    if (!account)
    {
        return invalidFieldError("malformedAddress", fieldName, "AccountID");
    }

    return keylet::did(*account).key;
}

static Expected<uint256, Json::Value>
parseDirectoryNode(
    Json::Value const& params,
    Json::StaticString const& fieldName)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName);
    }

    if (params.isMember(jss::sub_index) &&
        (!params[jss::sub_index].isConvertibleTo(Json::uintValue) ||
         params[jss::sub_index].isBool()))
    {
        return invalidFieldError("malformedRequest", jss::sub_index, "number");
    }

    if (params.isMember(jss::owner) == params.isMember(jss::dir_root))
    {
        return malformedError(
            "malformedRequest",
            "Must have exactly one of `owner` and `dir_root` fields.");
    }

    std::uint64_t uSubIndex = params.get(jss::sub_index, 0).asUInt();

    if (params.isMember(jss::dir_root))
    {
        if (auto const uDirRoot = parse<uint256>(params[jss::dir_root]))
        {
            return keylet::page(*uDirRoot, uSubIndex).key;
        }

        return invalidFieldError("malformedDirRoot", jss::dir_root, "hash");
    }

    if (params.isMember(jss::owner))
    {
        auto const ownerID = parse<AccountID>(params[jss::owner]);
        if (!ownerID)
        {
            return invalidFieldError(
                "malformedAddress", jss::owner, "AccountID");
        }

        return keylet::page(keylet::ownerDir(*ownerID), uSubIndex).key;
    }

    return malformedError("malformedRequest", "");
}

static Expected<uint256, Json::Value>
parseEscrow(Json::Value const& params, Json::StaticString const& fieldName)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName);
    }

    auto const id = requiredAccountID(params, jss::owner, "malformedOwner");
    if (!id)
        return Unexpected(id.error());
    auto const seq = requiredUInt32(params, jss::seq, "malformedSeq");
    if (!seq)
        return Unexpected(seq.error());

    return keylet::escrow(*id, *seq).key;
}

static Expected<uint256, Json::Value>
parseFeeSettings(Json::Value const& params, Json::StaticString const& fieldName)
{
    return parseObjectID(params, fieldName, "hex string");
}

static Expected<uint256, Json::Value>
parseLedgerHashes(
    Json::Value const& params,
    Json::StaticString const& fieldName)
{
    return parseObjectID(params, fieldName, "hex string");
}

static Expected<uint256, Json::Value>
parseMPToken(Json::Value const& params, Json::StaticString const& fieldName)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName);
    }

    auto const mptIssuanceID =
        requiredUInt192(params, jss::mpt_issuance_id, "malformedMPTIssuanceID");
    if (!mptIssuanceID)
        return Unexpected(mptIssuanceID.error());

    auto const account =
        requiredAccountID(params, jss::account, "malformedAccount");
    if (!account)
        return Unexpected(account.error());

    return keylet::mptoken(*mptIssuanceID, *account).key;
}

static Expected<uint256, Json::Value>
parseMPTokenIssuance(
    Json::Value const& params,
    Json::StaticString const& fieldName)
{
    auto const mptIssuanceID = parse<uint192>(params);
    if (!mptIssuanceID)
        return invalidFieldError(
            "malformedMPTokenIssuance", fieldName, "Hash192");

    return keylet::mptIssuance(*mptIssuanceID).key;
}

static Expected<uint256, Json::Value>
parseNFTokenOffer(
    Json::Value const& params,
    Json::StaticString const& fieldName)
{
    return parseObjectID(params, fieldName, "hex string");
}

static Expected<uint256, Json::Value>
parseNFTokenPage(Json::Value const& params, Json::StaticString const& fieldName)
{
    return parseObjectID(params, fieldName, "hex string");
}

static Expected<uint256, Json::Value>
parseNegativeUNL(Json::Value const& params, Json::StaticString const& fieldName)
{
    return parseObjectID(params, fieldName, "hex string");
}

static Expected<uint256, Json::Value>
parseOffer(Json::Value const& params, Json::StaticString const& fieldName)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName);
    }

    auto const id = requiredAccountID(params, jss::account, "malformedAddress");
    if (!id)
        return Unexpected(id.error());

    auto const seq = requiredUInt32(params, jss::seq, "malformedRequest");
    if (!seq)
        return Unexpected(seq.error());

    return keylet::offer(*id, *seq).key;
}

static Expected<uint256, Json::Value>
parseOracle(Json::Value const& params, Json::StaticString const& fieldName)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName);
    }

    auto const id = requiredAccountID(params, jss::account, "malformedAccount");
    if (!id)
        return Unexpected(id.error());

    auto const seq =
        requiredUInt32(params, jss::oracle_document_id, "malformedDocumentID");
    if (!seq)
        return Unexpected(seq.error());

    return keylet::oracle(*id, *seq).key;
}

static Expected<uint256, Json::Value>
parsePayChannel(Json::Value const& params, Json::StaticString const& fieldName)
{
    return parseObjectID(params, fieldName, "hex string");
}

static Expected<uint256, Json::Value>
parsePermissionedDomain(
    Json::Value const& pd,
    Json::StaticString const& fieldName)
{
    if (pd.isString())
    {
        return parseObjectID(pd, fieldName);
    }

    if (!pd.isObject())
    {
        return invalidFieldError(
            "malformedRequest", fieldName, "hex string or object");
    }

    auto const account =
        requiredAccountID(pd, jss::account, "malformedAddress");
    if (!account)
        return Unexpected(account.error());

    auto const seq = requiredUInt32(pd, jss::seq, "malformedRequest");
    if (!seq)
        return Unexpected(seq.error());

    return keylet::permissionedDomain(*account, pd[jss::seq].asUInt()).key;
}

static Expected<uint256, Json::Value>
parseRippleState(
    Json::Value const& jvRippleState,
    Json::StaticString const& fieldName)
{
    Currency uCurrency;

    if (!jvRippleState.isObject())
    {
        return parseObjectID(jvRippleState, fieldName);
    }

    if (auto const value =
            hasRequired(jvRippleState, {jss::currency, jss::accounts});
        !value)
    {
        return Unexpected(value.error());
    }

    if (!jvRippleState[jss::accounts].isArray() ||
        jvRippleState[jss::accounts].size() != 2)
    {
        return invalidFieldError(
            "malformedRequest", jss::accounts, "length-2 array of Accounts");
    }

    auto const id1 = parse<AccountID>(jvRippleState[jss::accounts][0u]);
    auto const id2 = parse<AccountID>(jvRippleState[jss::accounts][1u]);
    if (!id1 || !id2)
    {
        return invalidFieldError(
            "malformedAddress", jss::accounts, "array of Accounts");
    }
    if (id1 == id2)
    {
        return malformedError(
            "malformedRequest", "Cannot have a trustline to self.");
    }

    if (!jvRippleState[jss::currency].isString() ||
        jvRippleState[jss::currency] == "" ||
        !to_currency(uCurrency, jvRippleState[jss::currency].asString()))
    {
        return invalidFieldError(
            "malformedCurrency", jss::currency, "Currency");
    }

    return keylet::line(*id1, *id2, uCurrency).key;
}

static Expected<uint256, Json::Value>
parseSignerList(Json::Value const& params, Json::StaticString const& fieldName)
{
    return parseObjectID(params, fieldName, "hex string");
}

static Expected<uint256, Json::Value>
parseTicket(Json::Value const& params, Json::StaticString const& fieldName)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName);
    }

    auto const id = requiredAccountID(params, jss::account, "malformedAddress");
    if (!id)
        return Unexpected(id.error());

    auto const seq =
        requiredUInt32(params, jss::ticket_seq, "malformedRequest");
    if (!seq)
        return Unexpected(seq.error());

    return getTicketIndex(*id, *seq);
}

static Expected<uint256, Json::Value>
parseVault(Json::Value const& params, Json::StaticString const& fieldName)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName);
    }

    auto const id = requiredAccountID(params, jss::owner, "malformedOwner");
    if (!id)
        return Unexpected(id.error());

    auto const seq = requiredUInt32(params, jss::seq, "malformedRequest");
    if (!seq)
        return Unexpected(seq.error());

    return keylet::vault(*id, *seq).key;
}

static Expected<uint256, Json::Value>
parseXChainOwnedClaimID(
    Json::Value const& claim_id,
    Json::StaticString const& fieldName)
{
    if (!claim_id.isObject())
    {
        return parseObjectID(claim_id, fieldName);
    }

    auto const bridge_spec = parseBridgeFields(claim_id);
    if (!bridge_spec)
        return Unexpected(bridge_spec.error());

    auto const seq = requiredUInt32(
        claim_id, jss::xchain_owned_claim_id, "malformedXChainOwnedClaimID");
    if (!seq)
    {
        return Unexpected(seq.error());
    }

    Keylet keylet = keylet::xChainClaimID(*bridge_spec, *seq);
    return keylet.key;
}

static Expected<uint256, Json::Value>
parseXChainOwnedCreateAccountClaimID(
    Json::Value const& claim_id,
    Json::StaticString const& fieldName)
{
    if (!claim_id.isObject())
    {
        return parseObjectID(claim_id, fieldName);
    }

    auto const bridge_spec = parseBridgeFields(claim_id);
    if (!bridge_spec)
        return Unexpected(bridge_spec.error());

    auto const seq = requiredUInt32(
        claim_id,
        jss::xchain_owned_create_account_claim_id,
        "malformedXChainOwnedCreateAccountClaimID");
    if (!seq)
    {
        return Unexpected(seq.error());
    }

    Keylet keylet = keylet::xChainCreateAccountClaimID(*bridge_spec, *seq);
    return keylet.key;
}

using FunctionType = Expected<uint256, Json::Value> (*)(
    Json::Value const&,
    Json::StaticString const&);

struct LedgerEntry
{
    Json::StaticString fieldName;
    FunctionType parseFunction;
    LedgerEntryType expectedType;
};

// {
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
//   ...
// }
Json::Value
doLedgerEntry(RPC::JsonContext& context)
{
    static auto ledgerEntryParsers = std::to_array<LedgerEntry>({
#pragma push_macro("LEDGER_ENTRY")
#undef LEDGER_ENTRY

#define LEDGER_ENTRY(tag, value, name, rpcName, fields) \
    {jss::rpcName, parse##name, tag},

#include <xrpl/protocol/detail/ledger_entries.macro>

#undef LEDGER_ENTRY
#pragma pop_macro("LEDGER_ENTRY")
        {jss::index, parseIndex, ltANY},
        {jss::account_root, parseAccountRoot, ltACCOUNT_ROOT},
        {jss::ripple_state, parseRippleState, ltRIPPLE_STATE},
    });

    auto hasMoreThanOneMember = [&]() {
        int count = 0;

        for (auto const& ledgerEntry : ledgerEntryParsers)
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
        return RPC::make_param_error("Too many fields provided.");
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
        for (auto const& ledgerEntry : ledgerEntryParsers)
        {
            if (context.params.isMember(ledgerEntry.fieldName))
            {
                expectedType = ledgerEntry.expectedType;
                // `Bridge` is the only type that involves two fields at the
                // `ledger_entry` param level.
                // So that parser needs to have the whole `params` field.
                // All other parsers only need the one field name's info.
                Json::Value const& params = ledgerEntry.fieldName == jss::bridge
                    ? context.params
                    : context.params[ledgerEntry.fieldName];
                auto const result =
                    ledgerEntry.parseFunction(params, ledgerEntry.fieldName);
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
            return RPC::make_param_error("No ledger_entry params provided.");
        }
    }
    catch (Json::error& e)
    {
        if (context.apiVersion > 1u)
        {
            // For apiVersion 2 onwards, any parsing failures that throw
            // this exception return an invalidParam error.
            return RPC::make_error(rpcINVALID_PARAMS);
        }
        else
            throw;
    }

    if (uNodeIndex.isZero())
    {
        return RPC::make_error(rpcENTRY_NOT_FOUND);
    }

    auto const sleNode = lpLedger->read(keylet::unchecked(uNodeIndex));

    bool bNodeBinary = false;
    if (context.params.isMember(jss::binary))
        bNodeBinary = context.params[jss::binary].asBool();

    if (!sleNode)
    {
        // Not found.
        return RPC::make_error(rpcENTRY_NOT_FOUND);
    }

    if ((expectedType != ltANY) && (expectedType != sleNode->getType()))
    {
        return RPC::make_error(rpcUNEXPECTED_LEDGER_TYPE);
    }

    if (bNodeBinary)
    {
        Serializer s;

        sleNode->add(s);

        jvResult[jss::node_binary] = strHex(s.peekData());
        jvResult[jss::index] = to_string(uNodeIndex);
    }
    else
    {
        jvResult[jss::node] = sleNode->getJson(JsonOptions::none);
        jvResult[jss::index] = to_string(uNodeIndex);
    }

    return jvResult;
}

std::pair<org::xrpl::rpc::v1::GetLedgerEntryResponse, grpc::Status>
doLedgerEntryGrpc(
    RPC::GRPCContext<org::xrpl::rpc::v1::GetLedgerEntryRequest>& context)
{
    org::xrpl::rpc::v1::GetLedgerEntryRequest& request = context.params;
    org::xrpl::rpc::v1::GetLedgerEntryResponse response;
    grpc::Status status = grpc::Status::OK;

    std::shared_ptr<ReadView const> ledger;
    if (auto status = RPC::ledgerFromRequest(ledger, context))
    {
        grpc::Status errorStatus;
        if (status.toErrorCode() == rpcINVALID_PARAMS)
        {
            errorStatus = grpc::Status(
                grpc::StatusCode::INVALID_ARGUMENT, status.message());
        }
        else
        {
            errorStatus =
                grpc::Status(grpc::StatusCode::NOT_FOUND, status.message());
        }
        return {response, errorStatus};
    }

    auto const key = uint256::fromVoidChecked(request.key());
    if (!key)
    {
        grpc::Status errorStatus{
            grpc::StatusCode::INVALID_ARGUMENT, "index malformed"};
        return {response, errorStatus};
    }

    auto const sleNode = ledger->read(keylet::unchecked(*key));
    if (!sleNode)
    {
        grpc::Status errorStatus{
            grpc::StatusCode::NOT_FOUND, "object not found"};
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
}  // namespace ripple
