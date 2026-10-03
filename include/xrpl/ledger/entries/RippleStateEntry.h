#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
class RippleStateEntry : public SLEBase<ViewT, ltRIPPLE_STATE>
{
public:
    using Base = SLEBase<ViewT, ltRIPPLE_STATE>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit RippleStateEntry(
        AccountID const& id0,
        AccountID const& id1,
        Currency const& currency,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::trustLine(id0, id1, currency), view, j)
    {
    }

    explicit RippleStateEntry(
        AccountID const& id,
        Issue const& issue,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::trustLine(id, issue), view, j)
    {
    }

    /**
     * Returns the limit that @p account sets on this line for IOUs issued
     * by @p issuer. The issuer of the result is @p account.
     */
    [[nodiscard]] STAmount
    creditLimit(AccountID const& account, AccountID const& issuer) const
    {
        STAmount result = (*this)->getFieldAmount(account < issuer ? sfLowLimit : sfHighLimit);
        result.get<Issue>().account = account;
        return result;
    }

    /**
     * Returns the amount of IOUs issued by @p issuer that @p account holds
     * on this line. The issuer of the result is @p account.
     */
    [[nodiscard]] STAmount
    creditBalance(AccountID const& account, AccountID const& issuer) const
    {
        STAmount result = (*this)->getFieldAmount(sfBalance);
        if (account < issuer)
            result.negate();
        result.get<Issue>().account = account;
        return result;
    }

    /**
     * Returns true if @p issuer froze this line for @p account.
     */
    [[nodiscard]] bool
    isIndividualFrozen(AccountID const& account, AccountID const& issuer) const
    {
        return (*this)->isFlag((issuer > account) ? lsfHighFreeze : lsfLowFreeze);
    }

    /**
     * Returns true if either side deep-froze this line.
     */
    [[nodiscard]] bool
    isDeepFrozen() const
    {
        return (*this)->isFlag(lsfHighDeepFreeze) || (*this)->isFlag(lsfLowDeepFreeze);
    }

    /**
     * Returns true if @p issuer authorized @p account on this line.
     *
     * @note This checks only the line's auth flag. Whether the issuer
     * requires authorization (lsfRequireAuth) is on the issuer AccountRoot;
     * see requireAuth().
     */
    [[nodiscard]] bool
    isAuthorized(AccountID const& account, AccountID const& issuer) const
    {
        return (*this)->isFlag((account > issuer) ? lsfLowAuth : lsfHighAuth);
    }

    /**
     * Removes this line from the owner directories of @p lowAccount and
     * @p highAccount, clears both sponsors, and erases it.
     *
     * @return tesSUCCESS, or tefBAD_LEDGER if a directory removal fails
     */
    [[nodiscard]] TER
    removeFromLedger(AccountID const& lowAccount, AccountID const& highAccount)
        requires Base::kIsWritable;

    /**
     * Create a trust line
     *
     * This can set an initial balance.
     */
    [[nodiscard]] static TER
    create(
        ApplyView& view,
        bool const bSrcHigh,
        AccountID const& uSrcAccountID,
        AccountID const& uDstAccountID,
        UInt256 const& uIndex,      // --> ripple state entry
        SLE::Ref sleAccount,        // --> the account being set.
        bool const bAuth,           // --> authorize account.
        bool const bNoRipple,       // --> others cannot ripple through
        bool const bFreeze,         // --> funds cannot leave
        bool bDeepFreeze,           // --> can neither receive nor send funds
        STAmount const& saBalance,  // --> balance of account being set.
                                    // Issuer should be noAccount()
        STAmount const& saLimit,    // --> limit for account being set.
                                    // Issuer should be the account being set.
        std::uint32_t uQualityIn,
        std::uint32_t uQualityOut,
        SLE::Ref sponsorSle,
        beast::Journal j)
        requires Base::kIsWritable;
};

using RippleStateEntryR = RippleStateEntry<ReadView>;
using RippleStateEntryW = RippleStateEntry<ApplyView>;

}  // namespace xrpl
