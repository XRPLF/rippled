#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
class NFTokenOfferEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit NFTokenOfferEntry(
        AccountID const& owner,
        std::uint32_t seq,
        SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::nftokenOffer(owner, seq), view, j)
    {
    }

    explicit NFTokenOfferEntry(
        uint256 const& offerID,
        SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::nftokenOffer(offerID), view, j)
    {
    }
};

}  // namespace xrpl
