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

#include <cstdint>

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

    /**
     * Returns true if @p account cannot send or receive tokens of this
     * MPToken's issuance because a freeze applies: the issuance is globally
     * locked, this MPToken is individually locked, or (for a vault share)
     * the vault pseudo-account's underlying asset is frozen.
     *
     * The issuance is read once for the global-freeze and vault checks.
     *
     * @param account The holder of this MPToken.
     * @param depth Current recursion depth for the vault-share walk.
     * @return true if @p account is frozen out of this MPToken's issuance
     */
    [[nodiscard]] bool
    isFrozen(AccountID const& account, std::uint8_t depth = 0) const;
};

using MPTokenEntryR = MPTokenEntry<ReadView>;
using MPTokenEntryW = MPTokenEntry<ApplyView>;

}  // namespace xrpl
