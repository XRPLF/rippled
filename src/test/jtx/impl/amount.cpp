#include <test/jtx/amount.h>

#include <test/jtx/Account.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/UintTypes.h>

#include <cassert>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <ostream>
#include <sstream>
#include <string>

namespace xrpl::test::jtx {

PrettyAmount::
operator AnyAmount() const
{
    return {amount_};
}

template <typename T>
static std::string
to_places(T const d, std::uint8_t places)
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
operator<<(std::ostream& os, PrettyAmount const& amount)
{
    amount.value().asset().visit(
        [&](Issue const& issue) {
            if (issue.native())
            {
                // measure in hundredths
                auto const c = dropsPerXRP.drops() / 100;
                auto const n = amount.value().mantissa();
                if (n < c)
                {
                    if (amount.value().negative())
                    {
                        os << "-" << n << " drops";
                    }
                    else
                    {
                        os << n << " drops";
                    }
                }
                else
                {
                    auto const d = double(n) / dropsPerXRP.drops();
                    if (amount.value().negative())
                    {
                        os << "-";
                    }

                    os << to_places(d, 6) << " XRP";
                }
            }
            else
            {
                os << amount.value().getText() << "/" << to_string(issue.currency) << "("
                   << amount.name() << ")";
            }
        },
        [&](MPTIssue const& issue) {
            os << amount.value().getText() << "/" << to_string(issue) << "(" << amount.name()
               << ")";
        });
    return os;
}

//------------------------------------------------------------------------------

XRP_t const XRP{};

PrettyAmount
IOU::operator()(epsilon_t) const
{
    return {STAmount(issue(), 1, -81), account.name()};
}

PrettyAmount
IOU::operator()(xrpl::detail::epsilon_multiple m) const
{
    return {STAmount(issue(), safe_cast<std::uint64_t>(m.n), -81), account.name()};
}

std::ostream&
operator<<(std::ostream& os, IOU const& iou)
{
    os << to_string(iou.currency) << "(" << iou.account.name() << ")";
    return os;
}

std::ostream&
operator<<(std::ostream& os, MPT const& mpt)
{
    os << to_string(mpt.issuanceID);
    return os;
}

any_t const any{};

}  // namespace xrpl::test::jtx
