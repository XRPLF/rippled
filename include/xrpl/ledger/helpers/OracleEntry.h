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
class OracleEntry : public SLEBase<ViewT, ltORACLE>
{
public:
    using Base = SLEBase<ViewT, ltORACLE>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit OracleEntry(
        AccountID const& account,
        std::uint32_t documentID,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::oracle(account, documentID), view, j)
    {
    }
};

using ROracleEntry = OracleEntry<ReadView>;
using WOracleEntry = OracleEntry<ApplyView>;

}  // namespace xrpl
