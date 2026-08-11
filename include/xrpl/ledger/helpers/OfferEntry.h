#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
class OfferEntry : public SLEBase<ViewT, ltOFFER>
{
public:
    using Base = SLEBase<ViewT, ltOFFER>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit OfferEntry(
        AccountID const& id,
        std::uint32_t seq,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::offer(id, seq), view, j)
    {
    }

    explicit OfferEntry(
        uint256 const& offerID,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::offer(offerID), view, j)
    {
    }
};

using ROfferEntry = OfferEntry<ReadView>;
using WOfferEntry = OfferEntry<ApplyView>;

}  // namespace xrpl
