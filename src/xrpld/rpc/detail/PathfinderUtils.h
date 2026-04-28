#pragma once

#include <xrpl/protocol/STAmount.h>

namespace xrpl {

inline STAmount
largestAmount(STAmount const& amt)
{
    return amt.asset().visit(
        [&](Issue const& issue) -> STAmount {
            if (issue.native())
                return INITIAL_XRP;
            return STAmount(amt.asset(), STAmount::cMaxValue, STAmount::cMaxOffset);
        },
        [&](MPTIssue const&) { return STAmount(amt.asset(), maxMPTokenAmount, 0); });
}

inline STAmount
convertAmount(STAmount const& amt, bool all)
{
    if (!all)
        return amt;

    return largestAmount(amt);
};

inline bool
convertAllCheck(STAmount const& a)
{
    return a == largestAmount(a);
}

}  // namespace xrpl
