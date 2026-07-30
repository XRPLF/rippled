#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>

namespace xrpl {

template <typename ViewT>
class AMMEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit AMMEntry(
        Asset const& issue1,
        Asset const& issue2,
        SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::amm(issue1, issue2), view, j)
    {
    }

    explicit AMMEntry(
        uint256 const& ammID,
        SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::amm(ammID), view, j)
    {
    }
};

}  // namespace xrpl
