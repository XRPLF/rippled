#include <test/jtx/Account.h>
#include <test/jtx/amount.h>

#include <xrpl/basics/safe_cast.h>

#include <iomanip>

namespace xrpl {
namespace test {
namespace jtx {

#if 0
std::ostream&
operator<<(std::ostream&& os,
    AnyAmount const& amount)
{
    if (amount.isAny)
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

PrettyAmount::
operator AnyAmount() const
{
    return {amount_};
}

template <typename T>
static std::string
toPlaces(T const d, std::uint8_t places)
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
    if (amount.value().native())
    {
        // measure in hundredths
        auto const c = kDROPS_PER_XRP.drops() / 100;
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
            return os;
        }
        auto const d = double(n) / kDROPS_PER_XRP.drops();
        if (amount.value().negative())
            os << "-";

        os << toPlaces(d, 6) << " XRP";
    }
    else if (amount.value().holds<Issue>())
    {
        os << amount.value().getText() << "/" << to_string(amount.value().issue().currency) << "("
           << amount.name() << ")";
    }
    else
    {
        auto const& mptIssue = amount.value().asset().get<MPTIssue>();
        os << amount.value().getText() << "/" << to_string(mptIssue) << "(" << amount.name() << ")";
    }
    return os;
}

//------------------------------------------------------------------------------

XrpT const XRP{};

PrettyAmount
IOU::operator()(EpsilonT) const
{
    return {STAmount(issue(), 1, -81), account.name()};
}

PrettyAmount
IOU::operator()(detail::EpsilonMultiple m) const
{
    return {STAmount(issue(), safe_cast<std::uint64_t>(m.n), -81), account.name()};
}

std::ostream&
operator<<(std::ostream& os, IOU const& iou)
{
    os << to_string(iou.issue().currency) << "(" << iou.account.name() << ")";
    return os;
}

AnyT const kANY{};

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
