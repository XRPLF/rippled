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

    uint256 uNodeIndex;
    bool bNodeBinary = false;
    LedgerEntryType expectedType = ltANY;

    try
    {
        if (context.params.isMember(jss::index))
        {
            if (!uNodeIndex.parseHex(context.params[jss::index].asString()))
            {
                uNodeIndex = beast::zero;
                jvResult[jss::error] = "malformedRequest";
            }
        }
        else if (context.params.isMember(jss::account_root))
        {
            expectedType = ltACCOUNT_ROOT;
            auto const account = parseBase58<AccountID>(
                context.params[jss::account_root].asString());
            if (!account || account->isZero())
                jvResult[jss::error] = "malformedAddress";
            else
                uNodeIndex = keylet::account(*account).key;
        }
        else if (context.params.isMember(jss::check))
        {
            expectedType = ltCHECK;
            if (!uNodeIndex.parseHex(context.params[jss::check].asString()))
            {
                uNodeIndex = beast::zero;
                jvResult[jss::error] = "malformedRequest";
            }
        }
        else if (context.params.isMember(jss::deposit_preauth))
        {
            expectedType = ltDEPOSIT_PREAUTH;
            auto const& dp = context.params[jss::deposit_preauth];

            if (!dp.isObject())
            {
                if (!dp.isString() || !uNodeIndex.parseHex(dp.asString()))
                {
                    uNodeIndex = beast::zero;
                    jvResult[jss::error] = "malformedRequest";
                }
            }
            // clang-format off
            else if (
                (!dp.isMember(jss::owner) || !dp[jss::owner].isString()) ||
                (dp.isMember(jss::authorized) == dp.isMember(jss::authorized_credentials)) ||
                (dp.isMember(jss::authorized) && !dp[jss::authorized].isString()) ||
                (dp.isMember(jss::authorized_credentials) && !dp[jss::authorized_credentials].isArray())
                )
            // clang-format on
            {
                jvResult[jss::error] = "malformedRequest";
            }
            else
            {
                auto const owner =
                    parseBase58<AccountID>(dp[jss::owner].asString());
                if (!owner)
                {
                    jvResult[jss::error] = "malformedOwner";
                }
                else if (dp.isMember(jss::authorized))
                {
                    auto const authorized =
                        parseBase58<AccountID>(dp[jss::authorized].asString());
                    if (!authorized)
                        jvResult[jss::error] = "malformedAuthorized";
                    else
                        uNodeIndex =
                            keylet::depositPreauth(*owner, *authorized).key;
                }
                else
                {
                    auto const& ac(dp[jss::authorized_credentials]);
                    STArray const arr = parseAuthorizeCredentials(ac);

                    if (arr.empty() || (arr.size() > maxCredentialsArraySize))
                        jvResult[jss::error] = "malformedAuthorizedCredentials";
                    else
                    {
                        auto sorted = credentials::makeSorted(arr);
                        if (sorted.empty())
                            jvResult[jss::error] =
                                "malformedAuthorizedCredentials";
                        else
                            uNodeIndex =
                                keylet::depositPreauth(*owner, sorted).key;
                    }
                }
            }
        }
        else if (context.params.isMember(jss::directory))
        {
            expectedType = ltDIR_NODE;
            if (context.params[jss::directory].isNull())
            {
                jvResult[jss::error] = "malformedRequest";
            }
            else if (!context.params[jss::directory].isObject())
            {
                if (!uNodeIndex.parseHex(
                        context.params[jss::directory].asString()))
                {
                    uNodeIndex = beast::zero;
                    jvResult[jss::error] = "malformedRequest";
                }
            }
            else if (
                context.params[jss::directory].isMember(jss::sub_index) &&
                !context.params[jss::directory][jss::sub_index].isIntegral())
            {
                jvResult[jss::error] = "malformedRequest";
            }
            else
            {
                std::uint64_t uSubIndex =
                    context.params[jss::directory].isMember(jss::sub_index)
                    ? context.params[jss::directory][jss::sub_index].asUInt()
                    : 0;

                if (context.params[jss::directory].isMember(jss::dir_root))
                {
                    uint256 uDirRoot;

                    if (context.params[jss::directory].isMember(jss::owner))
                    {
                        // May not specify both dir_root and owner.
                        jvResult[jss::error] = "malformedRequest";
                    }
                    else if (!uDirRoot.parseHex(
                                 context.params[jss::directory][jss::dir_root]
                                     .asString()))
                    {
                        uNodeIndex = beast::zero;
                        jvResult[jss::error] = "malformedRequest";
                    }
                    else
                    {
                        uNodeIndex = keylet::page(uDirRoot, uSubIndex).key;
                    }
                }
                else if (context.params[jss::directory].isMember(jss::owner))
                {
                    auto const ownerID = parseBase58<AccountID>(
                        context.params[jss::directory][jss::owner].asString());

                    if (!ownerID)
                    {
                        jvResult[jss::error] = "malformedAddress";
                    }
                    else
                    {
                        uNodeIndex =
                            keylet::page(keylet::ownerDir(*ownerID), uSubIndex)
                                .key;
                    }
                }
                else
                {
                    jvResult[jss::error] = "malformedRequest";
                }
            }
        }
        else if (context.params.isMember(jss::escrow))
        {
            expectedType = ltESCROW;
            if (!context.params[jss::escrow].isObject())
            {
                if (!uNodeIndex.parseHex(
                        context.params[jss::escrow].asString()))
                {
                    uNodeIndex = beast::zero;
                    jvResult[jss::error] = "malformedRequest";
                }
            }
            else if (
                !context.params[jss::escrow].isMember(jss::owner) ||
                !context.params[jss::escrow].isMember(jss::seq) ||
                !context.params[jss::escrow][jss::seq].isIntegral())
            {
                jvResult[jss::error] = "malformedRequest";
            }
            else
            {
                auto const id = parseBase58<AccountID>(
                    context.params[jss::escrow][jss::owner].asString());
                if (!id)
                    jvResult[jss::error] = "malformedOwner";
                else
                    uNodeIndex =
                        keylet::escrow(
                            *id, context.params[jss::escrow][jss::seq].asUInt())
                            .key;
            }
        }
        else if (context.params.isMember(jss::offer))
        {
            expectedType = ltOFFER;
            if (!context.params[jss::offer].isObject())
            {
                if (!uNodeIndex.parseHex(context.params[jss::offer].asString()))
                {
                    uNodeIndex = beast::zero;
                    jvResult[jss::error] = "malformedRequest";
                }
            }
            else if (
                !context.params[jss::offer].isMember(jss::account) ||
                !context.params[jss::offer].isMember(jss::seq) ||
                !context.params[jss::offer][jss::seq].isIntegral())
            {
                jvResult[jss::error] = "malformedRequest";
            }
            else
            {
                auto const id = parseBase58<AccountID>(
                    context.params[jss::offer][jss::account].asString());
                if (!id)
                    jvResult[jss::error] = "malformedAddress";
                else
                    uNodeIndex =
                        keylet::offer(
                            *id, context.params[jss::offer][jss::seq].asUInt())
                            .key;
            }
        }
        else if (context.params.isMember(jss::payment_channel))
        {
            expectedType = ltPAYCHAN;

            if (!uNodeIndex.parseHex(
                    context.params[jss::payment_channel].asString()))
            {
                uNodeIndex = beast::zero;
                jvResult[jss::error] = "malformedRequest";
            }
        }
        else if (context.params.isMember(jss::ripple_state))
        {
            expectedType = ltRIPPLE_STATE;
            Currency uCurrency;
            Json::Value jvRippleState = context.params[jss::ripple_state];

            if (!jvRippleState.isObject() ||
                !jvRippleState.isMember(jss::currency) ||
                !jvRippleState.isMember(jss::accounts) ||
                !jvRippleState[jss::accounts].isArray() ||
                2 != jvRippleState[jss::accounts].size() ||
                !jvRippleState[jss::accounts][0u].isString() ||
                !jvRippleState[jss::accounts][1u].isString() ||
                (jvRippleState[jss::accounts][0u].asString() ==
                 jvRippleState[jss::accounts][1u].asString()))
            {
                jvResult[jss::error] = "malformedRequest";
            }
            else
            {
                auto const id1 = parseBase58<AccountID>(
                    jvRippleState[jss::accounts][0u].asString());
                auto const id2 = parseBase58<AccountID>(
                    jvRippleState[jss::accounts][1u].asString());
                if (!id1 || !id2)
                {
                    jvResult[jss::error] = "malformedAddress";
                }
                else if (!to_currency(
                             uCurrency,
                             jvRippleState[jss::currency].asString()))
                {
                    jvResult[jss::error] = "malformedCurrency";
                }
                else
                {
                    uNodeIndex = keylet::line(*id1, *id2, uCurrency).key;
                }
            }
        }
        else if (context.params.isMember(jss::ticket))
        {
            expectedType = ltTICKET;
            if (!context.params[jss::ticket].isObject())
            {
                if (!uNodeIndex.parseHex(
                        context.params[jss::ticket].asString()))
                {
                    uNodeIndex = beast::zero;
                    jvResult[jss::error] = "malformedRequest";
                }
            }
            else if (
                !context.params[jss::ticket].isMember(jss::account) ||
                !context.params[jss::ticket].isMember(jss::ticket_seq) ||
                !context.params[jss::ticket][jss::ticket_seq].isIntegral())
            {
                jvResult[jss::error] = "malformedRequest";
            }
            else
            {
                auto const id = parseBase58<AccountID>(
                    context.params[jss::ticket][jss::account].asString());
                if (!id)
                    jvResult[jss::error] = "malformedAddress";
                else
                    uNodeIndex = getTicketIndex(
                        *id,
                        context.params[jss::ticket][jss::ticket_seq].asUInt());
            }
        }
        else if (context.params.isMember(jss::nft_page))
        {
            expectedType = ltNFTOKEN_PAGE;

            if (context.params[jss::nft_page].isString())
            {
                if (!uNodeIndex.parseHex(
                        context.params[jss::nft_page].asString()))
                {
                    uNodeIndex = beast::zero;
                    jvResult[jss::error] = "malformedRequest";
                }
            }
            else
            {
                jvResult[jss::error] = "malformedRequest";
            }
        }
        else if (context.params.isMember(jss::amm))
        {
            expectedType = ltAMM;
            if (!context.params[jss::amm].isObject())
            {
                if (!uNodeIndex.parseHex(context.params[jss::amm].asString()))
                {
                    uNodeIndex = beast::zero;
                    jvResult[jss::error] = "malformedRequest";
                }
            }
            else if (
                !context.params[jss::amm].isMember(jss::asset) ||
                !context.params[jss::amm].isMember(jss::asset2))
            {
                jvResult[jss::error] = "malformedRequest";
            }
            else
            {
                try
                {
                    auto const issue =
                        issueFromJson(context.params[jss::amm][jss::asset]);
                    auto const issue2 =
                        issueFromJson(context.params[jss::amm][jss::asset2]);
                    uNodeIndex = keylet::amm(issue, issue2).key;
                }
                catch (std::runtime_error const&)
                {
                    jvResult[jss::error] = "malformedRequest";
                }
            }
        }
        else if (context.params.isMember(jss::bridge))
        {
            expectedType = ltBRIDGE;

            // return the keylet for the specified bridge or nullopt if the
            // request is malformed
            auto const maybeKeylet = [&]() -> std::optional<Keylet> {
                try
                {
                    if (!context.params.isMember(jss::bridge_account))
                        return std::nullopt;

                    auto const& jsBridgeAccount =
                        context.params[jss::bridge_account];
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
                    STXChainBridge const bridge(context.params[jss::bridge]);
                    STXChainBridge::ChainType const chainType =
                        STXChainBridge::srcChain(
                            account == bridge.lockingChainDoor());
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
                uNodeIndex = maybeKeylet->key;
            }
            else
            {
                uNodeIndex = beast::zero;
                jvResult[jss::error] = "malformedRequest";
            }
        }
        else if (context.params.isMember(jss::xchain_owned_claim_id))
        {
            expectedType = ltXCHAIN_OWNED_CLAIM_ID;
            auto& claim_id = context.params[jss::xchain_owned_claim_id];
            if (claim_id.isString())
            {
                // we accept a node id as specifier of a xchain claim id
                if (!uNodeIndex.parseHex(claim_id.asString()))
                {
                    uNodeIndex = beast::zero;
                    jvResult[jss::error] = "malformedRequest";
                }
            }
            else if (
                !claim_id.isObject() ||
                !(claim_id.isMember(sfIssuingChainDoor.getJsonName()) &&
                  claim_id[sfIssuingChainDoor.getJsonName()].isString()) ||
                !(claim_id.isMember(sfLockingChainDoor.getJsonName()) &&
                  claim_id[sfLockingChainDoor.getJsonName()].isString()) ||
                !claim_id.isMember(sfIssuingChainIssue.getJsonName()) ||
                !claim_id.isMember(sfLockingChainIssue.getJsonName()) ||
                !claim_id.isMember(jss::xchain_owned_claim_id))
            {
                jvResult[jss::error] = "malformedRequest";
            }
            else
            {
                // if not specified with a node id, a claim_id is specified by
                // four strings defining the bridge (locking_chain_door,
                // locking_chain_issue, issuing_chain_door, issuing_chain_issue)
                // and the claim id sequence number.
                auto lockingChainDoor = parseBase58<AccountID>(
                    claim_id[sfLockingChainDoor.getJsonName()].asString());
                auto issuingChainDoor = parseBase58<AccountID>(
                    claim_id[sfIssuingChainDoor.getJsonName()].asString());
                Issue lockingChainIssue, issuingChainIssue;
                bool valid = lockingChainDoor && issuingChainDoor;
                if (valid)
                {
                    try
                    {
                        lockingChainIssue = issueFromJson(
                            claim_id[sfLockingChainIssue.getJsonName()]);
                        issuingChainIssue = issueFromJson(
                            claim_id[sfIssuingChainIssue.getJsonName()]);
                    }
                    catch (std::runtime_error const& ex)
                    {
                        valid = false;
                        jvResult[jss::error] = "malformedRequest";
                    }
                }

                if (valid && claim_id[jss::xchain_owned_claim_id].isIntegral())
                {
                    auto seq = claim_id[jss::xchain_owned_claim_id].asUInt();

                    STXChainBridge bridge_spec(
                        *lockingChainDoor,
                        lockingChainIssue,
                        *issuingChainDoor,
                        issuingChainIssue);
                    Keylet keylet = keylet::xChainClaimID(bridge_spec, seq);
                    uNodeIndex = keylet.key;
                }
            }
        }
        else if (context.params.isMember(
                     jss::xchain_owned_create_account_claim_id))
        {
            // see object definition in LedgerFormats.cpp
            expectedType = ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID;
            auto& claim_id =
                context.params[jss::xchain_owned_create_account_claim_id];
            if (claim_id.isString())
            {
                // we accept a node id as specifier of a xchain create account
                // claim_id
                if (!uNodeIndex.parseHex(claim_id.asString()))
                {
                    uNodeIndex = beast::zero;
                    jvResult[jss::error] = "malformedRequest";
                }
            }
            else if (
                !claim_id.isObject() ||
                !(claim_id.isMember(sfIssuingChainDoor.getJsonName()) &&
                  claim_id[sfIssuingChainDoor.getJsonName()].isString()) ||
                !(claim_id.isMember(sfLockingChainDoor.getJsonName()) &&
                  claim_id[sfLockingChainDoor.getJsonName()].isString()) ||
                !claim_id.isMember(sfIssuingChainIssue.getJsonName()) ||
                !claim_id.isMember(sfLockingChainIssue.getJsonName()) ||
                !claim_id.isMember(jss::xchain_owned_create_account_claim_id))
            {
                jvResult[jss::error] = "malformedRequest";
            }
            else
            {
                // if not specified with a node id, a create account claim_id is
                // specified by four strings defining the bridge
                // (locking_chain_door, locking_chain_issue, issuing_chain_door,
                // issuing_chain_issue) and the create account claim id sequence
                // number.
                auto lockingChainDoor = parseBase58<AccountID>(
                    claim_id[sfLockingChainDoor.getJsonName()].asString());
                auto issuingChainDoor = parseBase58<AccountID>(
                    claim_id[sfIssuingChainDoor.getJsonName()].asString());
                Issue lockingChainIssue, issuingChainIssue;
                bool valid = lockingChainDoor && issuingChainDoor;
                if (valid)
                {
                    try
                    {
                        lockingChainIssue = issueFromJson(
                            claim_id[sfLockingChainIssue.getJsonName()]);
                        issuingChainIssue = issueFromJson(
                            claim_id[sfIssuingChainIssue.getJsonName()]);
                    }
                    catch (std::runtime_error const& ex)
                    {
                        valid = false;
                        jvResult[jss::error] = "malformedRequest";
                    }
                }

                if (valid &&
                    claim_id[jss::xchain_owned_create_account_claim_id]
                        .isIntegral())
                {
                    auto seq =
                        claim_id[jss::xchain_owned_create_account_claim_id]
                            .asUInt();

                    STXChainBridge bridge_spec(
                        *lockingChainDoor,
                        lockingChainIssue,
                        *issuingChainDoor,
                        issuingChainIssue);
                    Keylet keylet =
                        keylet::xChainCreateAccountClaimID(bridge_spec, seq);
                    uNodeIndex = keylet.key;
                }
            }
        }
        else if (context.params.isMember(jss::did))
        {
            expectedType = ltDID;
            auto const account =
                parseBase58<AccountID>(context.params[jss::did].asString());
            if (!account || account->isZero())
                jvResult[jss::error] = "malformedAddress";
            else
                uNodeIndex = keylet::did(*account).key;
        }
        else if (context.params.isMember(jss::oracle))
        {
            expectedType = ltORACLE;
            if (!context.params[jss::oracle].isObject())
            {
                if (!uNodeIndex.parseHex(
                        context.params[jss::oracle].asString()))
                {
                    uNodeIndex = beast::zero;
                    jvResult[jss::error] = "malformedRequest";
                }
            }
            else if (
                !context.params[jss::oracle].isMember(
                    jss::oracle_document_id) ||
                !context.params[jss::oracle].isMember(jss::account))
            {
                jvResult[jss::error] = "malformedRequest";
            }
            else
            {
                uNodeIndex = beast::zero;
                auto const& oracle = context.params[jss::oracle];
                auto const documentID = [&]() -> std::optional<std::uint32_t> {
                    auto const& id = oracle[jss::oracle_document_id];
                    if (id.isUInt() || (id.isInt() && id.asInt() >= 0))
                        return std::make_optional(id.asUInt());
                    else if (id.isString())
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
                    jvResult[jss::error] = "malformedAddress";
                else if (!documentID)
                    jvResult[jss::error] = "malformedDocumentID";
                else
                    uNodeIndex = keylet::oracle(*account, *documentID).key;
            }
        }
        else if (context.params.isMember(jss::credential))
        {
            expectedType = ltCREDENTIAL;
            auto const& cred = context.params[jss::credential];

            if (cred.isString())
            {
                if (!uNodeIndex.parseHex(cred.asString()))
                {
                    uNodeIndex = beast::zero;
                    jvResult[jss::error] = "malformedRequest";
                }
            }
            else if (
                (!cred.isMember(jss::subject) ||
                 !cred[jss::subject].isString()) ||
                (!cred.isMember(jss::issuer) ||
                 !cred[jss::issuer].isString()) ||
                (!cred.isMember(jss::credential_type) ||
                 !cred[jss::credential_type].isString()))
            {
                jvResult[jss::error] = "malformedRequest";
            }
            else
            {
                auto const subject =
                    parseBase58<AccountID>(cred[jss::subject].asString());
                auto const issuer =
                    parseBase58<AccountID>(cred[jss::issuer].asString());
                auto const credType =
                    strUnHex(cred[jss::credential_type].asString());
                if (!subject || subject->isZero() || !issuer ||
                    issuer->isZero() || !credType || credType->empty())
                {
                    jvResult[jss::error] = "malformedRequest";
                }
                else
                {
                    uNodeIndex = keylet::credential(
                                     *subject,
                                     *issuer,
                                     Slice(credType->data(), credType->size()))
                                     .key;
                }
            }
        }
        else if (context.params.isMember(jss::mpt_issuance))
        {
            expectedType = ltMPTOKEN_ISSUANCE;
            auto const unparsedMPTIssuanceID =
                context.params[jss::mpt_issuance];
            if (unparsedMPTIssuanceID.isString())
            {
                uint192 mptIssuanceID;
                if (!mptIssuanceID.parseHex(unparsedMPTIssuanceID.asString()))
                {
                    uNodeIndex = beast::zero;
                    jvResult[jss::error] = "malformedRequest";
                }
                else
                    uNodeIndex = keylet::mptIssuance(mptIssuanceID).key;
            }
            else
            {
                jvResult[jss::error] = "malformedRequest";
            }
        }
        else if (context.params.isMember(jss::mptoken))
        {
            expectedType = ltMPTOKEN;
            auto const& mptJson = context.params[jss::mptoken];
            if (!mptJson.isObject())
            {
                if (!uNodeIndex.parseHex(mptJson.asString()))
                {
                    uNodeIndex = beast::zero;
                    jvResult[jss::error] = "malformedRequest";
                }
            }
            else if (
                !mptJson.isMember(jss::mpt_issuance_id) ||
                !mptJson.isMember(jss::account))
            {
                jvResult[jss::error] = "malformedRequest";
            }
            else
            {
                try
                {
                    auto const mptIssuanceIdStr =
                        mptJson[jss::mpt_issuance_id].asString();

                    uint192 mptIssuanceID;
                    if (!mptIssuanceID.parseHex(mptIssuanceIdStr))
                        Throw<std::runtime_error>(
                            "Cannot parse mpt_issuance_id");

                    auto const account = parseBase58<AccountID>(
                        mptJson[jss::account].asString());

                    if (!account || account->isZero())
                        jvResult[jss::error] = "malformedAddress";
                    else
                        uNodeIndex =
                            keylet::mptoken(mptIssuanceID, *account).key;
                }
                catch (std::runtime_error const&)
                {
                    jvResult[jss::error] = "malformedRequest";
                }
            }
        }
        else
        {
            if (context.params.isMember("params") &&
                context.params["params"].isArray() &&
                context.params["params"].size() == 1 &&
                context.params["params"][0u].isString())
            {
                if (!uNodeIndex.parseHex(
                        context.params["params"][0u].asString()))
                {
                    uNodeIndex = beast::zero;
                    jvResult[jss::error] = "malformedRequest";
                }
            }
            else
            {
                if (context.apiVersion < 2u)
                    jvResult[jss::error] = "unknownOption";
                else
                    jvResult[jss::error] = "invalidParams";
            }
        }
    }
    catch (Json::error& e)
    {
        if (context.apiVersion > 1u)
        {
            // For apiVersion 2 onwards, any parsing failures that throw
            // this
            // exception return an invalidParam error.
            uNodeIndex = beast::zero;
            jvResult[jss::error] = "invalidParams";
        }
        else
            throw;
    }

    if (uNodeIndex.isNonZero())
    {
        auto const sleNode = lpLedger->read(keylet::unchecked(uNodeIndex));
        if (context.params.isMember(jss::binary))
            bNodeBinary = context.params[jss::binary].asBool();

        if (!sleNode)
        {
            // Not found.
            jvResult[jss::error] = "entryNotFound";
        }
        else if (
            (expectedType != ltANY) && (expectedType != sleNode->getType()))
        {
            jvResult[jss::error] = "unexpectedLedgerType";
        }
        else if (bNodeBinary)
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

    auto key = uint256::fromVoidChecked(request.key());
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
