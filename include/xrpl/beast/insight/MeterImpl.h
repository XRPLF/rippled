#pragma once

#include <cstdint>
#include <memory>

namespace beast::insight {

class Meter;

class MeterImpl : public std::enable_shared_from_this<MeterImpl>
{
public:
    using ValueType = std::uint64_t;

    virtual ~MeterImpl() = 0;
    virtual void
    increment(ValueType amount) = 0;
};

}  // namespace beast::insight
