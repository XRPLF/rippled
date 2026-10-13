#pragma once

#include <cstdint>
#include <memory>

namespace beast::insight {

class Counter;

class CounterImpl : public std::enable_shared_from_this<CounterImpl>
{
public:
    using ValueType = std::int64_t;

    virtual ~CounterImpl() = 0;
    virtual void
    increment(ValueType amount) = 0;
};

}  // namespace beast::insight
