#pragma once

#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/UintTypes.h>

namespace xrpl {

class PathFindMPT final
{
private:
    MPTID const mptID_;
    // If true then holder's balance is 0, always false for issuer
    bool const zeroBalance_;
    // OutstandingAmount is equal to MaximumAmount
    bool const maxedOut_;

public:
    PathFindMPT(MPTID const& mptID) : mptID_(mptID), zeroBalance_(false), maxedOut_(false)
    {
    }
    PathFindMPT(MPTID const& mptID, bool zeroBalance, bool maxedOut)
        : mptID_(mptID), zeroBalance_(zeroBalance), maxedOut_(maxedOut)
    {
    }
    operator MPTID const&() const
    {
        return mptID_;
    }
    [[nodiscard]] MPTID const&
    getMptID() const
    {
        return mptID_;
    }
    [[nodiscard]] bool
    canSend(AccountID const& account) const
    {
        // A maxed-out issuance only prevents the issuer from creating more
        // MPT. Holders can still send existing balances.
        return account == getMPTIssuer(mptID_) ? !maxedOut_ : !zeroBalance_;
    }
};

}  // namespace xrpl
