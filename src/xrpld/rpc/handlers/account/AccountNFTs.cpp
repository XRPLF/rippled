#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/nft.h>
#include <xrpl/protocol/nftPageMask.h>
#include <xrpl/resource/Fees.h>

#include <cstdint>
#include <memory>

namespace xrpl {

/** Enumerate all NFTs held by an account, with cursor-based pagination.
 *
 *  Walks the account's `NFTokenPage` chain — a singly-linked list of ledger
 *  objects, each holding up to 32 tokens sorted by the low 96 bits of their
 *  `NFTokenID` (issuer + taxon + serial). For each token the handler decodes
 *  and injects `Flags`, `Issuer`, `NFTokenTaxon`, `nft_serial`, and
 *  (conditionally) `TransferFee` as top-level response fields alongside the
 *  raw `STObject` JSON.
 *
 *  Pagination uses the full 256-bit `NFTokenID` of the last emitted token as
 *  the resumption marker. When a marker is supplied, the handler skips
 *  directly to the page that could contain it via `ledger->succ()` and then
 *  applies a two-level comparison: first on the masked (low 96-bit) key that
 *  determines page sort order, then on the full ID to handle tokens that
 *  share the same masked value (same issuer/taxon/serial but different flags
 *  or transfer fee). If a marker is present but is not found in the current
 *  ledger state, the handler returns `invalidFieldError(marker)`.
 *
 *  Request fields:
 *  - `account`      (required) Base58-encoded account address.
 *  - `ledger_hash`  (optional) Identify ledger by hash.
 *  - `ledger_index` (optional) Identify ledger by index or shortcut string.
 *  - `limit`        (optional) Result page size; clamped to
 *      `RPC::Tuning::kACCOUNT_NF_TOKENS` [20, 100, 400].
 *  - `marker`       (optional) Hex `uint256` resumption cursor from a prior
 *      response.
 *
 *  Response fields (non-paginated):
 *  - `account_nfts` JSON array of decoded NFT objects.
 *  - `account`      Base58 account string (echoed only when the full scan
 *      completes without truncation; clients should detect pagination by
 *      the presence of `marker`, not the absence of `account`).
 *
 *  Response fields (paginated):
 *  - `account_nfts` Partial array up to `limit` entries.
 *  - `limit`        The effective limit applied.
 *  - `marker`       Full hex `NFTokenID` of the last emitted token.
 *
 *  @param context  JSON-RPC dispatch context carrying request params, ledger
 *      master, role, and resource-fee handle.
 *  @return JSON result object, or an RPC error value on failure.
 *  @note `TransferFee` is omitted from each token object when its value is
 *      zero, because a zero fee is semantically equivalent to "not set".
 *  @note `context.loadType` is set to `feeMediumBurdenRPC` only on a
 *      complete (non-paginated) scan; mid-scan early returns skip the charge.
 *  @see AccountObjects for the general owner-directory counterpart.
 */
json::Value
doAccountNFTs(RPC::JsonContext& context)
{
    auto const& params = context.params;
    if (!params.isMember(jss::account))
        return RPC::missingFieldError(jss::account);

    if (!params[jss::account].isString())
        return RPC::invalidFieldError(jss::account);

    auto id = parseBase58<AccountID>(params[jss::account].asString());
    if (!id)
    {
        return rpcError(RpcActMalformed);
    }

    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);
    if (ledger == nullptr)
        return result;
    auto const accountID{id.value()};

    if (!ledger->exists(keylet::account(accountID)))
        return rpcError(RpcActNotFound);

    unsigned int limit = 0;
    if (auto err = readLimitField(limit, RPC::Tuning::kACCOUNT_NF_TOKENS, context))
        return *err;

    uint256 marker;
    bool const markerSet = params.isMember(jss::marker);

    if (markerSet)
    {
        auto const& m = params[jss::marker];
        if (!m.isString())
            return RPC::expectedFieldError(jss::marker, "string");

        if (!marker.parseHex(m.asString()))
            return RPC::invalidFieldError(jss::marker);
    }

    auto const first = keylet::nftpage(keylet::nftpageMin(accountID), marker);
    auto const last = keylet::nftpageMax(accountID);

    auto cp = ledger->read(
        Keylet(ltNFTOKEN_PAGE, ledger->succ(first.key, last.key.next()).value_or(last.key)));

    std::uint32_t cnt = 0;
    auto& nfts = (result[jss::account_nfts] = json::ValueType::Array);

    bool pastMarker = marker.isZero();
    bool markerFound = false;
    uint256 const maskedMarker = marker & nft::kPAGE_MASK;
    while (cp)
    {
        auto arr = cp->getFieldArray(sfNFTokens);

        for (auto const& o : arr)
        {
            uint256 const nftokenID = o[sfNFTokenID];
            uint256 const maskedNftokenID = nftokenID & nft::kPAGE_MASK;

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
                return RPC::invalidFieldError(jss::marker);

            pastMarker = true;

            {
                json::Value& obj = nfts.append(o.getJson(JsonOptions::Values::None));

                obj[sfFlags.jsonName] = nft::getFlags(nftokenID);
                obj[sfIssuer.jsonName] = to_string(nft::getIssuer(nftokenID));
                obj[sfNFTokenTaxon.jsonName] = nft::toUInt32(nft::getTaxon(nftokenID));
                obj[jss::nft_serial] = nft::getSerial(nftokenID);
                if (std::uint16_t const xferFee = {nft::getTransferFee(nftokenID)})
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
        {
            cp = ledger->read(Keylet(ltNFTOKEN_PAGE, *npm));
        }
        else
        {
            cp = nullptr;
        }
    }

    if (markerSet && !markerFound)
        return RPC::invalidFieldError(jss::marker);

    result[jss::account] = toBase58(accountID);
    context.loadType = Resource::kFEE_MEDIUM_BURDEN_RPC;
    return result;
}

}  // namespace xrpl
