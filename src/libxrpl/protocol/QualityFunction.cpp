#include <xrpl/protocol/QualityFunction.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/Quality.h>

#include <optional>
#include <stdexcept>

namespace xrpl {

QualityFunction::QualityFunction(Quality const& quality, QualityFunction::CLOBLikeTag)
    : m_(0), b_(0), quality_(quality)
{
    if (quality.rate() <= beast::kZero)
        Throw<std::runtime_error>("QualityFunction quality rate is 0.");
    b_ = 1 / quality.rate();
}

void
QualityFunction::combine(QualityFunction const& qf)
{
    m_ += b_ * qf.m_;
    b_ *= qf.b_;
    if (m_ != 0)
        quality_ = std::nullopt;
}

std::optional<Number>
QualityFunction::outFromAvgQ(Quality const& quality)
{
    if (m_ != 0 && quality.rate() != beast::kZero)
    {
        SaveNumberRoundMode const rm(Number::setround(Number::RoundingMode::Upward));
        auto const out = (1 / quality.rate() - b_) / m_;
        if (out <= 0)
            return std::nullopt;
        return out;
    }
    // The sole caller (StrandFlow::limitOut) only invokes this on a non-const
    // quality function, so m_ != 0 here, and a real payment/offer never yields
    // a zero-rate limit quality (it would divide by zero above). This fallback
    // is therefore unreachable in practice.
    return std::nullopt;  // LCOV_EXCL_LINE
}

bool
QualityFunction::satisfiesAvgQ(Quality const& quality, Number const& out) const
{
    // satisfiesAvgQ is only reached from StrandFlow::limitOut *after*
    // outFromAvgQ returned a value, which requires a non-zero rate. So a
    // zero-rate quality never reaches here; this guard is defensive.
    if (quality.rate() == beast::kZero)
        return false;  // LCOV_EXCL_LINE
    return m_ * out + b_ >= 1 / quality.rate();
}

}  // namespace xrpl
