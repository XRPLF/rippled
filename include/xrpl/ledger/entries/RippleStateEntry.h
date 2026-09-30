#pragma once

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
#include <xrpl/protocol/UintTypes.h>

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
     *
     * @param account the account whose limit is returned.
     * @param issuer the account on the other side of the line.
     * @return the limit @p account sets on this line.
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
     *
     * @param account the account whose balance is returned.
     * @param issuer the account on the other side of the line.
     * @return the balance @p account holds on this line.
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
};

using RippleStateEntryR = RippleStateEntry<ReadView>;
using RippleStateEntryW = RippleStateEntry<ApplyView>;

}  // namespace xrpl
