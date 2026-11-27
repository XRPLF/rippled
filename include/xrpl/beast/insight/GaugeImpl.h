#pragma once

#include <cstdint>
#include <memory>

namespace beast::insight {

class Gauge;

class GaugeImpl : public std::enable_shared_from_this<GaugeImpl>
{
public:
    using ValueType = std::uint64_t;
    using DifferenceType = std::int64_t;

    virtual ~GaugeImpl() = 0;
    virtual void
    set(ValueType value) = 0;
    virtual void
    increment(DifferenceType amount) = 0;
};

}  // namespace beast::insight
