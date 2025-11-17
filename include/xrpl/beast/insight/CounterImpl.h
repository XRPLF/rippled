#ifndef BEAST_INSIGHT_COUNTERIMPL_H_INCLUDED
#define BEAST_INSIGHT_COUNTERIMPL_H_INCLUDED

#include <cstdint>
#include <memory>

namespace beast {
namespace insight {

class Counter;

class CounterImpl : public std::enable_shared_from_this<CounterImpl>
{
public:
    using value_type = std::int64_t;

    virtual ~CounterImpl() = 0;
    virtual void
    increment(value_type amount) = 0;
};

}  // namespace insight
}  // namespace beast

#endif
