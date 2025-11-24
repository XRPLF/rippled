#include <xrpld/app/tx/detail/NFTokenUtils.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/nftPageMask.h>
#include <xrpl/resource/Fees.h>

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

    if (!params[jss::account].isString())
        return RPC::invalid_field_error(jss::account);

    auto id = parseBase58<AccountID>(params[jss::account].asString());
    if (!id)
    {
        return rpcError(rpcACT_MALFORMED);
    }

    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);
    if (ledger == nullptr)
        return result;
    auto const accountID{id.value()};

    if (!ledger->exists(keylet::account(accountID)))
        return rpcError(rpcACT_NOT_FOUND);

    unsigned int limit;
    if (auto err = readLimitField(limit, RPC::Tuning::accountNFTokens, context))
        return *err;

    uint256 marker;
    bool const markerSet = params.isMember(jss::marker);

    if (markerSet)
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
    bool markerFound = false;
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

            if (!pastMarker)
            {
                if (maskedNftokenID < maskedMarker)
                    continue;

                if (maskedNftokenID == maskedMarker && nftokenID < marker)
                    continue;

                if (nftokenID == marker)
                {
                    markerFound = true;
                    continue;
                }
            }

            if (markerSet && !markerFound)
                return RPC::invalid_field_error(jss::marker);

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

    if (markerSet && !markerFound)
        return RPC::invalid_field_error(jss::marker);

    result[jss::account] = toBase58(accountID);
    context.loadType = Resource::feeMediumBurdenRPC;
    return result;
}

