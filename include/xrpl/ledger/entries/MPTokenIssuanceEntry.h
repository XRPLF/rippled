#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
class MPTokenIssuanceEntry : public SLEBase<ViewT, ltMPTOKEN_ISSUANCE>
{
public:
    using Base = SLEBase<ViewT, ltMPTOKEN_ISSUANCE>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit MPTokenIssuanceEntry(
        std::uint32_t seq,
        AccountID const& issuer,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::mptokenIssuance(makeMptID(seq, issuer)), view, j)
    {
    }

    explicit MPTokenIssuanceEntry(
        MPTID const& issuanceID,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::mptokenIssuance(issuanceID), view, j)
    {
    }

    explicit MPTokenIssuanceEntry(
        UInt256 const& issuanceKey,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::mptokenIssuance(issuanceKey), view, j)
    {
    }

    /**
     * Returns true if the issuance is locked (lsfMPTLocked).
     */
    [[nodiscard]] bool
    isGlobalFrozen() const
    {
        return (*this)->isFlag(lsfMPTLocked);
    }

    /**
     * Returns the issuance's MaximumAmount, or the protocol maximum if the
     * field is absent. Never exceeds 2**63-1.
     */
    [[nodiscard]] std::int64_t
    maxAmount() const;

    /**
     * Returns maxAmount() minus OutstandingAmount.
     *
     * OutstandingAmount may overflow, so the result might be negative, but it
     * is always <= |MaximumAmount - OutstandingAmount|.
     */
    [[nodiscard]] std::int64_t
    availableAmount() const
    {
        auto const max = maxAmount();
        auto const outstanding = (**this)[sfOutstandingAmount];
        return max - outstanding;
    }

    /**
     * Returns the MPT transfer fee as a Rate, in fractions of 1 billion
     * (a 1% fee is 1,010,000,000). Returns parity if TransferFee is absent.
     */
    [[nodiscard]] Rate
    transferRate() const;

    /**
     * Returns tesSUCCESS if a holding of this issuance may be added:
     * tecOBJECT_NOT_FOUND if the issuance does not exist, tecNO_AUTH if
     * lsfMPTCanTransfer is not set.
     */
    [[nodiscard]] TER
    canAddHolding() const
    {
        if (!this->exists())
        {
            return tecOBJECT_NOT_FOUND;
        }
        if (!(*this)->isFlag(lsfMPTCanTransfer))
        {
            return tecNO_AUTH;
        }

        return tesSUCCESS;
    }
};

using MPTokenIssuanceEntryR = MPTokenIssuanceEntry<ReadView>;
using MPTokenIssuanceEntryW = MPTokenIssuanceEntry<ApplyView>;

}  // namespace xrpl
