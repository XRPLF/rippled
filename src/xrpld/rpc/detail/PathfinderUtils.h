#pragma once

#include <xrpl/protocol/STAmount.h>

namespace xrpl {

inline STAmount
largestAmount(STAmount const& amt)
{
    return amt.asset().visit(
        [&](Issue const& issue) -> STAmount {
            if (issue.native())
                return kInitialXrp;
            return STAmount(amt.asset(), STAmount::kMaxValue, STAmount::kMaxOffset);
        },
        [&](MPTIssue const&) { return STAmount(amt.asset(), kMaxMpTokenAmount, 0); });
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
