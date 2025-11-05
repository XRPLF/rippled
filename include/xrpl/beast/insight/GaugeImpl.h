#ifndef BEAST_INSIGHT_GAUGEIMPL_H_INCLUDED
#define BEAST_INSIGHT_GAUGEIMPL_H_INCLUDED

#include <cstdint>
#include <memory>

namespace beast {
namespace insight {

class Gauge;

class GaugeImpl : public std::enable_shared_from_this<GaugeImpl>
{
public:
    using value_type = std::uint64_t;
    using difference_type = std::int64_t;

    virtual ~GaugeImpl() = 0;
    virtual void
    set(value_type value) = 0;
    virtual void
    increment(difference_type amount) = 0;
};

}  // namespace insight
}  // namespace beast

#endif
