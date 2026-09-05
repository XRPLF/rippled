#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>

namespace xrpl {

template <typename ViewT>
class NFTokenPageEntry : public SLEBase<ViewT, ltNFTOKEN_PAGE>
{
public:
    using Base = SLEBase<ViewT, ltNFTOKEN_PAGE>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit NFTokenPageEntry(
        Keylet const& page,
        uint256 const& token,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::nftokenPage(page, token), view, j)
    {
    }
};

using NFTokenPageEntryR = NFTokenPageEntry<ReadView>;
using NFTokenPageEntryW = NFTokenPageEntry<ApplyView>;

}  // namespace xrpl
