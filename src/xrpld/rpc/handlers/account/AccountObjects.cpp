#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/nftPageMask.h>
#include <xrpl/resource/Fees.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace xrpl {

/** Gathers all objects for an account in a ledger.
    @param ledger Ledger to search account objects.
    @param account AccountID to find objects for.
    @param typeFilter Gathers objects of these types. empty gathers all types.
    @param dirIndex Begin gathering account objects from this directory.
    @param entryIndex Begin gathering objects from this directory node.
    @param limit Maximum number of objects to find.
    @param jvResult A JSON result that holds the request objects.
*/
bool
getAccountObjects(
    ReadView const& ledger,
    AccountID const& account,
    std::optional<std::vector<LedgerEntryType>> const& typeFilter,
    uint256 dirIndex,
    uint256 entryIndex,
    std::uint32_t const limit,
    json::Value& jvResult)
{
    // check if dirIndex is valid
    if (!dirIndex.isZero() && !ledger.read({ltDIR_NODE, dirIndex}))
        return false;

    auto typeMatchesFilter = [](std::vector<LedgerEntryType> const& typeFilter,
                                LedgerEntryType ledgerType) {
        auto it = std::ranges::find(typeFilter, ledgerType);
        return it != typeFilter.end();
    };

    // if dirIndex != 0, then all NFTs have already been returned.  only
    // iterate NFT pages if the filter says so AND dirIndex == 0
    bool iterateNFTPages =
        (!typeFilter.has_value() || typeMatchesFilter(typeFilter.value(), ltNFTOKEN_PAGE)) &&
        dirIndex.isZero();

    Keylet const firstNFTPage = keylet::nftpageMin(account);

    // we need to check the marker to see if it is an NFTTokenPage index.
    if (iterateNFTPages && entryIndex.isNonZero())
    {
        // if it is we will try to iterate the pages up to the limit
        // and then change over to the owner directory

        if (firstNFTPage.key != (entryIndex & ~nft::kPageMask))
            iterateNFTPages = false;
    }

    auto& jvObjects = (jvResult[jss::account_objects] = json::ValueType::Array);

    // this is a mutable version of limit, used to seamlessly switch
    // to iterating directory entries when nftokenpages are exhausted
    uint32_t limitLeft = limit;

    // iterate NFTokenPages preferentially
    if (iterateNFTPages)
    {
        Keylet const first =
            entryIndex.isZero() ? firstNFTPage : Keylet{ltNFTOKEN_PAGE, entryIndex};

        Keylet const last = keylet::nftpageMax(account);

        auto currentKey = ledger.succ(first.key, last.key.next()).value_or(last.key);

        auto currentPage = ledger.read(Keylet{ltNFTOKEN_PAGE, currentKey});

        while (currentPage)
        {
            jvObjects.append(currentPage->getJson(JsonOptions::Values::None));
            auto const npm = (*currentPage)[~sfNextPageMin];
            if (npm)
            {
                currentPage = ledger.read(Keylet(ltNFTOKEN_PAGE, *npm));
            }
            else
            {
                currentPage = nullptr;
            }

            if (--limitLeft == 0 && currentPage)
            {
                jvResult[jss::limit] = limit;
                jvResult[jss::marker] = std::string("0,") + to_string(currentKey);
                return true;
            }

            if (!npm)
                break;

            currentKey = *npm;
        }

        // if execution reaches here then we're about to transition
        // to iterating the root directory (and the conventional
        // behaviour of this RPC function.) Therefore we should
        // zero entryIndex so as not to terribly confuse things.
        entryIndex = beast::kZero;
    }

    auto const root = keylet::ownerDir(account);
    auto startEntryFound = false;

    if (dirIndex.isZero())
    {
        dirIndex = root.key;
        startEntryFound = true;
    }

    auto dir = ledger.read({ltDIR_NODE, dirIndex});
    if (!dir)
    {
        // it's possible the user had nftoken pages but no
        // directory entries. If there's no nftoken page, we will
        // give empty array for account_objects.
        if (limitLeft >= limit)
            jvResult[jss::account_objects] = json::ValueType::Array;

        // non-zero dirIndex validity was checked in the beginning of this
        // function; by this point, it should be zero. This function returns
        // true regardless of nftoken page presence; if absent, account_objects
        // is already set as an empty array. Notice we will only return false in
        // this function when entryIndex can not be found, indicating an invalid
        // marker error.
        return true;
    }

    std::uint32_t itemsAdded = 0;
    for (;;)
    {
        auto const& dirEntries = dir->getFieldV256(sfIndexes);
        auto entryIter = dirEntries.begin();

        if (!startEntryFound)
        {
            entryIter = std::find(entryIter, dirEntries.end(), entryIndex);
            if (entryIter == dirEntries.end())
                return false;

            startEntryFound = true;
        }

        // it's possible that the returned NFTPages exactly filled the
        // response.  Check for that condition.
        if (itemsAdded == limitLeft && limitLeft < limit && entryIter != dirEntries.end())
        {
            jvResult[jss::limit] = limit;
            jvResult[jss::marker] = to_string(dirIndex) + ',' + to_string(*entryIter);
            return true;
        }

        for (; entryIter != dirEntries.end(); ++entryIter)
        {
            auto const sleNode = ledger.read(keylet::child(*entryIter));

            if (!typeFilter.has_value() ||
                typeMatchesFilter(typeFilter.value(), sleNode->getType()))
            {
                jvObjects.append(sleNode->getJson(JsonOptions::Values::None));
            }

            if (++itemsAdded == limitLeft)
            {
                if (++entryIter != dirEntries.end())
                {
                    jvResult[jss::limit] = limit;
                    jvResult[jss::marker] = to_string(dirIndex) + ',' + to_string(*entryIter);
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

        if (itemsAdded == limitLeft)
        {
            auto const& currentDirEntries = dir->getFieldV256(sfIndexes);
            if (!currentDirEntries.empty())
            {
                jvResult[jss::limit] = limit;
                jvResult[jss::marker] =
                    to_string(dirIndex) + ',' + to_string(*currentDirEntries.begin());
            }

            return true;
        }
    }
}

json::Value
doAccountObjects(RPC::JsonContext& context)
{
    auto const& params = context.params;
    if (!params.isMember(jss::account))
        return RPC::missingFieldError(jss::account);

    if (!params[jss::account].isString())
        return RPC::invalidFieldError(jss::account);

    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);
    if (ledger == nullptr)
        return result;

    auto const id = parseBase58<AccountID>(params[jss::account].asString());
    if (!id)
    {
        RPC::injectError(RpcActMalformed, result);
        return result;
    }
    auto const accountID{id.value()};

    if (!ledger->exists(keylet::account(accountID)))
        return rpcError(RpcActNotFound);

    std::optional<std::vector<LedgerEntryType>> typeFilter;

    if (params.isMember(jss::deletion_blockers_only) &&
        params[jss::deletion_blockers_only].asBool())
    {
        struct
        {
            json::StaticString name;
            LedgerEntryType type;
        } static constexpr kDeletionBlockers[] = {
            {.name = jss::check, .type = ltCHECK},
            {.name = jss::escrow, .type = ltESCROW},
            {.name = jss::nft_page, .type = ltNFTOKEN_PAGE},
            {.name = jss::payment_channel, .type = ltPAYCHAN},
            {.name = jss::state, .type = ltRIPPLE_STATE},
            {.name = jss::xchain_owned_claim_id, .type = ltXCHAIN_OWNED_CLAIM_ID},
            {.name = jss::xchain_owned_create_account_claim_id,
             .type = ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID},
            {.name = jss::bridge, .type = ltBRIDGE},
            {.name = jss::mpt_issuance, .type = ltMPTOKEN_ISSUANCE},
            {.name = jss::mptoken, .type = ltMPTOKEN},
            {.name = jss::permissioned_domain, .type = ltPERMISSIONED_DOMAIN},
            {.name = jss::vault, .type = ltVAULT},
        };

        typeFilter.emplace();
        typeFilter->reserve(std::size(kDeletionBlockers));

        for (auto [name, type] : kDeletionBlockers)
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
            return RPC::invalidFieldError(jss::type);

        if (rpcStatus)
        {
            result.clear();
            rpcStatus.inject(result);
            return result;
        }
        if (type != ltANY)
        {
            typeFilter = std::vector<LedgerEntryType>({type});
        }
    }

    unsigned int limit = 0;
    if (auto err = readLimitField(limit, RPC::Tuning::kAccountObjects, context))
        return *err;

    uint256 dirIndex;
    uint256 entryIndex;
    if (params.isMember(jss::marker))
    {
        auto const& marker = params[jss::marker];
        if (!marker.isString())
            return RPC::expectedFieldError(jss::marker, "string");

        auto const& markerStr = marker.asString();
        auto const& idx = markerStr.find(',');
        if (idx == std::string::npos)
            return RPC::invalidFieldError(jss::marker);

        if (!dirIndex.parseHex(markerStr.substr(0, idx)))
            return RPC::invalidFieldError(jss::marker);

        if (!entryIndex.parseHex(markerStr.substr(idx + 1)))
            return RPC::invalidFieldError(jss::marker);
    }

    if (!getAccountObjects(*ledger, accountID, typeFilter, dirIndex, entryIndex, limit, result))
        return RPC::invalidFieldError(jss::marker);

    result[jss::account] = toBase58(accountID);
    context.loadType = Resource::kFeeMediumBurdenRpc;
    return result;
}

}  // namespace xrpl
