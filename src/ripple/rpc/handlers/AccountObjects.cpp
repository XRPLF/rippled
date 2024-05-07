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

#include <ripple/app/main/Application.h>
#include <ripple/app/tx/impl/details/NFTokenUtils.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/LedgerFormats.h>
#include <ripple/protocol/RPCErr.h>
#include <ripple/protocol/jss.h>
#include <ripple/protocol/nftPageMask.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <ripple/rpc/impl/Tuning.h>

#include <sstream>
#include <string>

namespace ripple {

/** General RPC command that can retrieve objects in the account root.
    {
      account: <account>
      ledger_hash: <string> // optional
      ledger_index: <string | unsigned integer> // optional
      type: <string> // optional, defaults to all account objects types
      limit: <integer> // optional
      marker: <opaque> // optional, resume previous query
    }
*/

Json::Value
doAccountNFTs(RPC::JsonContext& context)
{
    auto const& params = context.params;
    if (!params.isMember(jss::account))
        return RPC::missing_field_error(jss::account);

    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);
    if (ledger == nullptr)
        return result;

    auto id = parseBase58<AccountID>(params[jss::account].asString());
    if (!id)
    {
        RPC::inject_error(rpcACT_MALFORMED, result);
        return result;
    }
    auto const accountID{id.value()};

    if (!ledger->exists(keylet::account(accountID)))
        return rpcError(rpcACT_NOT_FOUND);

    unsigned int limit;
    if (auto err = readLimitField(limit, RPC::Tuning::accountNFTokens, context))
        return *err;

    uint256 marker;

    if (params.isMember(jss::marker))
    {
        auto const& m = params[jss::marker];
        if (!m.isString())
            return RPC::expected_field_error(jss::marker, "string");

        if (!marker.parseHex(m.asString()))
            return RPC::invalid_field_error(jss::marker);
    }

    auto const first = keylet::nftpage(keylet::nftpage_min(accountID), marker);
    auto const last = keylet::nftpage_max(accountID);

    auto cp = ledger->read(Keylet(
        ltNFTOKEN_PAGE,
        ledger->succ(first.key, last.key.next()).value_or(last.key)));

    std::uint32_t cnt = 0;
    auto& nfts = (result[jss::account_nfts] = Json::arrayValue);

    // Continue iteration from the current page:
    bool pastMarker = marker.isZero();
    uint256 const maskedMarker = marker & nft::pageMask;
    while (cp)
    {
        auto arr = cp->getFieldArray(sfNFTokens);

        for (auto const& o : arr)
        {
            // Scrolling past the marker gets weird.  We need to look at
            // a couple of conditions.
            //
            //  1. If the low 96-bits don't match, then we compare only
            //     against the low 96-bits, since that's what determines
            //     the sort order of the pages.
            //
            //  2. However, within one page there can be a number of
            //     NFTokenIDs that all have the same low 96 bits.  If we're
            //     in that case then we need to compare against the full
            //     256 bits.
            uint256 const nftokenID = o[sfNFTokenID];
            uint256 const maskedNftokenID = nftokenID & nft::pageMask;

            if (!pastMarker && maskedNftokenID < maskedMarker)
                continue;

            if (!pastMarker && maskedNftokenID == maskedMarker &&
                nftokenID <= marker)
                continue;

            pastMarker = true;

            {
                Json::Value& obj = nfts.append(o.getJson(JsonOptions::none));

                // Pull out the components of the nft ID.
                obj[sfFlags.jsonName] = nft::getFlags(nftokenID);
                obj[sfIssuer.jsonName] = to_string(nft::getIssuer(nftokenID));
                obj[sfNFTokenTaxon.jsonName] =
                    nft::toUInt32(nft::getTaxon(nftokenID));
                obj[jss::nft_serial] = nft::getSerial(nftokenID);
                if (std::uint16_t xferFee = {nft::getTransferFee(nftokenID)})
                    obj[sfTransferFee.jsonName] = xferFee;
            }

            if (++cnt == limit)
            {
                result[jss::limit] = limit;
                result[jss::marker] = to_string(o.getFieldH256(sfNFTokenID));
                return result;
            }
        }

        if (auto npm = (*cp)[~sfNextPageMin])
            cp = ledger->read(Keylet(ltNFTOKEN_PAGE, *npm));
        else
            cp = nullptr;
    }

    result[jss::account] = toBase58(accountID);
    context.loadType = Resource::feeMediumBurdenRPC;
    return result;
}

Json::Value
doAccountObjects(RPC::JsonContext& context)
{
    auto const& params = context.params;
    if (!params.isMember(jss::account))
        return RPC::missing_field_error(jss::account);

    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);
    if (ledger == nullptr)
        return result;

    auto const id = parseBase58<AccountID>(params[jss::account].asString());
    if (!id)
    {
        RPC::inject_error(rpcACT_MALFORMED, result);
        return result;
    }
    auto const accountID{id.value()};

    if (!ledger->exists(keylet::account(accountID)))
        return rpcError(rpcACT_NOT_FOUND);

    std::optional<std::vector<LedgerEntryType>> typeFilter;

    if (params.isMember(jss::deletion_blockers_only) &&
        params[jss::deletion_blockers_only].asBool())
    {
        struct
        {
            Json::StaticString name;
            LedgerEntryType type;
        } static constexpr deletionBlockers[] = {
            {jss::check, ltCHECK},
            {jss::escrow, ltESCROW},
            {jss::nft_page, ltNFTOKEN_PAGE},
            {jss::payment_channel, ltPAYCHAN},
            {jss::state, ltRIPPLE_STATE},
            {jss::xchain_owned_claim_id, ltXCHAIN_OWNED_CLAIM_ID},
            {jss::xchain_owned_create_account_claim_id,
             ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID},
            {jss::bridge, ltBRIDGE}};

        typeFilter.emplace();
        typeFilter->reserve(std::size(deletionBlockers));

        for (auto [name, type] : deletionBlockers)
        {
            if (params.isMember(jss::type) && name != params[jss::type])
            {
                continue;
            }

            typeFilter->push_back(type);
        }
    }
    else
    {
        auto [rpcStatus, type] = RPC::chooseLedgerEntryType(params);

        if (rpcStatus)
        {
            result.clear();
            rpcStatus.inject(result);
            return result;
        }
        else if (type != ltANY)
        {
            typeFilter = std::vector<LedgerEntryType>({type});
        }
    }

    unsigned int limit;
    if (auto err = readLimitField(limit, RPC::Tuning::accountObjects, context))
        return *err;

    uint256 dirIndex;
    uint256 entryIndex;
    if (params.isMember(jss::marker))
    {
        auto const& marker = params[jss::marker];
        if (!marker.isString())
            return RPC::expected_field_error(jss::marker, "string");

        std::stringstream ss(marker.asString());
        std::string s;
        if (!std::getline(ss, s, ','))
            return RPC::invalid_field_error(jss::marker);

        if (!dirIndex.parseHex(s))
            return RPC::invalid_field_error(jss::marker);

        if (!std::getline(ss, s, ','))
            return RPC::invalid_field_error(jss::marker);

        if (!entryIndex.parseHex(s))
            return RPC::invalid_field_error(jss::marker);
    }

    if (!RPC::getAccountObjects(
            *ledger,
            accountID,
            typeFilter,
            dirIndex,
            entryIndex,
            limit,
            result))
    {
        result[jss::account_objects] = Json::arrayValue;
    }

    result[jss::account] = toBase58(accountID);
    context.loadType = Resource::feeMediumBurdenRPC;
    return result;
}

}  // namespace ripple
