#pragma once

#include <xrpl/protocol/STAmount.h>
#include <xrpl/basics/TraceLog.h>

namespace xrpl {

inline STAmount
largestAmount(STAmount const& amt)
{
    TRACE_FUNC();
    return amt.asset().visit(
        [&](Issue const& issue) -> STAmount {
            if (issue.native())
                return kINITIAL_XRP;
            return STAmount(amt.asset(), STAmount::kMAX_VALUE, STAmount::kMAX_OFFSET);
        },
        [&](MPTIssue const&) { return STAmount(amt.asset(), kMAX_MP_TOKEN_AMOUNT, 0); });
}

inline STAmount
convertAmount(STAmount const& amt, bool all)
{
    TRACE_FUNC();
    if (!all)
        return amt;

    return largestAmount(amt);
};

inline bool
convertAllCheck(STAmount const& a)
{
    TRACE_FUNC();
    return a == largestAmount(a);
}

}  // namespace xrpl
