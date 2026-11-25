#include <xrpld/rpc/handlers/account/AccountNFTs.h>

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
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>  // IWYU pragma: keep
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/nft.h>
#include <xrpl/protocol/nftPageMask.h>
#include <xrpl/resource/Fees.h>

#include <rpcspec/Errors.hpp>
#include <rpcspec/handlers/account_nfts/Types.hpp>

#include <cstdint>
#include <expected>

namespace xrpl::rpc {

static_assert(::rpc::spec::handlers::account_nfts::kLimitMin == tuning::kAccountNfTokens.rmin);
static_assert(
    ::rpc::spec::handlers::account_nfts::kLimitDefault == tuning::kAccountNfTokens.rDefault);
static_assert(::rpc::spec::handlers::account_nfts::kLimitMax == tuning::kAccountNfTokens.rmax);

AccountNFTsHandler::AccountNFTsHandler(JsonContext& context) : context_(context)
{
}

std::expected<AccountNFTsHandler::Output, ::rpc::Status>
AccountNFTsHandler::process(Input const& input) const
{
    auto& context = context_.get();

    Output output;
    if (auto const status = getLedger(output.ledger, input.ledger, context);
        status != ::rpc::Status::kOK)
        return std::unexpected{status};
    auto const& ledger = *output.ledger;
    auto const accountID = input.account;
    output.account = accountID;

    if (!ledger.exists(keylet::account(accountID)))
        return std::unexpected{RpcActNotFound};

    output.limit = clampLimit(input.limit, tuning::kAccountNfTokens, context.role);

    ::rpc::Status const invalidMarker{RpcInvalidParams, invalidFieldMessage(jss::marker)};
    bool const markerSet = input.marker.has_value();
    UInt256 const marker = input.marker.value_or(UInt256{});

    auto const first = keylet::nftokenPage(keylet::nftokenPageMin(accountID), marker);
    auto const last = keylet::nftokenPageMax(accountID);

    auto cp = ledger.read(
        Keylet(ltNFTOKEN_PAGE, ledger.succ(first.key, last.key.next()).value_or(last.key)));

    std::uint32_t cnt = 0;

    // Continue iteration from the current page:
    bool pastMarker = marker.isZero();
    bool markerFound = false;
    UInt256 const maskedMarker = marker & nft::kPageMask;
    while (cp)
    {
        auto const& pageNftokens = cp->getFieldArray(sfNFTokens);

        for (auto const& nftoken : pageNftokens)
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
            UInt256 const nftokenID = nftoken[sfNFTokenID];
            UInt256 const maskedNftokenID = nftokenID & nft::kPageMask;

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
                return std::unexpected{invalidMarker};

            pastMarker = true;

            output.nfts.push_back(nftoken);

            if (++cnt == output.limit)
            {
                output.marker = nftokenID;
                return output;
            }
        }

        if (auto const npm = (*cp)[~sfNextPageMin]; npm.has_value())
        {
            cp = ledger.read(Keylet(ltNFTOKEN_PAGE, *npm));
        }
        else
        {
            cp = nullptr;
        }
    }

    if (markerSet && !markerFound)
        return std::unexpected{invalidMarker};

    context.loadType = resource::kFeeMediumBurdenRpc;
    return output;
}

void
AccountNFTsHandler::writeResult(json::Value& value, Output const& output) const
{
    injectLedgerFields(*output.ledger, context_.get(), value);

    auto& nfts = (value[jss::account_nfts] = json::ValueType::Array);
    for (auto const& nftoken : output.nfts)
    {
        json::Value& obj = nfts.append(nftoken.getJson(JsonOptions::Values::None));

        // Pull out the components of the nft ID.
        UInt256 const nftokenID = nftoken[sfNFTokenID];
        obj[sfFlags.jsonName] = nft::getFlags(nftokenID);
        obj[sfIssuer.jsonName] = to_string(nft::getIssuer(nftokenID));
        obj[sfNFTokenTaxon.jsonName] = nft::toUInt32(nft::getTaxon(nftokenID));
        obj[jss::nft_serial] = nft::getSequence(nftokenID);
        if (std::uint16_t const xferFee = {nft::getTransferFee(nftokenID)})
            obj[sfTransferFee.jsonName] = xferFee;
    }

    if (output.marker.has_value())
    {
        value[jss::limit] = output.limit;
        value[jss::marker] = to_string(*output.marker);
        return;
    }

    value[jss::account] = toBase58(output.account);
}

}  // namespace xrpl::rpc
