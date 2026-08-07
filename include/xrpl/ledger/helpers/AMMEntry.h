#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>

namespace xrpl {

template <typename ViewT>
class AMMEntry : public SLEBase<ViewT, ltAMM>
{
public:
    using Base = SLEBase<ViewT, ltAMM>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit AMMEntry(
        Asset const& issue1,
        Asset const& issue2,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::amm(issue1, issue2), view, j)
    {
    }

    explicit AMMEntry(
        uint256 const& ammID,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::amm(ammID), view, j)
    {
    }
};

using RAMMEntry = AMMEntry<ReadView>;
using WAMMEntry = AMMEntry<ApplyView>;

}  // namespace xrpl
