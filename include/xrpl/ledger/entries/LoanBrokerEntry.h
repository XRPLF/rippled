#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>
#include <string_view>

namespace xrpl {

template <typename ViewT>
class LoanBrokerEntry : public SLEBase<ViewT, ltLOAN_BROKER>
{
public:
    using Base = SLEBase<ViewT, ltLOAN_BROKER>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit LoanBrokerEntry(
        AccountID const& owner,
        SeqProxy const& seq,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::loanBroker(owner, seq), view, j)
    {
    }

    explicit LoanBrokerEntry(
        UInt256 const& loanBrokerID,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::loanBroker(loanBrokerID), view, j)
    {
    }

    /**
     * Broker cover preclaim precision guard (fixCleanup3_2_0).
     *
     * Prevents a "silent sub-ULP no-op" where a deposit, withdrawal, or
     * clawback amount is so small that it rounds to zero at
     * `sfCoverAvailable`'s scale. Without this guard, both the pseudo
     * trust-line and `sfCoverAvailable` would identically absorb the rounded
     * zero, resulting in a successful transaction (tesSUCCESS) where no
     * funds actually moved.
     *
     * @param vaultAsset The underlying vault asset (the broker's cover
     * asset).
     * @param amount The effective subtraction/addition amount.
     * @param logPrefix Transactor name for log diagnostics.
     *
     * @return `tecPRECISION_LOSS` if the request rounds to zero at cover
     * scale. `tesSUCCESS` if the amendment is disabled or the request is
     * safely supra-ULP.
     */
    [[nodiscard]] TER
    canApplyToCover(Asset const& vaultAsset, STAmount const& amount, std::string_view logPrefix)
        const;

    /**
     * Adjust this LoanBroker's owner count.
     *
     * A LoanBroker's sfOwnerCount tracks the number of outstanding loans on
     * this broker; it is not a reserve-backed owner count and is distinct
     * from the broker's pseudo-account's owner count. Loans can never carry
     * a reserve sponsor (LoanSet rejects reserve sponsorship at preflight),
     * so this never involves sponsor accounting and never invokes the
     * ownerCountHook used for ACCOUNT_ROOT reserve tracking.
     *
     * @param delta Amount to add (positive) or remove (negative) from the
     * count.
     */
    void
    adjustOwnerCount(std::int32_t delta)
        requires Base::kIsWritable;
};

using LoanBrokerEntryR = LoanBrokerEntry<ReadView>;
using LoanBrokerEntryW = LoanBrokerEntry<ApplyView>;

}  // namespace xrpl
