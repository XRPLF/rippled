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
    if (auto err = readLimitField(limit, RPC::Tuning::kAccountNfTokens, context))
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

    // Continue iteration from the current page:
    bool pastMarker = marker.isZero();
    bool markerFound = false;
    uint256 const maskedMarker = marker & nft::kPageMask;
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
            uint256 const maskedNftokenID = nftokenID & nft::kPageMask;

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

                // Pull out the components of the nft ID.
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
    context.loadType = Resource::kFeeMediumBurdenRpc;
    return result;
}

}  // namespace xrpl
