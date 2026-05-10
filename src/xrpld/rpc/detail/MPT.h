#pragma once

#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/basics/TraceLog.h>

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
    TRACE_FUNC();
        return mptID_;
    }
    [[nodiscard]] MPTID const&
    getMptID() const
    {
    TRACE_FUNC();
        return mptID_;
    }
    [[nodiscard]] bool
    isZeroBalance() const
    {
    TRACE_FUNC();
        return zeroBalance_;
    }
    [[nodiscard]] bool
    isMaxedOut() const
    {
    TRACE_FUNC();
        return maxedOut_;
    }
};

}  // namespace xrpl
