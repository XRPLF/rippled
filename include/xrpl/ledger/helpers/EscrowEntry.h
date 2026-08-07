#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
class EscrowEntry : public SLEBase<ViewT, ltESCROW>
{
public:
    using Base = SLEBase<ViewT, ltESCROW>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit EscrowEntry(
        AccountID const& src,
        std::uint32_t seq,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::escrow(src, seq), view, j)
    {
    }
};

using REscrowEntry = EscrowEntry<ReadView>;
using WEscrowEntry = EscrowEntry<ApplyView>;

}  // namespace xrpl
