#pragma once

#include <cstdint>
#include <memory>

namespace beast {
namespace insight {

class Meter;

class MeterImpl : public std::enable_shared_from_this<MeterImpl>
{
public:
    using value_type = std::uint64_t;

    virtual ~MeterImpl() = 0;
    virtual void
    increment(value_type amount) = 0;
};

}  // namespace insight
}  // namespace beast
