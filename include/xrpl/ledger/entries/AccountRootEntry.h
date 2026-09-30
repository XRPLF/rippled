#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>

namespace xrpl {

struct Adjustment
{
    std::int32_t ownerCountDelta = 0;
    std::int32_t accountCountDelta = 0;
};

template <typename ViewT>
class AccountRootEntry : public SLEBase<ViewT, ltACCOUNT_ROOT>
{
public:
    using Base = SLEBase<ViewT, ltACCOUNT_ROOT>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit AccountRootEntry(
        AccountID const& id,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::account(id), view, j)
    {
    }

    /**
     * Returns the account reserve, in drops.
     *
     * Actual owner count can be adjusted by delta in ownerCountAdj
     * Actual reserve count can be adjusted by delta in accountCountAdj
     * The reserve is calculated as:
     * (ownerCount + "sponsoring object count" - "sponsored object count" + additionalOwnerCount) *
     * increment + (1 if not sponsored account + sponsoringAccountCount) * "reserve base"
     *
     * @param adj Adjustment to the owner/account count (default: 0/0). Positive to add, negative
     * to subtract.
     * @return The account reserve amount in drops
     */
    [[nodiscard]] XRPAmount
    reserve(Adjustment adj = {}) const;
};

using AccountRootEntryR = AccountRootEntry<ReadView>;
using AccountRootEntryW = AccountRootEntry<ApplyView>;

}  // namespace xrpl
