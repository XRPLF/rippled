/** @file
 *  Implements the `account_objects` RPC handler.
 *
 *  The handler returns all ledger objects owned by a specific XRPL account.
 *  It bridges two disjoint ledger structures — NFT pages (`ltNFTOKEN_PAGE`)
 *  and the owner directory (`ltDIR_NODE`) — behind a single paginated API.
 *  Pagination is expressed as a `"<dirIndex>,<entryIndex>"` marker; a zero
 *  `dirIndex` signals an NFT-phase resume while a non-zero `dirIndex` means
 *  all NFT pages were already returned.
 */
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

/** Populate `jvResult` with owned ledger objects for an account.
 *
 *  Traverses two disjoint ledger structures in order: NFT pages first
 *  (a sorted range of `ltNFTOKEN_PAGE` entries keyed by account prefix),
 *  then the owner directory (`ltDIR_NODE` linked list). The two phases
 *  share a single `mlimit` budget so the combined result never exceeds
 *  `limit` entries.
 *
 *  NFT page iteration is skipped when resuming mid-directory: a non-zero
 *  `dirIndex` in a resumed call implies all NFT pages were already emitted
 *  in a prior response.
 *
 *  The marker emitted when the NFT phase fills the page exactly is
 *  `"0,<last_nft_page_key>"`. A marker emitted mid-directory has the form
 *  `"<dirIndex>,<entryIndex>"`. The caller interprets the zero `dirIndex`
 *  as the NFT-resume signal.
 *
 *  @param ledger The ledger view to search.
 *  @param account The account whose owned objects are requested.
 *  @param typeFilter If set, only objects whose `LedgerEntryType` appears
 *      in the vector are included; an empty optional means all types.
 *  @param dirIndex The owner-directory page key at which to resume; zero
 *      means start from the beginning (and iterate NFT pages first).
 *  @param entryIndex Within the page identified by `dirIndex`, the first
 *      entry to include; zero means start from the first entry of that
 *      page. When `dirIndex` is zero this may instead encode an NFT page
 *      key for a mid-NFT-chain resume.
 *  @param limit Maximum number of objects to add to `jvResult`. The
 *      result may contain fewer entries if the account has fewer objects.
 *  @param jvResult Output JSON object. The `account_objects` array is
 *      appended here; `limit` and `marker` fields are set when there are
 *      more objects to fetch.
 *  @return `true` in all normal cases (including "nothing to return").
 *      `false` only when `entryIndex` is non-zero and cannot be located
 *      in the expected directory node, indicating a stale or corrupted
 *      marker.
 *  @note The `dirIndex` validity check at entry is the only case that
 *      returns `false`; all other early exits return `true` with an empty
 *      or partially populated `account_objects` array.
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
    if (!dirIndex.isZero() && !ledger.read({ltDIR_NODE, dirIndex}))
        return false;

    auto typeMatchesFilter = [](std::vector<LedgerEntryType> const& typeFilter,
                                LedgerEntryType ledgerType) {
        auto it = std::ranges::find(typeFilter, ledgerType);
        return it != typeFilter.end();
    };

    // A non-zero dirIndex means the client is resuming mid-directory, so all
    // NFT pages were already returned in a prior response.
    bool iterateNFTPages =
        (!typeFilter.has_value() || typeMatchesFilter(typeFilter.value(), ltNFTOKEN_PAGE)) &&
        dirIndex == beast::kZERO;

    Keylet const firstNFTPage = keylet::nftpageMin(account);

    // A non-zero entryIndex with a zero dirIndex may encode an NFT page key.
    // Strip the lower 96-bit token-sort portion (~nft::kPAGE_MASK) and compare
    // with the account's minimum NFT page key to decide which phase to resume.
    if (iterateNFTPages && entryIndex != beast::kZERO)
    {
        if (firstNFTPage.key != (entryIndex & ~nft::kPAGE_MASK))
            iterateNFTPages = false;
    }

    auto& jvObjects = (jvResult[jss::account_objects] = json::ValueType::Array);

    // Mutable budget shared across both phases so neither loop needs to
    // communicate remaining capacity to the other.
    uint32_t mlimit = limit;

    if (iterateNFTPages)
    {
        Keylet const first =
            entryIndex == beast::kZERO ? firstNFTPage : Keylet{ltNFTOKEN_PAGE, entryIndex};

        Keylet const last = keylet::nftpageMax(account);

        uint256 ck = ledger.succ(first.key, last.key.next()).value_or(last.key);

        auto cp = ledger.read(Keylet{ltNFTOKEN_PAGE, ck});

        while (cp)
        {
            jvObjects.append(cp->getJson(JsonOptions::Values::None));
            auto const npm = (*cp)[~sfNextPageMin];
            if (npm)
            {
                cp = ledger.read(Keylet(ltNFTOKEN_PAGE, *npm));
            }
            else
            {
                cp = nullptr;
            }

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

        // Reset entryIndex before entering the owner-directory phase so
        // it is not mistakenly treated as a directory-entry resume key.
        entryIndex = beast::kZERO;
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
        // Account may have NFT pages but no owner-directory entries (e.g.
        // only holds NFTs). Emit an empty array only when mlimit is still
        // intact, meaning the NFT phase did not already initialise jvObjects.
        if (mlimit >= limit)
            jvResult[jss::account_objects] = json::ValueType::Array;

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

        // NFT phase may have consumed the full budget; emit a directory-style
        // marker at the first entry of this node before consuming any entries.
        if (i == mlimit && mlimit < limit)
        {
            jvResult[jss::limit] = limit;
            jvResult[jss::marker] = to_string(dirIndex) + ',' + to_string(*iter);
            return true;
        }

        for (; iter != entries.end(); ++iter)
        {
            auto const sleNode = ledger.read(keylet::child(*iter));

            if (!typeFilter.has_value() ||
                typeMatchesFilter(typeFilter.value(), sleNode->getType()))
            {
                jvObjects.append(sleNode->getJson(JsonOptions::Values::None));
            }

            if (++i == mlimit)
            {
                if (++iter != entries.end())
                {
                    jvResult[jss::limit] = limit;
                    jvResult[jss::marker] = to_string(dirIndex) + ',' + to_string(*iter);
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
                jvResult[jss::marker] = to_string(dirIndex) + ',' + to_string(*e.begin());
            }

            return true;
        }
    }
}

/** Handle the `account_objects` RPC command.
 *
 *  Returns all ledger objects owned by the requested account, with optional
 *  filtering by type and support for paginated traversal via a marker.
 *
 *  Two filter modes are mutually exclusive:
 *  - **`deletion_blockers_only`**: restricts results to the compile-time
 *    set of types that prevent account deletion (`ltCHECK`, `ltESCROW`,
 *    `ltNFTOKEN_PAGE`, `ltPAYCHAN`, `ltRIPPLE_STATE`, cross-chain objects,
 *    `ltMPTOKEN_ISSUANCE`, `ltMPTOKEN`, `ltPERMISSIONED_DOMAIN`, `ltVAULT`).
 *    A `type` parameter alongside this flag intersects the two sets.
 *  - **`type`** (standalone): restricts results to a single entry type.
 *    Global ledger types that can never be account-owned
 *    (`ltAMENDMENTS`, `ltDIR_NODE`, `ltFEE_SETTINGS`, `ltLEDGER_HASHES`,
 *    `ltNEGATIVE_UNL`) are rejected with `rpcINVALID_PARAMS`.
 *
 *  Pagination limit is clamped to `RPC::Tuning::kACCOUNT_OBJECTS`
 *  (min 10, default 200, max 400). The `marker` field must be a string of
 *  the form `"<hex256>,<hex256>"`; any other format returns
 *  `rpcINVALID_PARAMS`. A marker that cannot be located in the ledger
 *  (stale or corrupted) also returns `rpcINVALID_PARAMS`.
 *
 *  @param context The RPC dispatch context carrying the request parameters,
 *      ledger access, and resource accounting state.
 *  @return A JSON object containing `account_objects` (array of ledger
 *      entry JSON), `account` (Base58 account ID), and — when more results
 *      remain — `limit` and `marker` fields for the next page.
 *  @note Resource cost is `feeMediumBurdenRPC` because a full scan may
 *      traverse many chained directory nodes and NFT pages.
 */
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
        } static constexpr kDELETION_BLOCKERS[] = {
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
        typeFilter->reserve(std::size(kDELETION_BLOCKERS));

        for (auto [name, type] : kDELETION_BLOCKERS)
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
    if (auto err = readLimitField(limit, RPC::Tuning::kACCOUNT_OBJECTS, context))
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
    context.loadType = Resource::kFEE_MEDIUM_BURDEN_RPC;
    return result;
}

}  // namespace xrpl
