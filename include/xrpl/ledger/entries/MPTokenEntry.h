#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/UintTypes.h>

namespace xrpl {

template <typename ViewT>
class MPTokenEntry : public SLEBase<ViewT, ltMPTOKEN>
{
public:
    using Base = SLEBase<ViewT, ltMPTOKEN>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit MPTokenEntry(
        MPTID const& issuanceID,
        AccountID const& holder,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::mptoken(issuanceID, holder), view, j)
    {
    }

    explicit MPTokenEntry(
        UInt256 const& issuanceKey,
        AccountID const& holder,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::mptoken(issuanceKey, holder), view, j)
    {
    }

    explicit MPTokenEntry(
        UInt256 const& mptokenKey,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::mptoken(mptokenKey), view, j)
    {
    }

    /**
     * Returns true if this MPToken carries the individual-lock flag
     * (lsfMPTLocked).
     *
     * @warning This checks only the raw per-holder lock bit. It does not
     * perform the transitive vault pseudo-account check. Use isFrozen() to
     * decide whether the holder may send or receive tokens.
     *
     * @return true if lsfMPTLocked is set on this MPToken
     */
    [[nodiscard]] bool
    isIndividualFrozen() const
    {
        return (*this)->isFlag(lsfMPTLocked);
    }
};

using MPTokenEntryR = MPTokenEntry<ReadView>;
using MPTokenEntryW = MPTokenEntry<ApplyView>;

}  // namespace xrpl
