#ifndef XRPL_PATH_IMPL_PATHFINDERUTILS_H_INCLUDED
#define XRPL_PATH_IMPL_PATHFINDERUTILS_H_INCLUDED

#include <xrpl/protocol/STAmount.h>

namespace ripple {

inline STAmount
largestAmount(STAmount const& amt)
{
    if (amt.native())
        return INITIAL_XRP;

    return STAmount(amt.issue(), STAmount::cMaxValue, STAmount::cMaxOffset);
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

}  // namespace ripple

#endif
