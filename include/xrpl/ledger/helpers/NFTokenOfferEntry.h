#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
class NFTokenOfferEntry : public SLEBase<ViewT, ltNFTOKEN_OFFER>
{
public:
    using Base = SLEBase<ViewT, ltNFTOKEN_OFFER>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit NFTokenOfferEntry(
        AccountID const& owner,
        std::uint32_t seq,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::nftokenOffer(owner, seq), view, j)
    {
    }

    explicit NFTokenOfferEntry(
        uint256 const& offerID,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::nftokenOffer(offerID), view, j)
    {
    }
};

using RNFTokenOfferEntry = NFTokenOfferEntry<ReadView>;
using WNFTokenOfferEntry = NFTokenOfferEntry<ApplyView>;

}  // namespace xrpl
