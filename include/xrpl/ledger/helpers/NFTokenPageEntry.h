#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/Keylet.h>

namespace xrpl {

template <typename ViewT>
class NFTokenPageEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit NFTokenPageEntry(
        Keylet const& page,
        uint256 const& token,
        SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::nftokenPage(page, token), view, j)
    {
    }
};

}  // namespace xrpl