bool
getAccountObjects(
    ReadView const& ledger,
    AccountID const& account,
    std::optional<std::vector<LedgerEntryType>> const& typeFilter,
    uint256 dirIndex,
    uint256 entryIndex,
    std::uint32_t const limit,
    Json::Value& jvResult)
{
    // check if dirIndex is valid
    if (!dirIndex.isZero() && !ledger.read({ltDIR_NODE, dirIndex}))
        return false;

    auto typeMatchesFilter = [](std::vector<LedgerEntryType> const& typeFilter,
                                LedgerEntryType ledgerType) {
        auto it = std::find(typeFilter.begin(), typeFilter.end(), ledgerType);
        return it != typeFilter.end();
    };

    // if dirIndex != 0, then all NFTs have already been returned.  only
    // iterate NFT pages if the filter says so AND dirIndex == 0
    bool iterateNFTPages =
        (!typeFilter.has_value() ||
         typeMatchesFilter(typeFilter.value(), ltNFTOKEN_PAGE)) &&
        dirIndex == beast::zero;

    Keylet const firstNFTPage = keylet::nftpage_min(account);

    // we need to check the marker to see if it is an NFTTokenPage index.
    if (iterateNFTPages && entryIndex != beast::zero)
    {
        // if it is we will try to iterate the pages up to the limit
        // and then change over to the owner directory

        if (firstNFTPage.key != (entryIndex & ~nft::pageMask))
            iterateNFTPages = false;
    }

    auto& jvObjects = (jvResult[jss::account_objects] = Json::arrayValue);

    // this is a mutable version of limit, used to seamlessly switch
    // to iterating directory entries when nftokenpages are exhausted
    uint32_t mlimit = limit;

    // iterate NFTokenPages preferentially
    if (iterateNFTPages)
    {
        Keylet const first = entryIndex == beast::zero
            ? firstNFTPage
            : Keylet{ltNFTOKEN_PAGE, entryIndex};

        Keylet const last = keylet::nftpage_max(account);

        // current key
        uint256 ck = ledger.succ(first.key, last.key.next()).value_or(last.key);

        // current page
        auto cp = ledger.read(Keylet{ltNFTOKEN_PAGE, ck});

        while (cp)
        {
            jvObjects.append(cp->getJson(JsonOptions::none));
            auto const npm = (*cp)[~sfNextPageMin];
            if (npm)
                cp = ledger.read(Keylet(ltNFTOKEN_PAGE, *npm));
            else
                cp = nullptr;

            if (--mlimit == 0)
            {
                if (cp)
                {
                    jvResult[jss::limit] = limit;
                    jvResult[jss::marker] = std::string("0,") + to_string(ck);
                    return true;
                }
            }

            if (!npm)
                break;

            ck = *npm;
        }

        // if execution reaches here then we're about to transition
        // to iterating the root directory (and the conventional
        // behaviour of this RPC function.) Therefore we should
        // zero entryIndex so as not to terribly confuse things.
        entryIndex = beast::zero;
    }

    auto const root = keylet::ownerDir(account);
    auto found = false;

    if (dirIndex.isZero())
    {
        dirIndex = root.key;
        found = true;
    }

    auto dir = ledger.read({ltDIR_NODE, dirIndex});
    if (!dir)
    {
        // it's possible the user had nftoken pages but no
        // directory entries. If there's no nftoken page, we will
        // give empty array for account_objects.
        if (mlimit >= limit)
            jvResult[jss::account_objects] = Json::arrayValue;

        // non-zero dirIndex validity was checked in the beginning of this
        // function; by this point, it should be zero. This function returns
        // true regardless of nftoken page presence; if absent, account_objects
        // is already set as an empty array. Notice we will only return false in
        // this function when entryIndex can not be found, indicating an invalid
        // marker error.
        return true;
    }

    std::uint32_t i = 0;
    for (;;)
    {
        auto const& entries = dir->getFieldV256(sfIndexes);
        auto iter = entries.begin();

        if (!found)
        {
            iter = std::find(iter, entries.end(), entryIndex);
            if (iter == entries.end())
                return false;

            found = true;
        }

        // it's possible that the returned NFTPages exactly filled the
        // response.  Check for that condition.
        if (i == mlimit && mlimit < limit)
        {
            jvResult[jss::limit] = limit;
            jvResult[jss::marker] =
                to_string(dirIndex) + ',' + to_string(*iter);
            return true;
        }

        for (; iter != entries.end(); ++iter)
        {
            auto const sleNode = ledger.read(keylet::child(*iter));

            if (!typeFilter.has_value() ||
                typeMatchesFilter(typeFilter.value(), sleNode->getType()))
            {
                jvObjects.append(sleNode->getJson(JsonOptions::none));
            }

            if (++i == mlimit)
            {
                if (++iter != entries.end())
                {
                    jvResult[jss::limit] = limit;
                    jvResult[jss::marker] =
                        to_string(dirIndex) + ',' + to_string(*iter);
                    return true;
                }

                break;
            }
        }

        auto const nodeIndex = dir->getFieldU64(sfIndexNext);
        if (nodeIndex == 0)
            return true;

        dirIndex = keylet::page(root, nodeIndex).key;
        dir = ledger.read({ltDIR_NODE, dirIndex});
        if (!dir)
            return true;

        if (i == mlimit)
        {
            auto const& e = dir->getFieldV256(sfIndexes);
            if (!e.empty())
            {
                jvResult[jss::limit] = limit;
                jvResult[jss::marker] =
                    to_string(dirIndex) + ',' + to_string(*e.begin());
            }

            return true;
        }
    }
}

Json::Value
doAccountObjects(RPC::JsonContext& context)
{
    auto const& params = context.params;
    if (!params.isMember(jss::account))
        return RPC::missing_field_error(jss::account);

    if (!params[jss::account].isString())
        return RPC::invalid_field_error(jss::account);

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
            {jss::bridge, ltBRIDGE},
            {jss::mpt_issuance, ltMPTOKEN_ISSUANCE},
            {jss::mptoken, ltMPTOKEN},
            {jss::permissioned_domain, ltPERMISSIONED_DOMAIN},
            {jss::vault, ltVAULT},
        };

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

        if (!RPC::isAccountObjectsValidType(type))
            return RPC::invalid_field_error(jss::type);

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

        auto const& markerStr = marker.asString();
        auto const& idx = markerStr.find(',');
        if (idx == std::string::npos)
            return RPC::invalid_field_error(jss::marker);

        if (!dirIndex.parseHex(markerStr.substr(0, idx)))
            return RPC::invalid_field_error(jss::marker);

        if (!entryIndex.parseHex(markerStr.substr(idx + 1)))
            return RPC::invalid_field_error(jss::marker);
    }

    if (!getAccountObjects(
            *ledger,
            accountID,
            typeFilter,
            dirIndex,
            entryIndex,
            limit,
            result))
        return RPC::invalid_field_error(jss::marker);

    result[jss::account] = toBase58(accountID);
    context.loadType = Resource::feeMediumBurdenRPC;
    return result;
}

}  // namespace ripple
