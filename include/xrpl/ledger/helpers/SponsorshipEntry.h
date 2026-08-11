#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>

namespace xrpl {

template <typename ViewT>
class SponsorshipEntry : public SLEBase<ViewT, ltSPONSORSHIP>
{
public:
    using Base = SLEBase<ViewT, ltSPONSORSHIP>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit SponsorshipEntry(
        AccountID const& sponsor,
        AccountID const& sponsee,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::sponsorship(sponsor, sponsee), view, j)
    {
    }
};

using RSponsorshipEntry = SponsorshipEntry<ReadView>;
using WSponsorshipEntry = SponsorshipEntry<ApplyView>;

}  // namespace xrpl
