//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2012-2015 Ripple Labs Inc.

  Permission to use, copy, modify, and/or distribute this software for any
  purpose  with  or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
  MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <BeastConfig.h>
#include <test/jtx/Account.h>
#include <test/jtx/amount.h>
#include <cassert>
#include <cmath>
#include <iomanip>

namespace ripple {
namespace test {
namespace jtx {

#if 0
std::ostream&
operator<<(std::ostream&& os,
    AnyAmount const& amount)
{
    if (amount.is_any)
    {
        os << amount.value.getText() << "/" <<
            to_string(amount.value.issue().currency) <<
                "*";
        return os;
    }
    os << amount.value.getText() << "/" <<
        to_string(amount.value.issue().currency) <<
            "(" << amount.name() << ")";
    return os;
}
#endif

PrettyAmount::operator AnyAmount() const
{
    return { amount_ };
}

template <typename T>
static
std::string
to_places(const T d, std::uint8_t places)
{
    assert(places <= std::numeric_limits<T>::digits10);

    std::ostringstream oss;
    oss << std::setprecision(places) << std::fixed << d;

    std::string out = oss.str();
    out.erase(out.find_last_not_of('0') + 1, std::string::npos);
    if (out.back() == '.')
        out.pop_back();

    return out;
}

std::ostream&
operator<< (std::ostream& os,
    PrettyAmount const& amount)
{
    if (amount.value().native())
    {
        // measure in hundredths
        auto const c =
            dropsPerXRP<int>::value / 100;
        auto const n = amount.value().mantissa();
        if(n < c)
        {
            if (amount.value().negative())
                os << "-" << n << " drops";
            else
                os << n << " drops";
            return os;
        }
        auto const d = double(n) /
            dropsPerXRP<int>::value;
        if (amount.value().negative())
            os << "-";

        os << to_places(d, 6) << " XRP";
    }
    else
    {
        os <<
            amount.value().getText() << "/" <<
                to_string(amount.value().issue().currency) <<
                    "(" << amount.name() << ")";
    }
    return os;
}

//------------------------------------------------------------------------------

XRP_t const XRP {};

PrettyAmount
IOU::operator()(epsilon_t) const
{
    return { STAmount(issue(), 1, -81),
        account.name() };
}

PrettyAmount
IOU::operator()(detail::epsilon_multiple m) const
{
    return { STAmount(issue(),
        static_cast<std::uint64_t>(m.n), -81),
            account.name() };
}

std::ostream&
operator<<(std::ostream& os,
    IOU const& iou)
{
    os <<
        to_string(iou.issue().currency) <<
            "(" << iou.account.name() << ")";
    return os;
}

any_t const any { };

} // jtx
} // test
} // ripple
