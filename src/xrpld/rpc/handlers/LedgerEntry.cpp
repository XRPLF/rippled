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

#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/CredentialHelpers.h>
#include <xrpld/ledger/ReadView.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/GRPCHandlers.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/json/json_errors.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/jss.h>
#include <functional>

namespace ripple {

static STArray
parseAuthorizeCredentials(Json::Value const& jv)
{
    STArray arr(sfAuthorizeCredentials, jv.size());
    for (auto const& jo : jv)
    {
        if (!jo.isObject() ||  //
            !jo.isMember(jss::issuer) || !jo[jss::issuer].isString() ||
            !jo.isMember(jss::credential_type) ||
            !jo[jss::credential_type].isString())
            return {};

        auto const issuer = parseBase58<AccountID>(jo[jss::issuer].asString());
        if (!issuer || !*issuer)
            return {};

        auto const credentialType =
            strUnHex(jo[jss::credential_type].asString());
        if (!credentialType || credentialType->empty() ||
            credentialType->size() > maxCredentialTypeLength)
            return {};

        auto credential = STObject::makeInnerObject(sfCredential);
        credential.setAccountID(sfIssuer, *issuer);
        credential.setFieldVL(sfCredentialType, *credentialType);
        arr.push_back(std::move(credential));
    }

    return arr;
}

std::optional<uint256>
parseIndex(Json::Value const& params, Json::Value& jvResult)
{
    uint256 uNodeIndex;
    if (!uNodeIndex.parseHex(params.asString()))
    {
        jvResult[jss::error] = "malformedRequest";
        return std::nullopt;
    }

    return uNodeIndex;
}

std::optional<uint256>
parseAccountRoot(Json::Value const& params, Json::Value& jvResult)
{
    auto const account = parseBase58<AccountID>(params.asString());
    if (!account || account->isZero())
    {
        jvResult[jss::error] = "malformedAddress";
        return std::nullopt;
    }

    return keylet::account(*account).key;
}

std::optional<uint256>
parseCheck(Json::Value const& params, Json::Value& jvResult)
{
    uint256 uNodeIndex;
    if (!uNodeIndex.parseHex(params.asString()))
    {
        jvResult[jss::error] = "malformedRequest";
        return std::nullopt;
    }

    return uNodeIndex;
}

std::optional<uint256>
parseDepositPreauth(Json::Value const& dp, Json::Value& jvResult)
{
    if (!dp.isObject())
    {
        uint256 uNodeIndex;
        if (!dp.isString() || !uNodeIndex.parseHex(dp.asString()))
        {
            jvResult[jss::error] = "malformedRequest";
            return std::nullopt;
        }
        return uNodeIndex;
    }

    // clang-format off
    if (
        (!dp.isMember(jss::owner) || !dp[jss::owner].isString()) ||
        (dp.isMember(jss::authorized) == dp.isMember(jss::authorized_credentials)) ||
        (dp.isMember(jss::authorized) && !dp[jss::authorized].isString()) ||
        (dp.isMember(jss::authorized_credentials) && !dp[jss::authorized_credentials].isArray())
        )
    // clang-format on
    {
        jvResult[jss::error] = "malformedRequest";
        return std::nullopt;
    }

    auto const owner = parseBase58<AccountID>(dp[jss::owner].asString());
    if (!owner)
    {
        jvResult[jss::error] = "malformedOwner";
        return std::nullopt;
    }

    if (dp.isMember(jss::authorized))
    {
        auto const authorized =
            parseBase58<AccountID>(dp[jss::authorized].asString());
        if (!authorized)
        {
            jvResult[jss::error] = "malformedAuthorized";
            return std::nullopt;
        }
        return keylet::depositPreauth(*owner, *authorized).key;
    }

    auto const& ac(dp[jss::authorized_credentials]);
    STArray const arr = parseAuthorizeCredentials(ac);

    if (arr.empty() || (arr.size() > maxCredentialsArraySize))
    {
        jvResult[jss::error] = "malformedAuthorizedCredentials";
        return std::nullopt;
    }

    auto const& sorted = credentials::makeSorted(arr);
    if (sorted.empty())
    {
        jvResult[jss::error] = "malformedAuthorizedCredentials";
        return std::nullopt;
    }

    return keylet::depositPreauth(*owner, sorted).key;
}

std::optional<uint256>
parseDirectory(Json::Value const& params, Json::Value& jvResult)
{
    if (params.isNull())
    {
        jvResult[jss::error] = "malformedRequest";
        return std::nullopt;
    }

    if (!params.isObject())
    {
        uint256 uNodeIndex;
        if (!uNodeIndex.parseHex(params.asString()))
        {
            jvResult[jss::error] = "malformedRequest";
            return std::nullopt;
        }
        return uNodeIndex;
    }

    if (params.isMember(jss::sub_index) && !params[jss::sub_index].isIntegral())
    {
        jvResult[jss::error] = "malformedRequest";
        return std::nullopt;
    }

    std::uint64_t uSubIndex =
        params.isMember(jss::sub_index) ? params[jss::sub_index].asUInt() : 0;

    if (params.isMember(jss::dir_root))
    {
        uint256 uDirRoot;

        if (params.isMember(jss::owner))
        {
            // May not specify both dir_root and owner.
            jvResult[jss::error] = "malformedRequest";
            return std::nullopt;
        }

        if (!uDirRoot.parseHex(params[jss::dir_root].asString()))
        {
            jvResult[jss::error] = "malformedRequest";
            return std::nullopt;
        }
        return keylet::page(uDirRoot, uSubIndex).key;
    }

    if (params.isMember(jss::owner))
    {
        auto const ownerID =
            parseBase58<AccountID>(params[jss::owner].asString());

        if (!ownerID)
        {
            jvResult[jss::error] = "malformedAddress";
            return std::nullopt;
        }

        return keylet::page(keylet::ownerDir(*ownerID), uSubIndex).key;
    }

    jvResult[jss::error] = "malformedRequest";
    return std::nullopt;
}

std::optional<uint256>
parseEscrow(Json::Value const& params, Json::Value& jvResult)
{
    if (!params.isObject())
    {
        uint256 uNodeIndex;
        if (!uNodeIndex.parseHex(params.asString()))
        {
            jvResult[jss::error] = "malformedRequest";
            return std::nullopt;
        }

        return uNodeIndex;
    }

    if (!params.isMember(jss::owner) || !params.isMember(jss::seq) ||
        !params[jss::seq].isIntegral())
    {
        jvResult[jss::error] = "malformedRequest";
        return std::nullopt;
    }

    auto const id = parseBase58<AccountID>(params[jss::owner].asString());

    if (!id)
    {
        jvResult[jss::error] = "malformedOwner";
        return std::nullopt;
    }

    return keylet::escrow(*id, params[jss::seq].asUInt()).key;
}

std::optional<uint256>
parseOffer(Json::Value const& params, Json::Value& jvResult)
{
    if (!params.isObject())
    {
        uint256 uNodeIndex;
        if (!uNodeIndex.parseHex(params.asString()))
        {
            jvResult[jss::error] = "malformedRequest";
            return std::nullopt;
        }
        return uNodeIndex;
    }

    if (!params.isMember(jss::account) || !params.isMember(jss::seq) ||
        !params[jss::seq].isIntegral())
    {
        jvResult[jss::error] = "malformedRequest";
        return std::nullopt;
    }

    auto const id = parseBase58<AccountID>(params[jss::account].asString());
    if (!id)
    {
        jvResult[jss::error] = "malformedAddress";
        return std::nullopt;
    }

    return keylet::offer(*id, params[jss::seq].asUInt()).key;
}

std::optional<uint256>
parsePaymentChannel(Json::Value const& params, Json::Value& jvResult)
{
    uint256 uNodeIndex;
    if (!uNodeIndex.parseHex(params.asString()))
    {
        jvResult[jss::error] = "malformedRequest";
        return std::nullopt;
    }

    return uNodeIndex;
}

std::optional<uint256>
parseRippleState(Json::Value const& jvRippleState, Json::Value& jvResult)
{
    Currency uCurrency;

    if (!jvRippleState.isObject() || !jvRippleState.isMember(jss::currency) ||
        !jvRippleState.isMember(jss::accounts) ||
        !jvRippleState[jss::accounts].isArray() ||
        2 != jvRippleState[jss::accounts].size() ||
        !jvRippleState[jss::accounts][0u].isString() ||
        !jvRippleState[jss::accounts][1u].isString() ||
        (jvRippleState[jss::accounts][0u].asString() ==
         jvRippleState[jss::accounts][1u].asString()))
    {
        jvResult[jss::error] = "malformedRequest";
        return std::nullopt;
    }

    auto const id1 =
        parseBase58<AccountID>(jvRippleState[jss::accounts][0u].asString());
    auto const id2 =
        parseBase58<AccountID>(jvRippleState[jss::accounts][1u].asString());
    if (!id1 || !id2)
    {
        jvResult[jss::error] = "malformedAddress";
        return std::nullopt;
    }

    if (!to_currency(uCurrency, jvRippleState[jss::currency].asString()))
    {
        jvResult[jss::error] = "malformedCurrency";
        return std::nullopt;
    }

    return keylet::line(*id1, *id2, uCurrency).key;
}

std::optional<uint256>
parseTicket(Json::Value const& params, Json::Value& jvResult)
{
    if (!params.isObject())
    {
        uint256 uNodeIndex;
        if (!uNodeIndex.parseHex(params.asString()))
        {
            jvResult[jss::error] = "malformedRequest";
            return std::nullopt;
        }
        return uNodeIndex;
    }

    if (!params.isMember(jss::account) || !params.isMember(jss::ticket_seq) ||
        !params[jss::ticket_seq].isIntegral())
    {
        jvResult[jss::error] = "malformedRequest";
        return std::nullopt;
    }

    auto const id = parseBase58<AccountID>(params[jss::account].asString());
    if (!id)
    {
        jvResult[jss::error] = "malformedAddress";
        return std::nullopt;
    }

    return getTicketIndex(*id, params[jss::ticket_seq].asUInt());
}

std::optional<uint256>
parseNFTokenPage(Json::Value const& params, Json::Value& jvResult)
{
    if (params.isString())
    {
        uint256 uNodeIndex;
        if (!uNodeIndex.parseHex(params.asString()))
        {
            jvResult[jss::error] = "malformedRequest";
            return std::nullopt;
        }
        return uNodeIndex;
    }

    jvResult[jss::error] = "malformedRequest";
    return std::nullopt;
}

std::optional<uint256>
parseAMM(Json::Value const& params, Json::Value& jvResult)
{
    if (!params.isObject())
    {
        uint256 uNodeIndex;
        if (!uNodeIndex.parseHex(params.asString()))
        {
            jvResult[jss::error] = "malformedRequest";
            return std::nullopt;
        }
        return uNodeIndex;
    }

    if (!params.isMember(jss::asset) || !params.isMember(jss::asset2))
    {
        jvResult[jss::error] = "malformedRequest";
        return std::nullopt;
    }

    try
    {
        auto const issue = issueFromJson(params[jss::asset]);
        auto const issue2 = issueFromJson(params[jss::asset2]);
        return keylet::amm(issue, issue2).key;
    }
    catch (std::runtime_error const&)
    {
        jvResult[jss::error] = "malformedRequest";
        return std::nullopt;
    }
}

std::optional<uint256>
parseBridge(Json::Value const& params, Json::Value& jvResult)
{
    // return the keylet for the specified bridge or nullopt if the
    // request is malformed
    auto const maybeKeylet = [&]() -> std::optional<Keylet> {
        try
        {
            if (!params.isMember(jss::bridge_account))
                return std::nullopt;

            auto const& jsBridgeAccount = params[jss::bridge_account];
            if (!jsBridgeAccount.isString())
            {
                return std::nullopt;
            }

            auto const account =
                parseBase58<AccountID>(jsBridgeAccount.asString());
            if (!account || account->isZero())
            {
                return std::nullopt;
            }

            // This may throw and is the reason for the `try` block. The
            // try block has a larger scope so the `bridge` variable
            // doesn't need to be an optional.
            STXChainBridge const bridge(params[jss::bridge]);
            STXChainBridge::ChainType const chainType =
                STXChainBridge::srcChain(account == bridge.lockingChainDoor());

            if (account != bridge.door(chainType))
                return std::nullopt;

            return keylet::bridge(bridge, chainType);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }();

    if (maybeKeylet)
    {
        return maybeKeylet->key;
    }

    jvResult[jss::error] = "malformedRequest";
    return std::nullopt;
}

std::optional<uint256>
parseXChainOwnedClaimID(Json::Value const& claim_id, Json::Value& jvResult)
{
    if (claim_id.isString())
    {
        uint256 uNodeIndex;
        // we accept a node id as specifier of a xchain claim id
        if (!uNodeIndex.parseHex(claim_id.asString()))
        {
            jvResult[jss::error] = "malformedRequest";
            return std::nullopt;
        }
        return uNodeIndex;
    }

    if (!claim_id.isObject() ||
        !(claim_id.isMember(sfIssuingChainDoor.getJsonName()) &&
          claim_id[sfIssuingChainDoor.getJsonName()].isString()) ||
        !(claim_id.isMember(sfLockingChainDoor.getJsonName()) &&
          claim_id[sfLockingChainDoor.getJsonName()].isString()) ||
        !claim_id.isMember(sfIssuingChainIssue.getJsonName()) ||
        !claim_id.isMember(sfLockingChainIssue.getJsonName()) ||
        !claim_id.isMember(jss::xchain_owned_claim_id))
    {
        jvResult[jss::error] = "malformedRequest";
        return std::nullopt;
    }

    // if not specified with a node id, a claim_id is specified by
    // four strings defining the bridge (locking_chain_door,
    // locking_chain_issue, issuing_chain_door, issuing_chain_issue)
    // and the claim id sequence number.
    auto const lockingChainDoor = parseBase58<AccountID>(
        claim_id[sfLockingChainDoor.getJsonName()].asString());
    auto const issuingChainDoor = parseBase58<AccountID>(
        claim_id[sfIssuingChainDoor.getJsonName()].asString());
    Issue lockingChainIssue, issuingChainIssue;
    bool valid = lockingChainDoor && issuingChainDoor;

    if (valid)
    {
        try
        {
            lockingChainIssue =
                issueFromJson(claim_id[sfLockingChainIssue.getJsonName()]);
            issuingChainIssue =
                issueFromJson(claim_id[sfIssuingChainIssue.getJsonName()]);
        }
        catch (std::runtime_error const& ex)
        {
            jvResult[jss::error] = "malformedRequest";
            return std::nullopt;
        }
    }

    if (valid && claim_id[jss::xchain_owned_claim_id].isIntegral())
    {
        auto const seq = claim_id[jss::xchain_owned_claim_id].asUInt();

        STXChainBridge bridge_spec(
            *lockingChainDoor,
            lockingChainIssue,
            *issuingChainDoor,
            issuingChainIssue);
        Keylet keylet = keylet::xChainClaimID(bridge_spec, seq);
        return keylet.key;
    }

    jvResult[jss::error] = "malformedRequest";
    return std::nullopt;
}

std::optional<uint256>
parseXChainOwnedCreateAccountClaimID(
    Json::Value const& claim_id,
    Json::Value& jvResult)
{
    if (claim_id.isString())
    {
        uint256 uNodeIndex;
        // we accept a node id as specifier of a xchain create account
        // claim_id
        if (!uNodeIndex.parseHex(claim_id.asString()))
        {
            jvResult[jss::error] = "malformedRequest";
            return std::nullopt;
        }
        return uNodeIndex;
    }

    if (!claim_id.isObject() ||
        !(claim_id.isMember(sfIssuingChainDoor.getJsonName()) &&
          claim_id[sfIssuingChainDoor.getJsonName()].isString()) ||
        !(claim_id.isMember(sfLockingChainDoor.getJsonName()) &&
          claim_id[sfLockingChainDoor.getJsonName()].isString()) ||
        !claim_id.isMember(sfIssuingChainIssue.getJsonName()) ||
        !claim_id.isMember(sfLockingChainIssue.getJsonName()) ||
        !claim_id.isMember(jss::xchain_owned_create_account_claim_id))
    {
        jvResult[jss::error] = "malformedRequest";
        return std::nullopt;
    }

    // if not specified with a node id, a create account claim_id is
    // specified by four strings defining the bridge
    // (locking_chain_door, locking_chain_issue, issuing_chain_door,
    // issuing_chain_issue) and the create account claim id sequence
    // number.
    auto const lockingChainDoor = parseBase58<AccountID>(
        claim_id[sfLockingChainDoor.getJsonName()].asString());
    auto const issuingChainDoor = parseBase58<AccountID>(
        claim_id[sfIssuingChainDoor.getJsonName()].asString());
    Issue lockingChainIssue, issuingChainIssue;
    bool valid = lockingChainDoor && issuingChainDoor;
    if (valid)
    {
        try
        {
            lockingChainIssue =
                issueFromJson(claim_id[sfLockingChainIssue.getJsonName()]);
            issuingChainIssue =
                issueFromJson(claim_id[sfIssuingChainIssue.getJsonName()]);
        }
        catch (std::runtime_error const& ex)
        {
            valid = false;
            jvResult[jss::error] = "malformedRequest";
        }
    }

    if (valid &&
        claim_id[jss::xchain_owned_create_account_claim_id].isIntegral())
    {
        auto const seq =
            claim_id[jss::xchain_owned_create_account_claim_id].asUInt();

        STXChainBridge bridge_spec(
            *lockingChainDoor,
            lockingChainIssue,
            *issuingChainDoor,
            issuingChainIssue);
        Keylet keylet = keylet::xChainCreateAccountClaimID(bridge_spec, seq);
        return keylet.key;
    }

    return std::nullopt;
}

std::optional<uint256>
parseDID(Json::Value const& params, Json::Value& jvResult)
{
    auto const account = parseBase58<AccountID>(params.asString());
    if (!account || account->isZero())
    {
        jvResult[jss::error] = "malformedAddress";
        return std::nullopt;
    }

    return keylet::did(*account).key;
}

std::optional<uint256>
parseOracle(Json::Value const& params, Json::Value& jvResult)
{
    if (!params.isObject())
    {
        uint256 uNodeIndex;
        if (!uNodeIndex.parseHex(params.asString()))
        {
            jvResult[jss::error] = "malformedRequest";
            return std::nullopt;
        }
        return uNodeIndex;
    }

    if (!params.isMember(jss::oracle_document_id) ||
        !params.isMember(jss::account))
    {
        jvResult[jss::error] = "malformedRequest";
        return std::nullopt;
    }

    auto const& oracle = params;
    auto const documentID = [&]() -> std::optional<std::uint32_t> {
        auto const id = oracle[jss::oracle_document_id];
        if (id.isUInt() || (id.isInt() && id.asInt() >= 0))
            return std::make_optional(id.asUInt());

        if (id.isString())
        {
            std::uint32_t v;
            if (beast::lexicalCastChecked(v, id.asString()))
                return std::make_optional(v);
        }

        return std::nullopt;
    }();

    auto const account =
        parseBase58<AccountID>(oracle[jss::account].asString());
    if (!account || account->isZero())
    {
        jvResult[jss::error] = "malformedAddress";
        return std::nullopt;
    }

    if (!documentID)
    {
        jvResult[jss::error] = "malformedDocumentID";
        return std::nullopt;
    }

    return keylet::oracle(*account, *documentID).key;
}

std::optional<uint256>
parseCredential(Json::Value const& cred, Json::Value& jvResult)
{
    if (cred.isString())
    {
        uint256 uNodeIndex;
        if (!uNodeIndex.parseHex(cred.asString()))
        {
            jvResult[jss::error] = "malformedRequest";
            return std::nullopt;
        }
        return uNodeIndex;
    }

    if ((!cred.isMember(jss::subject) || !cred[jss::subject].isString()) ||
        (!cred.isMember(jss::issuer) || !cred[jss::issuer].isString()) ||
        (!cred.isMember(jss::credential_type) ||
         !cred[jss::credential_type].isString()))
    {
        jvResult[jss::error] = "malformedRequest";
        return std::nullopt;
    }

    auto const subject = parseBase58<AccountID>(cred[jss::subject].asString());
    auto const issuer = parseBase58<AccountID>(cred[jss::issuer].asString());
    auto const credType = strUnHex(cred[jss::credential_type].asString());

    if (!subject || subject->isZero() || !issuer || issuer->isZero() ||
        !credType || credType->empty())
    {
        jvResult[jss::error] = "malformedRequest";
        return std::nullopt;
    }

    return keylet::credential(
               *subject, *issuer, Slice(credType->data(), credType->size()))
        .key;
}

std::optional<uint256>
parseMPTokenIssuance(
    Json::Value const& unparsedMPTIssuanceID,
    Json::Value& jvResult)
{
    if (unparsedMPTIssuanceID.isString())
    {
        uint192 mptIssuanceID;
        if (!mptIssuanceID.parseHex(unparsedMPTIssuanceID.asString()))
        {
            jvResult[jss::error] = "malformedRequest";
            return std::nullopt;
        }

        return keylet::mptIssuance(mptIssuanceID).key;
    }

    jvResult[jss::error] = "malformedRequest";
    return std::nullopt;
}

std::optional<uint256>
parseMPToken(Json::Value const& mptJson, Json::Value& jvResult)
{
    if (!mptJson.isObject())
    {
        uint256 uNodeIndex;
        if (!uNodeIndex.parseHex(mptJson.asString()))
        {
            jvResult[jss::error] = "malformedRequest";
            return std::nullopt;
        }
        return uNodeIndex;
    }

    if (!mptJson.isMember(jss::mpt_issuance_id) ||
        !mptJson.isMember(jss::account))
    {
        jvResult[jss::error] = "malformedRequest";
        return std::nullopt;
    }

    try
    {
        auto const mptIssuanceIdStr = mptJson[jss::mpt_issuance_id].asString();

        uint192 mptIssuanceID;
        if (!mptIssuanceID.parseHex(mptIssuanceIdStr))
            Throw<std::runtime_error>("Cannot parse mpt_issuance_id");

        auto const account =
            parseBase58<AccountID>(mptJson[jss::account].asString());

        if (!account || account->isZero())
        {
            jvResult[jss::error] = "malformedAddress";
            return std::nullopt;
        }

        return keylet::mptoken(mptIssuanceID, *account).key;
    }
    catch (std::runtime_error const&)
    {
        jvResult[jss::error] = "malformedRequest";
        return std::nullopt;
    }
}

using FunctionType =
    std::optional<uint256> (*)(Json::Value const&, Json::Value&);

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
    std::shared_ptr<ReadView const> lpLedger;
    auto jvResult = RPC::lookupLedger(lpLedger, context);

    if (!lpLedger)
        return jvResult;

    static auto ledgerEntryParsers = std::to_array<LedgerEntry>({
        {jss::index, parseIndex, ltANY},
        {jss::account_root, parseAccountRoot, ltACCOUNT_ROOT},
        // TODO: add amendments
        {jss::amm, parseAMM, ltAMM},
        {jss::bridge, parseBridge, ltBRIDGE},
        {jss::check, parseCheck, ltCHECK},
        {jss::credential, parseCredential, ltCREDENTIAL},
        {jss::deposit_preauth, parseDepositPreauth, ltDEPOSIT_PREAUTH},
        {jss::did, parseDID, ltDID},
        {jss::directory, parseDirectory, ltDIR_NODE},
        {jss::escrow, parseEscrow, ltESCROW},
        // TODO: add fee, hashes
        {jss::mpt_issuance, parseMPTokenIssuance, ltMPTOKEN_ISSUANCE},
        {jss::mptoken, parseMPToken, ltMPTOKEN},
        // TODO: add NFT Offers
        {jss::nft_page, parseNFTokenPage, ltNFTOKEN_PAGE},
        // TODO: add NegativeUNL
        {jss::offer, parseOffer, ltOFFER},
        {jss::oracle, parseOracle, ltORACLE},
        {jss::payment_channel, parsePaymentChannel, ltPAYCHAN},
        {jss::ripple_state, parseRippleState, ltRIPPLE_STATE},
        // This is an alias, since the `ledger_data` filter uses jss::state
        {jss::state, parseRippleState, ltRIPPLE_STATE},
        {jss::ticket, parseTicket, ltTICKET},
        {jss::xchain_owned_claim_id,
         parseXChainOwnedClaimID,
         ltXCHAIN_OWNED_CLAIM_ID},
        {jss::xchain_owned_create_account_claim_id,
         parseXChainOwnedCreateAccountClaimID,
         ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID},
    });

    uint256 uNodeIndex;
    LedgerEntryType expectedType = ltANY;

    try
    {
        bool found = false;
        for (const auto& ledgerEntry : ledgerEntryParsers)
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
                uNodeIndex = ledgerEntry.parseFunction(params, jvResult)
                                 .value_or(beast::zero);
                if (jvResult.isMember(jss::error))
                {
                    return jvResult;
                }
                found = true;
                break;
            }
        }
        if (!found)
        {
            if (context.apiVersion < 2u)
                jvResult[jss::error] = "unknownOption";
            else
                jvResult[jss::error] = "invalidParams";
            return jvResult;
        }
    }
    catch (Json::error& e)
    {
        if (context.apiVersion > 1u)
        {
            // For apiVersion 2 onwards, any parsing failures that throw this
            // exception return an invalidParam error.
            jvResult[jss::error] = "invalidParams";
            return jvResult;
        }
        else
            throw;
    }

    if (uNodeIndex.isZero())
    {
        jvResult[jss::error] = "entryNotFound";
        return jvResult;
    }

    auto const sleNode = lpLedger->read(keylet::unchecked(uNodeIndex));

    bool bNodeBinary = false;
    if (context.params.isMember(jss::binary))
        bNodeBinary = context.params[jss::binary].asBool();

    if (!sleNode)
    {
        // Not found.
        jvResult[jss::error] = "entryNotFound";
        return jvResult;
    }

    if ((expectedType != ltANY) && (expectedType != sleNode->getType()))
    {
        jvResult[jss::error] = "unexpectedLedgerType";
        return jvResult;
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
    else
    {
        Serializer s;
        sleNode->add(s);

        auto& stateObject = *response.mutable_ledger_object();
        stateObject.set_data(s.peekData().data(), s.getLength());
        stateObject.set_key(request.key());
        *(response.mutable_ledger()) = request.ledger();
        return {response, status};
    }
}
}  // namespace ripple
